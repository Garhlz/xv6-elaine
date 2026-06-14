// 缓冲缓存 (buffer cache)。
//
// 缓冲缓存是一个由 `struct buf` 组成的哈希链表，用于在内存中缓存磁盘块内容。
// 将磁盘块缓存在内存中可减少磁盘 I/O 次数，同时为多个进程并发访问同一磁盘块
// 提供同步点。
//
// 对外接口:
// * bread(dev, blockno) — 获取指定磁盘块的缓冲区，必要时从磁盘读取数据。
// * bwrite(buf)         — 将修改后的缓冲区写回磁盘。
// * brelse(buf)         — 释放缓冲区，调用后不要再使用该 buf。
// * bpin(buf) / bunpin(buf) — 增减引用计数（日志层用于将块"钉"在缓存中）。
// * 同一时刻只有一个进程可以持有某个缓冲区的睡眠锁，因此不要长时间持有。
//
// 内部实现要点:
// * 29 个哈希桶 (hash bucket)，按块号 hash 分布——命中路径只需锁住目标桶。
// * 一把全局淘汰锁 evict_lock 串行化 victim（被淘汰者）选择，避免无锁下
//   重复缓存同一块或跨桶迁移时的竞态。
// * 近似 LRU 淘汰: brelse() 用 ticks 打时间戳，bget() 未命中时扫描全桶，
//   选择 refcnt==0 且时间戳最老的 buf 作为 victim。
// * bpin/bunpin 直接操作引用计数（refcnt），不涉及睡眠锁——用于日志层在
//   某事务对块的读写完成之前防止该块被淘汰。

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

// 哈希桶数量，取素数 29 是为了让块号均匀散开，减少不同块落入同一桶的概率。
#define NBUCKET 29
#define BUCKET_HASH(blockno) ((blockno) % NBUCKET)

// bcache：全局唯一的缓冲区缓存实例。
// 字段说明:
//   evict_lock       — 全局淘汰锁（自旋锁），确保同一时刻只有一个 CPU 在做 victim 选择
//                       和跨桶迁移，防止两个 CPU 选中同一个 victim。
//   locks[NBUCKET]   — 每个哈希桶的自旋锁，保护该桶内 buf 链表的遍历和修改。
//   buf[NBUF]        — 所有 `struct buf` 的静态数组（NBUF=30，param.h 中定义）。
//   buckets[NBUCKET] — 每个桶的哨兵节点 (sentinel / dummy head)。哨兵的 prev/next
//                       指向自己表示空桶，实际缓存块插入在哨兵之后。
//   注意：buf[] 和 buckets[] 是分离的——buf[] 是实际存储，buckets[] 只是链表头。
struct {
    struct spinlock evict_lock;
    struct spinlock locks[NBUCKET];
    struct buf buf[NBUF];
    struct buf buckets[NBUCKET];
} bcache;

// 每个桶的锁名称字符串（用于调试输出），例如 "bcache_0", "bcache_1" ...
static char bcache_lock_names[NBUCKET][16];

// 初始化缓冲区缓存:
// 1. 初始化 evict_lock 和所有桶的自旋锁（名称存入 bcache_lock_names[][]）。
// 2. 将每个桶的哨兵节点初始化为自环（prev/next 指向自身），表示空桶。
// 3. 将所有 NBUF 个 `struct buf` 插入桶 0 的链表，并初始化每个 buf 的睡眠锁。
// 初始时所有 buf 都挂在桶 0 中，尚未绑定有效磁盘块；
// 后续随着 bget() 复用 victim，会逐渐迁移到对应 hash 桶。
void binit(void) {
    struct buf *b;

    // 初始化 evict_lock 和每个桶的自旋锁 + 哨兵节点
    initlock(&bcache.evict_lock, "bcache_evict");
    for (int i = 0; i < NBUCKET; i++) {
        snprintf(bcache_lock_names[i], sizeof(bcache_lock_names[i]), "bcache_%d", i);
        initlock(&bcache.locks[i], bcache_lock_names[i]);
        // 初始化，让每个桶的哨兵节点 prev/next 指向自己，表示空桶
        bcache.buckets[i].prev = &bcache.buckets[i];
        bcache.buckets[i].next = &bcache.buckets[i];
    }

    // 把所有 buf 初始插入桶 0（插入到哨兵之后、第一个原有节点之前）
    for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
        b->next = bcache.buckets[0].next;
        b->prev = &bcache.buckets[0];
        // 初始化每个buffer自己的睡眠锁，sleeplock 可以睡眠，适合磁盘 I/O 这种可能阻塞的场景。
        initsleeplock(&b->lock, "buffer");
        bcache.buckets[0].next->prev = b;
        bcache.buckets[0].next = b;
    }
}

// — bget() 的 helper 函数 —
//
// 这些 helper 各自承担一个清晰职责，遵循以下锁约定:
//   - bucket_lookup_and_ref / victim_still_available / bucket_remove /
//     bucket_insert_head 调用者负责持有对应桶锁，helper 内部不自行加锁。
//   - find_lru_victim 内部逐桶加锁和释放，不长期同时持有多把桶锁。
//   - 绝不在持有自旋锁时获取睡眠锁——acquiresleep 统一在 bget() 释放桶锁后调用。
//
// 关键不变量:
//   - 同一个 (dev, blockno) 在缓存中最多只有一个 `struct buf`。
//   - refcnt == 0 的 buf 才能被选为 victim 淘汰。
//   - 桶自旋锁保护桶链表及元数据字段（dev/blockno/refcnt/valid/timestamp）。
//   - evict_lock 串行化所有 miss/eviction 路径。

// 调用者必须持有 bucket_idx 对应的桶自旋锁。
// 在指定桶中查找 (dev, blockno)。命中则增加 refcnt 并返回 buf；否则返回 0。
static struct buf *bucket_lookup_and_ref(uint dev, uint blockno, int bucket_idx) {
    struct buf *b;

    for (b = bcache.buckets[bucket_idx].next; b != &bcache.buckets[bucket_idx]; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            return b;
        }
    }
    return 0;
}

// 扫描所有哈希桶，寻找 refcnt==0 且 timestamp 最旧的 buf 作为 victim。
// 通过 *old_bucket_idx 返回 victim 所在的桶号；若无可用 victim 则返回 0。
// 逐桶加锁、扫描、释放，避免同时持有多把桶锁。victim 可能在被选中后被
// 其他 CPU 抢先取走，因此调用者需要重新验证。
static struct buf *find_lru_victim(int *old_bucket_idx) {
    struct buf *victim = 0;
    uint oldest_ts = 0xffffffff;

    *old_bucket_idx = -1;
    for (int i = 0; i < NBUCKET; i++) {
        acquire(&bcache.locks[i]);
        for (struct buf *b = bcache.buckets[i].next; b != &bcache.buckets[i]; b = b->next) {
            if (b->refcnt == 0 && (victim == 0 || (int)(b->timestamp - oldest_ts) < 0)) {
                victim = b;
                *old_bucket_idx = i;
                oldest_ts = b->timestamp;
            }
        }
        release(&bcache.locks[i]);
    }
    return victim;
}

// 调用者必须持有 old_bucket_idx 对应的桶自旋锁。
// 验证 victim 仍可被淘汰: refcnt 仍为 0、块号未变（桶号未变）、仍在链表中。
// 返回 1 表示可用，0 表示已被其他 CPU 抢先取走。
static int victim_still_available(struct buf *victim, int old_bucket_idx) {
    struct buf *b;

    if (victim->refcnt != 0)
        return 0;
    if (BUCKET_HASH(victim->blockno) != old_bucket_idx)
        return 0;
    for (b = bcache.buckets[old_bucket_idx].next; b != &bcache.buckets[old_bucket_idx];
         b = b->next) {
        if (b == victim)
            return 1;
    }
    return 0;
}

// 调用者必须持有 b 所在桶的自旋锁。
// 从桶链表中摘除 b。
static void bucket_remove(struct buf *b) {
    b->next->prev = b->prev;
    b->prev->next = b->next;
}

// 调用者必须持有 bucket_idx 对应的桶自旋锁。
// 将 b 插入桶链表头部（哨兵节点之后）。
static void bucket_insert_head(struct buf *b, int bucket_idx) {
    b->next = bcache.buckets[bucket_idx].next;
    b->prev = &bcache.buckets[bucket_idx];
    bcache.buckets[bucket_idx].next->prev = b;
    bcache.buckets[bucket_idx].next = b;
}

// 获取设备 dev 上磁盘块 blockno 对应的缓冲区 (bget = buffer get)。
//
// 流程:
//   1. 快速命中:   锁目标桶 → bucket_lookup_and_ref() → 释放桶锁 → acquiresleep() → 返回。
//   2. 未命中:     获取 evict_lock → 双重检查 → find_lru_victim() →
//      victim_still_available() 验证 → bucket_remove() 摘除 →
//      bucket_insert_head() 插入目标桶 → 释放自旋锁 → acquiresleep() → 返回。
//
// 返回时一定持有 buf 的睡眠锁；若 valid==0 则 bread() 负责加载磁盘数据。
static struct buf *bget(uint dev, uint blockno) {
    int bucket_idx = BUCKET_HASH(blockno);
    struct buf *b;
    int old_bucket_idx;

    // 阶段 1: 快速命中，只需锁目标桶
    acquire(&bcache.locks[bucket_idx]);
    b = bucket_lookup_and_ref(dev, blockno, bucket_idx);
    release(&bcache.locks[bucket_idx]);
    if (b) {
        // 释放桶锁后才获取睡眠锁——绝不在持自旋锁时进入可能睡眠的路径
        acquiresleep(&b->lock);
        return b;
    }

    // 阶段 2: 未命中，获取全局淘汰锁
    acquire(&bcache.evict_lock);

retry:
    // 阶段 3: 双重检查——等待 evict_lock 期间其他 CPU 可能已缓存该块
    acquire(&bcache.locks[bucket_idx]);
    b = bucket_lookup_and_ref(dev, blockno, bucket_idx);
    release(&bcache.locks[bucket_idx]);
    if (b) {
        release(&bcache.evict_lock);
        acquiresleep(&b->lock);
        return b;
    }

    // 阶段 4: 选 LRU victim
    b = find_lru_victim(&old_bucket_idx);
    if (b == 0) {
        release(&bcache.evict_lock);
        panic("bget: no buffers");
    }

    // 阶段 5: 验证 victim 仍可用，然后从旧桶摘除
    acquire(&bcache.locks[old_bucket_idx]);
    if (!victim_still_available(b, old_bucket_idx)) {
        release(&bcache.locks[old_bucket_idx]);
        goto retry;
    }
    bucket_remove(b);
    release(&bcache.locks[old_bucket_idx]);

    // 阶段 6: 重设 victim 身份并插入目标桶
    acquire(&bcache.locks[bucket_idx]);
    b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
    b->timestamp = 0;
    bucket_insert_head(b, bucket_idx);
    release(&bcache.locks[bucket_idx]);

    release(&bcache.evict_lock);

    acquiresleep(&b->lock);
    return b;
}

// 读取磁盘块 blockno 并返回其缓冲区 (bread = buffer read)。
// 调用 bget() 获取缓冲区，若 buf->valid==0 表示数据未加载，则通过
// virtio_disk_rw() 发起磁盘读取（参数 0 表示读）。
// 返回时 buf->valid==1，调用者持有 buf 的睡眠锁，可直接访问 buf->data[]。
struct buf *bread(uint dev, uint blockno) {
    struct buf *b;

    b = bget(dev, blockno);
    if (!b->valid) {
        virtio_disk_rw(b, 0); // 0 = 读操作 (READ)
        b->valid = 1;
    }
    return b;
}

// 将缓冲区写回磁盘 (bwrite = buffer write)。
// 调用者必须先持有 buf 的睡眠锁（bread 返回时自动持有，或由调用者在 brelse
// 之前重新获取）。通过 holdingsleep() 断言验证。
// 参数 1 表示写入磁盘 (WRITE)；virtio_disk_rw() 会复制数据到磁盘驱动缓冲区，
// 并设置 buf->disk=1 表示磁盘正持有该 buf。
void bwrite(struct buf *b) {
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    virtio_disk_rw(b, 1); // 1 = 写操作 (WRITE)
}

// 释放缓冲区 (brelse = buffer release)。
// 1. 释放 buf 的睡眠锁（因此调用后其他 CPU 可以获取该 buf）。
// 2. 在持有桶自旋锁的情况下递减 refcnt。
// 3. 若 refcnt 降为 0，记录当前 ticks 作为 LRU 淘汰的 timestamp——
//    这表示该 buf 现在可以被选作 victim，且越早释放越可能被优先淘汰。
//
// 注意：brelse 不写回磁盘。写回由 bwrite() 负责，或者由日志层的 end_op()
// 批量完成。大多数文件系统读操作路径下，brelse 只是释放引用+锁。
void brelse(struct buf *b) {
    int bucket_idx;

    if (!holdingsleep(&b->lock))
        panic("brelse");

    releasesleep(&b->lock);

    bucket_idx = BUCKET_HASH(b->blockno);
    acquire(&bcache.locks[bucket_idx]);
    b->refcnt--;
    if (b->refcnt == 0)
        b->timestamp = ticks; // 打时间戳，bget() 的 LRU 扫描以此为淘汰依据
    release(&bcache.locks[bucket_idx]);
}

// pin / unpin：增减引用计数，不涉及睡眠锁。
// 日志层（log.c）使用这对函数将即将写入日志的块"钉"在缓存中——
// 只要 refcnt > 0，bget() 的全局扫描就不会选它为 victim，
// 从而保证日志事务期间该 buf 不会被淘汰。
//
// bpin()   — 引用计数 +1（pin，钉住），防止淘汰。
// bunpin() — 引用计数 -1（unpin，解除），允许淘汰。

// 不获取 buf 的睡眠锁，但会获取对应 bucket 的自旋锁来修改 refcnt。
void bpin(struct buf *b) {
    int bucket_idx = BUCKET_HASH(b->blockno);

    acquire(&bcache.locks[bucket_idx]);
    b->refcnt++;
    release(&bcache.locks[bucket_idx]);
}

void bunpin(struct buf *b) {
    int bucket_idx = BUCKET_HASH(b->blockno);

    acquire(&bcache.locks[bucket_idx]);
    b->refcnt--;
    release(&bcache.locks[bucket_idx]);
}

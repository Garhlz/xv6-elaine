// Buffer cache.
// ... (文件顶部的注释保持不变) ...

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
#define BUCKET_HASH(blockno) (blockno % NBUCKET)

struct
{
    struct spinlock locks[NBUCKET];
    struct buf buf[NBUF];
    struct buf buckets[NBUCKET];
} bcache;

void binit(void)
{
    struct buf *b;
    char lock_name[16];

    for (int i = 0; i < NBUCKET; i++)
    {
        snprintf(lock_name, sizeof(lock_name), "bcache_%d", i);
        initlock(&bcache.locks[i], lock_name);
        bcache.buckets[i].prev = &bcache.buckets[i];
        bcache.buckets[i].next = &bcache.buckets[i];
    }

    for (b = bcache.buf; b < bcache.buf + NBUF; b++)
    {
        b->next = bcache.buckets[0].next;
        b->prev = &bcache.buckets[0];
        initsleeplock(&b->lock, "buffer");
        bcache.buckets[0].next->prev = b;
        bcache.buckets[0].next = b;
    }
}

static struct buf *
bget(uint dev, uint blockno)
{
    struct buf *b;
    int bucket_idx = BUCKET_HASH(blockno);

    // --- 快速路径: 在自己的桶里查找 ---
    acquire(&bcache.locks[bucket_idx]);
    for (b = bcache.buckets[bucket_idx].next; b != &bcache.buckets[bucket_idx]; b = b->next)
    {
        if (b->dev == dev && b->blockno == blockno)
        {
            b->refcnt++;
            release(&bcache.locks[bucket_idx]);
            acquiresleep(&b->lock);
            return b;
        }
    }
    // 未命中，但我们继续持有锁，因为下面可能要回收一个buf并放入这个桶

    // --- 慢速路径: 回收一个 LRU buf ---
    struct buf *lru_buf = 0;
    uint oldest_ts = 0xFFFFFFFF;

    // 寻找全局最旧的、未被引用的 buf
    for (b = bcache.buf; b < bcache.buf + NBUF; b++)
    {
        if (b->refcnt == 0 && b->timestamp < oldest_ts)
        {
            oldest_ts = b->timestamp;
            lru_buf = b;
        }
    }

    if (lru_buf == 0)
    {
        // 所有 buf 都被引用，无法回收
        release(&bcache.locks[bucket_idx]);
        panic("bget: no buffers");
    }

    int old_bucket_idx = BUCKET_HASH(lru_buf->blockno);

    // 如果要回收的 buf 所在的旧桶不是我们当前锁住的新桶，
    // 我们需要获取旧桶的锁来安全地移除它。
    // 为了避免死锁，我们总是按索引从大到小的顺序获取锁。
    if (old_bucket_idx < bucket_idx)
    {
        acquire(&bcache.locks[old_bucket_idx]);
    }
    else if (old_bucket_idx > bucket_idx)
    {
        // 释放新桶锁，按顺序先锁旧桶，再锁新桶
        release(&bcache.locks[bucket_idx]);
        acquire(&bcache.locks[old_bucket_idx]);
        acquire(&bcache.locks[bucket_idx]);
    }
    // 如果 old_bucket_idx == bucket_idx, 我们已经持有锁了。

    if (lru_buf->refcnt != 0)
    {
        // 在我们等待锁的期间，这个 lru_buf 又被使用了。
        // 这是一个复杂的竞争，最简单的处理方式是释放所有锁，然后重试。
        if (old_bucket_idx != bucket_idx)
            release(&bcache.locks[old_bucket_idx]);
        release(&bcache.locks[bucket_idx]);
        return bget(dev, blockno);
    }

    // 从旧桶链表中移除
    lru_buf->next->prev = lru_buf->prev;
    lru_buf->prev->next = lru_buf->next;

    // 释放旧桶锁（如果它和新桶不同）
    if (old_bucket_idx != bucket_idx)
    {
        release(&bcache.locks[old_bucket_idx]);
    }

    // 更新 buf 元数据
    lru_buf->dev = dev;
    lru_buf->blockno = blockno;
    lru_buf->valid = 0;
    lru_buf->refcnt = 1;

    // 插入到新桶的链表头部 (我们还持有新桶的锁)
    lru_buf->next = bcache.buckets[bucket_idx].next;
    lru_buf->prev = &bcache.buckets[bucket_idx];
    bcache.buckets[bucket_idx].next->prev = lru_buf;
    bcache.buckets[bucket_idx].next = lru_buf;

    release(&bcache.locks[bucket_idx]);

    acquiresleep(&lru_buf->lock);
    return lru_buf;
}

struct buf *
bread(uint dev, uint blockno)
{
    struct buf *b;
    b = bget(dev, blockno);
    if (!b->valid)
    {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

void bwrite(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    virtio_disk_rw(b, 1);
}

void brelse(struct buf *b)
{
    if (!holdingsleep(&b->lock))
        panic("brelse");

    releasesleep(&b->lock);

    int bucket_idx = BUCKET_HASH(b->blockno);
    acquire(&bcache.locks[bucket_idx]);

    b->refcnt--;
    if (b->refcnt == 0)
    {
        extern uint ticks;
        b->timestamp = ticks;
    }

    release(&bcache.locks[bucket_idx]);
}

void bpin(struct buf *b)
{
    int bucket_idx = BUCKET_HASH(b->blockno);
    acquire(&bcache.locks[bucket_idx]);
    b->refcnt++;
    release(&bcache.locks[bucket_idx]);
}

void bunpin(struct buf *b)
{
    int bucket_idx = BUCKET_HASH(b->blockno);
    acquire(&bcache.locks[bucket_idx]);
    b->refcnt--;
    release(&bcache.locks[bucket_idx]);
}
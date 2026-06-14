// 文件系统实现，分为五层:
//   + 块层（Blocks）: 原始磁盘块的分配与回收
//   + 日志层（Log）: 多步更新的崩溃恢复（见 log.c）
//   + 文件层（Files）: inode 分配、读写、元数据管理
//   + 目录层（Directories）: 内容为目录项列表的特殊 inode
//   + 路径层（Names）: 如 /usr/rtm/xv6/fs.c 的路径解析
//
// 本文件包含底层文件系统操作例程。
// 高层系统调用实现在 sysfile.c 中。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
// 通常每个磁盘设备应该有一个超级块，但 xv6 只支持单个设备
struct superblock sb;
static uint balloc_start; // 上次分配位置，用于加速连续查找

// 从磁盘块 1 读取超级块到内存
static void readsb(int dev, struct superblock *sb) {
    struct buf *bp;

    bp = bread(dev, 1);
    memmove(sb, bp->data, sizeof(*sb));
    brelse(bp);
}

// 初始化文件系统：读取超级块，验证魔数，初始化日志
void fsinit(int dev) {
    readsb(dev, &sb);
    if (sb.magic != FSMAGIC)
        panic("invalid file system");
    initlog(dev, &sb);
}

// 将指定块全部清零
static void bzero(int dev, int bno) {
    struct buf *bp;

    bp = bread(dev, bno);
    memset(bp->data, 0, BSIZE);
    log_write(bp);
    brelse(bp);
}

// ---- 块分配 ----

// 在位图中查找空闲数据块，标记为已使用，清零后返回块号
// 从 balloc_start 开始向后扫描，到达末尾后从头继续
static uint balloc(uint dev) {
    uint base, b, start;
    int bi, m;
    struct buf *bp;

    bp = 0;
    start = balloc_start % sb.size;

    for (b = start; b < sb.size; b = base + BPB) {
        base = b - b % BPB;
        bp = bread(dev, BBLOCK(base, sb));
        for (bi = b - base; bi < BPB && base + bi < sb.size; bi++) {
            m = 1 << (bi % 8);
            if ((bp->data[bi / 8] & m) == 0) { // 该块空闲？
                bp->data[bi / 8] |= m;         // 标记为已使用。
                log_write(bp);
                brelse(bp);
                balloc_start = base + bi + 1;
                if (balloc_start >= sb.size)
                    balloc_start = 0;
                bzero(dev, base + bi);
                return base + bi;
            }
        }
        brelse(bp);
    }

    for (b = 0; b < start; b = base + BPB) {
        base = b - b % BPB;
        bp = bread(dev, BBLOCK(base, sb));
        for (bi = b - base; bi < BPB && base + bi < start; bi++) {
            m = 1 << (bi % 8);
            if ((bp->data[bi / 8] & m) == 0) { // 该块空闲？
                bp->data[bi / 8] |= m;         // 标记为已使用。
                log_write(bp);
                brelse(bp);
                balloc_start = base + bi + 1;
                if (balloc_start >= sb.size)
                    balloc_start = 0;
                bzero(dev, base + bi);
                return base + bi;
            }
        }
        brelse(bp);
    }
    panic("balloc: out of blocks");
}

// 释放磁盘块：在位图中将对应位清零
// 把磁盘块 b 标记为空闲，也就是把文件系统中对应位图块的位清零
static void bfree(int dev, uint b) {
    struct buf *bp;
    int bi, m;

    bp = bread(dev, BBLOCK(b, sb));
    bi = b % BPB;
    m = 1 << (bi % 8);
    if ((bp->data[bi / 8] & m) == 0)
        panic("freeing free block");
    bp->data[bi / 8] &= ~m;
    log_write(bp);
    brelse(bp);
}

// ---- Inode 管理 (inode management) ----
//
// inode 描述一个无名的文件实体。磁盘上的 dinode 保存元数据：
// 类型 (type)、大小 (size)、链接数 (nlink)、数据块地址列表 (addrs)。
//
// inode 在磁盘上从 sb.inodestart 开始顺序排列，每个 inode 有一个
// 编号 (inum, inode-number)，表示它在磁盘上的位置。
//
// 内核在内存中维护一个使用中的 inode 表 (itable)，用于在多进程
// 间同步对同一 inode 的访问。内存 inode (struct inode) 包含
// 磁盘上不存在的簿记信息：ref（引用计数）、valid（内容是否已加载）。
//
// inode 在使用前经历以下状态转换:
//
// * 分配 (Allocation): 磁盘上 type 非零即为已分配。ialloc() 负责分配，
//   iput() 在 ref 和 nlink 均为零时释放。
//
// * 引用 (Referencing): ip->ref 为 0 表示该 itable 条目空闲。
//   ip->ref 记录了指向该条目的内存指针数（打开的文件、当前目录等）。
//   iget() 查找或创建表项并递增 ref，iput() 递减 ref。
//
// * 有效 (Valid): ip->valid == 1 时，表中的 type/size 等信息才有效。
//   ilock() 从磁盘读取 inode 并设置 valid，iput() 在 ref 归零时清除 valid。
//
// * 锁定 (Locked): 文件系统代码必须先锁住 inode 才能检查和修改其内容。
//
// 典型使用流程:
//   ip = iget(dev, inum)    // 获取长期引用 (long-term reference)
//   ilock(ip)               // 短期锁定，加载磁盘数据到内存
//   ... 检查和修改 ip->xxx ...
//   iunlock(ip)             // 解锁
//   iput(ip)                // 释放引用 (put)
//
// ilock() 与 iget() 分离的设计目的: 系统调用可以在打开文件时获取
// 长期引用，只在 read/write 等操作期间短暂锁定。这也避免了路径名
// 查找 (pathname lookup) 中的死锁和竞态。iget() 增加 ip->ref
// 确保 inode 留在表中、指向它的指针始终有效。
//
// 锁的层级 (lock hierarchy):
//   itable.lock (自旋锁 spinlock) — 保护 itable 条目的分配，以及 ip->ref / ip->dev / ip->inum
//   ip->lock (睡眠锁 sleeplock)   — 保护 ip->valid / ip->size / ip->type 等其余字段

struct {
    struct spinlock lock;
    struct inode inode[NINODE];
} itable;

void iinit() {
    int i = 0;

    initlock(&itable.lock, "itable");
    for (i = 0; i < NINODE; i++) {
        initsleeplock(&itable.inode[i].lock, "inode");
    }
}

static struct inode *iget(uint dev, uint inum);

// — inode 生命周期管理函数 —
//
// 每个 inode 在使用前经历以下阶段:
//   ialloc()     在磁盘上找空闲 dinode，分配 → iget() 获取内存条目
//   ilock()      获取睡眠锁，首次加载时从磁盘读 dinode 到内存
//   ...          读写 ip->xxx 字段（持锁期间）
//   iupdate()    把修改过的 inode 写回磁盘
//   iunlock()    释放睡眠锁
//   iput()       递减引用计数；若为最后一次引用且 nlink==0，调用 itrunc() 释放所有块
//
// 锁的层级:
//   itable.lock (自旋锁) — 保护 itable[] 的分配和 ip->ref/dev/inum
//   ip->lock (睡眠锁)    — 保护 ip->valid/size/type/addrs[] 等字段

// 在设备 dev 上分配一个磁盘 inode，标记类型为 type，返回其内存 inode。
// 磁盘上: 找一个 type==0 的空闲 dinode → memset 清零 → 设 type → log_write 记录。
// 内存里: 调用 iget() 获取/创建对应的 `struct inode` 条目（未锁定，ref=1, valid=0）。
// TODO 顺序扫描，效率很低，可以加一个 next_inum 分配游标，避免每次都从 inode 1 开始扫
struct inode *ialloc(uint dev, short type) {
    int inum;
    struct buf *buf;
    struct dinode *disk_inode;

    // 遍历所有 dinode编号
    for (inum = 1; inum < sb.ninodes; inum++) {
        // 读出 inum 这个 inode 所在的磁盘块。
        buf = bread(dev, IBLOCK(inum, sb));
        // 在块内找到对应 inum 的 dinode 的地址
        disk_inode = (struct dinode *)buf->data + inum % IPB;
        if (disk_inode->type == 0) {
            // 找到一个空闲 dinode——清零、设置 type、通过日志写回磁盘
            memset(disk_inode, 0, sizeof(*disk_inode));
            disk_inode->type = type;
            log_write(buf);
            brelse(buf);
            return iget(dev, inum);
        }
        brelse(buf);
    }
    panic("ialloc: no inodes");
}

// 将内存 inode 的元数据写回磁盘上的 dinode。
// 每次修改 ip 中"磁盘可见"字段后必须调用。调用者必须持有 ip->lock。
void iupdate(struct inode *ip) {
    struct buf *buf;
    struct dinode *disk_inode;

    // 在内存缓冲区中的磁盘块的地址
    buf = bread(ip->dev, IBLOCK(ip->inum, sb));
    // 磁盘块的buf地址 + 当前inode的块内偏移
    disk_inode = (struct dinode *)buf->data + ip->inum % IPB;
    disk_inode->type = ip->type;
    disk_inode->major = ip->major;
    disk_inode->minor = ip->minor;
    disk_inode->nlink = ip->nlink;
    disk_inode->size = ip->size;
    memmove(disk_inode->addrs, ip->addrs, sizeof(ip->addrs));
    log_write(buf);
    brelse(buf);
}

// 在内存 inode 表 (itable) 中查找编号为 inum 的 inode。
// 若已在表中: 递增 ip->ref 并返回（调用者获得新引用）。
// 若不在表中: 从空闲槽位分配新条目，设置 dev/inum，ref=1，valid=0。
// 此函数不解锁也不从磁盘加载数据——加载工作由 ilock() 负责。
// 返回时持有 itable.lock 已被释放。
//
// iget() 和 ilock() 分离的用意: 路径名查找 (namex) 可以提前获取离磁盘 I/O。
static struct inode *iget(uint dev, uint inum) {
    struct inode *ip;
    struct inode *empty_slot;

    acquire(&itable.lock);

    // 第一遍: 该 inode 是否已在表中？
    empty_slot = 0;
    // TODO 顺序扫描查询，低效
    for (ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++) {
        if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
            // 命中: 增加引用计数并返回
            ip->ref++;
            release(&itable.lock);
            return ip;
        }
        if (empty_slot == 0 && ip->ref == 0)
            empty_slot = ip; // 顺便记住第一个空闲槽位，后续分配时使用
    }

    // 未命中: 从空闲槽位分配新条目
    if (empty_slot == 0)
        panic("iget: no inodes");

    ip = empty_slot;
    ip->dev = dev;
    ip->inum = inum;
    ip->ref = 1;
    ip->valid = 0; // 尚未从磁盘加载，由 ilock() 负责
    release(&itable.lock);

    return ip;
}

// 增加 ip 的引用计数 (idup = inode dup)。
// 直接返回 ip 自身，支持 ip = idup(old_ip) 的链式赋值。
// 典型场景: namex() 中，如果路径不是以 '/' 开头，用此函数复制 cwd 引用。
struct inode *idup(struct inode *ip) {
    acquire(&itable.lock);
    ip->ref++;
    release(&itable.lock);
    return ip;
}

// 锁定 inode，首次加载时从磁盘 dinode 读取元数据填充内存 inode。
// 调用者必须已经通过 iget() 或 idup() 持有引用 (ref >= 1)。
// 如果 valid==0（从未加载过），则 bread() 对应 dinode 并复制字段到 ip。
// 加载完成后 valid=1，后续 ilock/iunlock 对不会重复加载。
void ilock(struct inode *ip) {
    struct buf *buf;
    struct dinode *disk_inode;

    if (ip == 0 || ip->ref < 1)
        panic("ilock");

    acquiresleep(&ip->lock);

    // valid==0 表示: 虽然 iget() 已经在 itable 中建立了条目，
    // 但元数据（type/size/nlink/addrs）尚未从磁盘加载。
    if (ip->valid == 0) {
        // 从磁盘中读取/修改某个 dinode 数据的固定流程
        buf = bread(ip->dev, IBLOCK(ip->inum, sb));
        disk_inode = (struct dinode *)buf->data + ip->inum % IPB;
        ip->type = disk_inode->type;
        ip->major = disk_inode->major;
        ip->minor = disk_inode->minor;
        ip->nlink = disk_inode->nlink;
        ip->size = disk_inode->size;
        memmove(ip->addrs, disk_inode->addrs, sizeof(ip->addrs));
        brelse(buf);
        ip->valid = 1; // 标记为已加载，后续 ilock/iunlock 不再读磁盘

        if (ip->type == 0)
            panic("ilock: no type"); // 磁盘上的 dinode 空闲——逻辑错误
    }
}

// 解锁 inode。调用者必须持有 ip->lock。
void iunlock(struct inode *ip) {
    if (ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
        panic("iunlock");

    releasesleep(&ip->lock);
}

// 递减内存 inode 的引用计数 (iput = inode put)。
// 如果这是最后一次引用 (ref==1) 且磁盘上也没有目录项指向它 (nlink==0)，
// 则截断所有数据块 + 标记 type=0 释放磁盘 inode。
//
// iput() 必须在事务 (begin_op/end_op) 内调用，因为它可能触发磁盘释放。
//
// 锁的微妙之处: 当 ref==1 时，只有当前进程持有 ip 的引用，不可能有其他进程
// 持有 ip->lock，因此 acquiresleep() 不会阻塞或死锁。获取 ip->lock 后释放
// itable.lock，这样 itrunc() 和 iupdate() 中的磁盘 I/O 不会在持自旋锁时发生。
void iput(struct inode *ip) {
    acquire(&itable.lock);

    if (ip->ref == 1 && ip->valid && ip->nlink == 0) {
        // 既无内存引用 (ref==1) 也无磁盘链接 (nlink==0): 彻底释放
        // ref==1 保证当前是唯一持有者——可以安全获取 ip->lock
        acquiresleep(&ip->lock);
        release(&itable.lock); // 释放自旋锁，让 itrunc 内的磁盘操作不被锁住

        itrunc(ip);    // 释放所有数据块
        ip->type = 0;  // 标记磁盘 inode 为空闲
        iupdate(ip);   // 写回磁盘
        ip->valid = 0; // 标记内存条目中的磁盘数据已失效

        releasesleep(&ip->lock);
        acquire(&itable.lock); // 重新获取自旋锁，继续操作 itable
    }

    ip->ref--;
    release(&itable.lock);
}

// 解锁 + 释放引用 的便捷组合。
void iunlockput(struct inode *ip) {
    iunlock(ip);
    iput(ip);
}

// ---- Inode 内容 (inode content) ----
//
// 每个 inode 的数据存储为磁盘上的块，通过 ip->addrs[] 索引:
//   addrs[0..10]   — NDIRECT 个直接块地址
//   addrs[11]      — 一级间接块 (indirect): 内含 NINDIRECT 个块地址
//   addrs[12]      — 二级间接块 (doubly-indirect): 内含 NINDIRECT 个
//                     指向一级间接块的地址，共 NDOUBLY_INDIRECT 个数据块

// 返回 inode ip 中第 block_idx 个逻辑块的磁盘物理块号 (bmap = block map)
// 若该块尚未分配，则自动调用 balloc() 分配。
//
// inode 的数据块索引结构 (ip->addrs[]):
//   addrs[0..10]   — NDIRECT 个直接块: addrs[i] 直接存储物理块号
//   addrs[11]      — 一级间接块:   指向一个存有 NINDIRECT 个块号的磁盘块
//   addrs[12]      — 二级间接块:   指向一个存有 NINDIRECT 个一级间接块号的磁盘块，
//                                   每个一级间接块再存 NINDIRECT 个数据块号，
//                                   共 NDOUBLY_INDIRECT 个数据块
static uint bmap(struct inode *ip, uint block_idx) {
    uint disk_block;   // 最终返回的磁盘物理块号
    uint *block_table; // 指向间接块数据区（解释为 uint 数组，每个元素是一个块号）
    struct buf *buf;   // 缓冲区块指针

    // — 直接块 (direct): addrs[0..NDIRECT-1] —
    if (block_idx < NDIRECT) {
        disk_block = ip->addrs[block_idx];
        if (disk_block == 0) {
            disk_block = balloc(ip->dev);
            ip->addrs[block_idx] = disk_block;
        }
        return disk_block;
    }
    block_idx -= NDIRECT;

    // — 一级间接块 (single indirect): addrs[NDIRECT] → 块号数组 —
    if (block_idx < NINDIRECT) {
        disk_block = ip->addrs[NDIRECT];
        if (disk_block == 0) {
            disk_block = balloc(ip->dev);
            ip->addrs[NDIRECT] = disk_block;
        }
        buf = bread(ip->dev, disk_block);
        block_table = (uint *)buf->data;
        disk_block = block_table[block_idx];
        if (disk_block == 0) {
            disk_block = balloc(ip->dev);
            block_table[block_idx] = disk_block;
            log_write(buf);
        }
        brelse(buf);
        return disk_block;
    }
    block_idx -= NINDIRECT;

    // — 二级间接块 (doubly-indirect): addrs[NDIRECT+1] → 块号数组 → 块号数组 —
    if (block_idx < NDOUBLY_INDIRECT) {
        uint outer_idx = block_idx / NINDIRECT;
        uint inner_idx = block_idx % NINDIRECT;

        // 第一步：确保二级间接块本身已分配
        disk_block = ip->addrs[NDIRECT + 1];
        if (disk_block == 0) {
            disk_block = balloc(ip->dev);
            ip->addrs[NDIRECT + 1] = disk_block;
        }

        // 第二步：在二级间接块中查找/分配一级间接块
        buf = bread(ip->dev, disk_block);
        block_table = (uint *)buf->data;
        disk_block = block_table[outer_idx];
        if (disk_block == 0) {
            disk_block = balloc(ip->dev);
            block_table[outer_idx] = disk_block;
            log_write(buf);
        }
        brelse(buf);

        // 第三步：在一级间接块中查找/分配最终的数据块
        buf = bread(ip->dev, disk_block);
        block_table = (uint *)buf->data;
        disk_block = block_table[inner_idx];
        if (disk_block == 0) {
            disk_block = balloc(ip->dev);
            block_table[inner_idx] = disk_block;
            log_write(buf);
        }
        brelse(buf);
        return disk_block;
    }

    panic("bmap: out of range");
}

// 截断 inode (itrunc = inode truncate): 释放 inode 的所有数据块，将 size 置零。
// 按 直接块 → 一级间接块 → 二级间接块 的顺序逐级释放:
//   - 直接块:   直接 bfree() addrs[] 中的每一个非零项。
//   - 一级间接块: bread() 间接块 → 遍历它所含的 NINDIRECT 个块号 → bfree() 每个非零项
//                 → bfree() 间接块自身。
//   - 二级间接块: bread() 二级间接块 → 对每个一级间接块号:
//                   bread() 一级间接块 → 遍历所含 NINDIRECT 个块号 → bfree()
//                   → bfree() 一级间接块自身
//                 → bfree() 二级间接块自身。
// 最后 ip->size=0 并 iupdate() 写回磁盘。
//
// 调用者必须持有 ip->lock。此函数不要求事务——因为仅在 iput() 中 nlink==0
// 时调用，此时该 inode 对外不可达，不会有并发修改。
void itrunc(struct inode *ip) {
    uint *block_table;
    struct buf *buf;

    // — 直接块: 释放 addrs[0..NDIRECT-1] 中的每个非零块 —
    for (int di = 0; di < NDIRECT; di++) {
        if (ip->addrs[di]) {
            bfree(ip->dev, ip->addrs[di]);
            ip->addrs[di] = 0;
        }
    }

    // — 一级间接块: addrs[NDIRECT] → 释放所含数据块 + 自身 —
    if (ip->addrs[NDIRECT]) {
        buf = bread(ip->dev, ip->addrs[NDIRECT]);
        block_table = (uint *)buf->data;
        // 释放间接块中每个条目指向的数据块
        for (int entry = 0; entry < NINDIRECT; entry++) {
            if (block_table[entry])
                bfree(ip->dev, block_table[entry]);
        }
        brelse(buf);
        // 释放间接块自身
        bfree(ip->dev, ip->addrs[NDIRECT]);
        ip->addrs[NDIRECT] = 0;
    }

    // — 二级间接块: addrs[NDIRECT+1] → 两层释放 —
    if (ip->addrs[NDIRECT + 1]) {
        buf = bread(ip->dev, ip->addrs[NDIRECT + 1]);
        block_table = (uint *)buf->data;
        for (int outer = 0; outer < NINDIRECT; outer++) {
            if (block_table[outer] == 0)
                continue;

            // 对每个一级间接块: 释放所含数据块 + 自身
            struct buf *inner_buf = bread(ip->dev, block_table[outer]);
            uint *inner_table = (uint *)inner_buf->data;
            for (int inner = 0; inner < NINDIRECT; inner++) {
                if (inner_table[inner])
                    bfree(ip->dev, inner_table[inner]);
            }
            brelse(inner_buf);
            bfree(ip->dev, block_table[outer]);
        }
        brelse(buf);
        // 释放二级间接块自身
        bfree(ip->dev, ip->addrs[NDIRECT + 1]);
        ip->addrs[NDIRECT + 1] = 0;
    }

    ip->size = 0;
    iupdate(ip);
}

// 将 inode 元数据复制到 stat 结构体 (stati = stat inode)
// 调用者必须持有 ip->lock
void stati(struct inode *ip, struct stat *st) {
    st->dev = ip->dev;
    st->ino = ip->inum;
    st->type = ip->type;
    st->nlink = ip->nlink;
    st->size = ip->size;
}

// 从 inode 读取数据 (readi = read inode)。
// 调用者必须持有 ip->lock。
//
// user_dst==1: dst 是用户态虚拟地址（通过页表翻译写入）
// user_dst==0: dst 是内核地址（直接 memmove）
//
// 执行流程: 把 [off, off+n) 这段文件区间拆成一个个磁盘块，
// 逐块用 bread() 读到缓冲区，再从缓冲区拷贝到 dst。
int readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n) {
    uint already = 0; // 已经成功拷贝的字节数
    uint chunk;       // 本轮需要从当前块拷贝的字节数
    struct buf *buf;

    // off 超出文件末尾，没有数据可读
    if (off > ip->size || off + n < off)
        return 0;
    // 如果要读到超过文件末尾，截断到文件大小
    if (off + n > ip->size)
        n = ip->size - off;

    while (already < n) {
        // 1. 当前文件偏移 off 落在哪个磁盘块的哪个位置？
        uint block_no = off / BSIZE;     // 逻辑块号
        uint pos_in_block = off % BSIZE; // 该块内的字节偏移

        // 2. bmap() 映射逻辑块号→物理块号（必要时分配），bread() 读入缓冲区
        buf = bread(ip->dev, bmap(ip, block_no));

        // 3. 本轮能拷多少字节？两个上限取较小者：
        //    - 还需要读的字节数:  n - already
        //    - 当前块剩余空间:    BSIZE - pos_in_block
        chunk = min(n - already, BSIZE - pos_in_block);

        // 4. 从 buf->data[pos_in_block] 拷贝 chunk 字节到 dst
        if (either_copyout(user_dst, dst, buf->data + pos_in_block, chunk) == -1) {
            brelse(buf);
            return -1;
        }
        brelse(buf);

        // 5. 推进：已读字节数、文件偏移、目标地址都向前走 chunk 字节
        already += chunk;
        off += chunk;
        dst += chunk;
    }
    return already;
}

// 向 inode 写入数据 (writei = write inode)。
// 调用者必须持有 ip->lock。
//
// user_src==1: src 是用户态虚拟地址（通过页表读取）
// user_src==0: src 是内核地址（直接 memmove）
//
// 执行流程: 把 [off, off+n) 这段文件区间拆成一个个磁盘块，
// 逐块用 bmap() 定位/分配 → bread() 读入缓冲区 →
// 从 src 拷贝数据到缓冲区 → log_write() 记录修改。
// 最后更新 inode 的文件大小和元数据。
int writei(struct inode *ip, int user_src, uint64 src, uint off, uint n) {
    uint already = 0; // 已经成功写入的字节数
    uint chunk;       // 本轮需要向当前块拷贝的字节数
    struct buf *buf;

    // off 超出文件末尾，或 n 溢出回绕
    if (off > ip->size || off + n < off)
        return -1;
    // 超出文件系统支持的最大文件大小
    if (off + n > MAXFILE * BSIZE)
        return -1;

    while (already < n) {
        // 1. 当前文件偏移 off 落在哪个磁盘块的哪个位置？
        uint block_no = off / BSIZE;     // 逻辑块号
        uint pos_in_block = off % BSIZE; // 该块内的字节偏移

        // 2. bmap() 映射逻辑块号→物理块号（必要时分配），bread() 读入缓冲区
        buf = bread(ip->dev, bmap(ip, block_no));

        // 3. 本轮能写多少字节？两个上限取较小者：
        //    - 还需要写的字节数:  n - already
        //    - 当前块剩余空间:    BSIZE - pos_in_block
        chunk = min(n - already, BSIZE - pos_in_block);

        // 4. 从 src 拷贝 chunk 字节到 buf->data[pos_in_block]
        if (either_copyin(buf->data + pos_in_block, user_src, src, chunk) == -1) {
            brelse(buf);
            break; // 拷贝失败，提前退出（已写入的 already 字节仍然有效）
        }
        log_write(buf); // 通过日志记录修改，而非直接 bwrite()
        brelse(buf);

        // 5. 推进：已写字节数、文件偏移、源地址都向前走 chunk 字节
        already += chunk;
        off += chunk;
        src += chunk;
    }

    // 若写入位置超出了原文件末尾，扩展文件大小
    if (off > ip->size)
        ip->size = off;

    // 即使 size 未变也必须写回 inode——bmap() 可能在循环中新增了间接块
    iupdate(ip);

    return already;
}

// ---- 目录 (directories) ----
//
// 目录是一个内容为 `struct dirent` 数组的特殊 inode（type==T_DIR）。
// 每个 dirent 包含一个文件名 (name) 和一个 inode 编号 (inum)，实现
// "文件名 → inode" 的映射。inum==0 表示该条目空闲。

// 比较两个文件名，最多比较 DIRSIZ 个字符。
int namecmp(const char *s, const char *t) {
    return strncmp(s, t, DIRSIZ);
}

// 在目录 dir 中查找名为 name 的目录项 (dirlookup = directory lookup)。
// 若找到则可用 *out_offset 记录该目录项在目录文件中的字节偏移，
// 并返回对应的内存 inode（通过 iget() 获取，已引用 ref+1 但未锁定）。
// 若 out_offset 为 0 则不记录偏移（dirlink 用于检测重名时不需要偏移）。
// 若未找到则返回 0。
struct inode *dirlookup(struct inode *dir, char *name, uint *out_offset) {
    if (dir->type != T_DIR)
        panic("dirlookup not DIR");

    uint entry_size = sizeof(struct dirent);
    struct dirent entry;

    for (uint offset = 0; offset < dir->size; offset += entry_size) {
        if (readi(dir, 0, (uint64)&entry, offset, entry_size) != entry_size)
            panic("dirlookup read");
        if (entry.inum == 0)
            continue;
        if (namecmp(name, entry.name) == 0) {
            if (out_offset)
                *out_offset = offset;
            uint inode_no = entry.inum;
            return iget(dir->dev, inode_no);
        }
    }
    return 0;
}

// 在目录 dir 中添加新目录项 (dirlink = directory link)。
// 将 (name, inode_no) 写入目录中的一个空闲 dirent（inum==0 的槽位）。
//
// 两步:
//   1. 先调用 dirlookup 检查重名——若存在则 iput 释放引用并返回 -1。
//   2. 扫描目录找到空闲 dirent，填入 name 和 inode_no 后 writei 写回。
int dirlink(struct inode *dir, char *name, uint inode_no) {
    uint entry_size = sizeof(struct dirent);
    struct dirent entry;
    uint offset;

    // 第一步: 检查重名
    struct inode *existing = dirlookup(dir, name, 0);
    if (existing != 0) {
        iput(existing);
        return -1;
    }

    // 第二步: 扫描目录找到第一个空闲 dirent (inum==0)
    // TODO 顺序扫描效率很低，可以加一个 next_dirent_offset 游标，避免每次都从头扫描
    for (offset = 0; offset < dir->size; offset += entry_size) {
        if (readi(dir, 0, (uint64)&entry, offset, entry_size) != entry_size)
            panic("dirlink read");
        if (entry.inum == 0)
            break;
    }

    // 填充目录项并写回。offset==dir->size 时 writei 触发 bmap 分配新块
    strncpy(entry.name, name, DIRSIZ);
    entry.inum = inode_no;
    if (writei(dir, 0, (uint64)&entry, offset, entry_size) != entry_size)
        panic("dirlink");

    return 0;
}

// ---- 路径 (paths) ----
//
// 路径解析的核心函数是 namex()，它逐级跟随目录项将路径字符串转换为 inode。
// namex 依赖 skipelem() 从路径字符串中提取每一级文件名。

// 从路径 path 中提取下一级路径元素 (skipelem = skip element)。
// 将提取出的文件名复制到 name（最多 DIRSIZ 字符），返回剩余路径的指针。
//
// 约定:
//   - 返回 "" (空字符串) 表示 path 只包含最后一级，无剩余路径。
//   - 返回 0 (NULL) 表示 path 已无更多元素可提取。
//   - 连续的 '/' 被压缩为单个分隔符，前导和尾随 '/' 被跳过。
//
// 示例:
//   skipelem("a/bb/c", name)   → 返回 "bb/c", name = "a"
//   skipelem("///a//bb", name) → 返回 "bb",   name = "a"
//   skipelem("a", name)        → 返回 "",     name = "a"
//   skipelem("", name)         → 返回 0
//   skipelem("////", name)     → 返回 0
//
static char *skipelem(char *path, char *name) {
    // 跳过前导 '/'
    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;

    // 扫描到文件名末尾（下一个 '/' 或 '\0'）
    char *name_start = path;
    while (*path != '/' && *path != 0)
        path++;
    int name_len = path - name_start;

    // 复制文件名到 name 缓冲区，截断到 DIRSIZ
    if (name_len >= DIRSIZ)
        memmove(name, name_start, DIRSIZ);
    else {
        memmove(name, name_start, name_len);
        name[name_len] = 0;
    }

    // 跳过文件名后面的 '/'
    while (*path == '/')
        path++;
    return path;
}

// 路径名解析 (namex = name resolve)。
// 逐级跟随目录项将路径字符串 path 转换为 inode。
//
// 参数:
//   return_parent==0: 返回最终一级 inode（用于 open/stat 等）。
//   return_parent==1: 提前一级停止，返回父目录 inode，最终文件名写入 name
//                     （用于 create/unlink 等需要修改目录内容的调用）。
//
// 调用者必须在事务 (begin_op/end_op) 内——此函数可能调用 iput()。
// 锁约定: 逐级 ilock → 处理 → iunlockput，不同时持有两级目录的锁。
static struct inode *namex(char *path, int return_parent, char *name) {
    struct inode *current;
    struct inode *child;

    // 确定起始目录
    if (*path == '/')
        current = iget(ROOTDEV, ROOTINO); // 绝对路径 → 根 inode (ref+1, 未锁定)
    else
        current = idup(myproc()->cwd); // 相对路径 → cwd (ref+1)

    while ((path = skipelem(path, name)) != 0) {
        // name 为当前处理的目录/文件名，path 为剩余的路径
        ilock(current);
        if (current->type != T_DIR) {
            iunlockput(current);
            return 0;
        }

        int is_last = (*path == '\0');
        if (return_parent && is_last) {
            iunlock(current); // 返回父目录（已解锁，仍有引用）
            return current;
        }

        // 在当前目录中查找 name 对应的 inode
        child = dirlookup(current, name, 0);
        if (child == 0) {
            iunlockput(current);
            return 0;
        }
        iunlockput(current);
        current = child;
    }

    // path 已耗尽；return_parent 模式但无父目录部分则返回 0
    if (return_parent) {
        iput(current);
        return 0;
    }
    return current;
}

// 返回路径 path 对应的 inode (namei = name to inode)。
struct inode *namei(char *path) {
    char name[DIRSIZ];
    return namex(path, 0, name);
}

// 返回路径 path 的父目录 inode，最终文件名写入 name (nameiparent)。
struct inode *nameiparent(char *path, char *name) {
    return namex(path, 1, name);
}

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// xv6 日志系统 (log system) — 简单物理重做日志 (physical re-do log)。
//
// 一个日志事务 (transaction) 可以合并多个并发 FS 系统调用的更新。日志系统只在
// 没有活跃 FS 系统调用时才提交 (commit)。这样设计的好处是：永远不需要
// 考虑"一次提交是否会写入尚未完成的系统调用的更新"——因为提交时刻一定
// 没有未完成的调用。
//
// 使用方式: 每个 FS 系统调用必须用 begin_op() / end_op() 包裹:
//   begin_op()  — 通常只是递增活跃计数 (outstanding) 并返回；
//                  若日志空间接近耗尽，则睡眠等待最后的 end_op() 提交。
//   end_op()    — 递减计数，若降为 0 则触发提交 (commit)。
//
// 磁盘上的日志布局 (on-disk log format):
//   日志头块 (header)  → 包含块号列表: [block A#, block B#, block C#, ...]
//   数据块 A
//   数据块 B
//   数据块 C
//   ...
//
// 日志追加 (append) 是同步的: 先将修改过的缓存块写入日志区，再写日志头。
// 崩溃恢复 (recovery): 若日志头标记了已提交的事务，则把日志区数据块
// 重放到各自的数据区位置，然后清空日志头。

// 日志头 (log header): 磁盘上的日志头块和内存中的日志头共用此结构体。
// n      — 本次事务涉及的磁盘块数量
// block[] — 被修改的块号列表，最多 LOGSIZE 个
struct logheader {
    int n;
    int block[LOGSIZE];
};

// 日志全局状态 (log)。
// 整个系统只有一份 log 实例，用 log.lock 自旋锁保护。
struct log {
    struct spinlock lock;
    int start;       // 日志区在磁盘上的起始块号（由超级块指定）
    int size;        // 日志区总块数（nlog）
    int outstanding; // 正在执行的 FS 系统调用数量（begin_op - end_op）
    int committing;  // 是否有线程正在执行 commit()（非 0 表示正在提交，其他线程应等待）
    int dev;         // 日志所在的设备号
    struct logheader lh;
};
struct log log;

static void recover_from_log(void);
static void commit();

// 初始化日志系统: 验证日志头大小、设置锁、从超级块获取日志区位置、
// 然后调用 recover_from_log() 执行崩溃恢复（重放未完成的已提交事务）。
void initlog(int dev, struct superblock *sb) {
    if (sizeof(struct logheader) >= BSIZE)
        panic("initlog: too big logheader");
    // 日志头区域被设置为一个 block 以内
    initlock(&log.lock, "log");
    log.start = sb->logstart; // 日志区起始块号
    log.size = sb->nlog;      // 日志区块数
    log.dev = dev;
    recover_from_log(); // 启动时恢复——重放上次可能未完成的已提交事务
}

// 将已提交的日志头中的 block 写回磁盘 (install_trans = install transaction)。
// 遍历内存中 log.lh.block[] 记录的每个块号:
//   lbuf = bread(日志区的对应块)   — 源: 磁盘日志区中的块
//   dbuf = bread(数据区的目标块)   — 目标: 磁盘数据区中的块
//   memmove(dbuf, lbuf) → bwrite(dbuf) → 将日志内容写回数据区
// recovering==1 时不调用 bunpin（恢复过程中不需要 pin 管理）
// recovering==0 时（正常提交路径），释放之前在 log_write() 中 pin 的引用。
static void install_trans(int recovering) {
    int tail;

    for (tail = 0; tail < log.lh.n; tail++) {
        struct buf *lbuf = bread(log.dev, log.start + tail + 1); // 源: 日志区中的块
        // 取出日志头中记录的目标块号，从 buffer cache 中读取数据区的目标块
        struct buf *dbuf = bread(log.dev, log.lh.block[tail]); // 目标: 磁盘数据区
        memmove(dbuf->data, lbuf->data, BSIZE);                // 将日志数据拷贝到目标
        bwrite(dbuf);                                          // 将目标写回磁盘
        if (recovering == 0)
            bunpin(dbuf); // 正常提交路径: 释放 log_write() 加的 pin 引用
        brelse(lbuf);
        brelse(dbuf);
    }
}

// 从磁盘日志头块读取到内存 log.lh (read_head = read log header)。
static void read_head(void) {
    struct buf *buf = bread(log.dev, log.start);

    // 取出大小固定为 logheader 的日志头块内容，保存到内存 log.lh 中
    // 约定buf->data[]的前 sizeof(struct logheader) 字节存放日志头信息，后续是日志数据块
    struct logheader *lh = (struct logheader *)(buf->data);
    int i;
    log.lh.n = lh->n;
    for (i = 0; i < log.lh.n; i++) {
        log.lh.block[i] = lh->block[i];
    }
    // 读完之后，手动释放buffer块
    brelse(buf);
}

// 将内存中的 log.lh 写入磁盘日志头块 (write_head = write log header)。
// 这是事务的真正提交点 (commit point)，一旦日志头成功写入磁盘，
// 即使立刻断电，恢复时也能重放这些块。提交点之前的任何崩溃都将丢失整个事务。
static void write_head(void) {
    struct buf *buf = bread(log.dev, log.start);
    struct logheader *hb = (struct logheader *)(buf->data);
    int i;
    hb->n = log.lh.n;
    for (i = 0; i < log.lh.n; i++) {
        hb->block[i] = log.lh.block[i];
    }
    bwrite(buf);
    brelse(buf);
}

// 崩溃恢复 (recover from log): 系统启动时调用。
// 1. read_head(): 从磁盘读取日志头——若上次正常关机则 n==0，无事可做。
// 2. install_trans(1): 若 n>0 则重放已提交的事务（将日志区的块写回数据区）。
// 3. 清空日志头 (n=0 并写回磁盘)，标记日志区为空。
static void recover_from_log(void) {
    // 把磁盘上的日志头读到内存 log.lh 中，以便知道哪些块需要重放
    read_head();

    // 若日志头中记录了块号，重放到数据区
    // recovering=1 表示正在恢复过程中，不需要 bunpin，因为恢复时不涉及 log_write() 的 pin 管理
    install_trans(1);
    log.lh.n = 0;
    // 将空的日志头写回磁盘，标记日志区空闲
    write_head();
}

// 开始一个 FS 系统调用 (begin_op = begin operation)。
// 递增 log.outstanding 计数。若日志正在提交或空间不足，则睡眠等待。
//
// 空间估算: 每个 FS 系统调用最多写入 MAXOPBLOCKS 个块，因此预判
// log.lh.n + (outstanding+1)*MAXOPBLOCKS 是否会超过 LOGSIZE。
// 若会超限就睡眠，等最后一个 end_op() 提交后再唤醒。
void begin_op(void) {
    acquire(&log.lock);
    while (1) {
        if (log.committing) {
            // commit 时不能让新的 syscall 进来
            // 原子释放锁并睡眠
            sleep(&log, &log.lock);
        } else if (log.lh.n + (log.outstanding + 1) * MAXOPBLOCKS > LOGSIZE) {
            // 日志空间可能不够容纳本次调用的写入量，等待提交释放空间
            sleep(&log, &log.lock);
        } else {
            log.outstanding += 1;
            release(&log.lock);
            break;
        }
    }
}

// 退出当前事务，最后一个 FS 系统调用负责提交 (end_op = end operation)。
// 递减 log.outstanding；若降为 0 且当前事务非空，则触发提交 (commit)。
//
// 组提交 (group commit): 多个并发的 FS 系统调用可以合并在一个事务中。
// 最后一个完成的调用负责提交，前面的调用只需唤醒等待者。
// 注意: commit() 在释放 log.lock 的情况下执行（因为其中会睡眠等待 I/O）。
void end_op(void) {
    int do_commit = 0;

    acquire(&log.lock);
    log.outstanding -= 1;
    if (log.committing)
        panic("log.committing"); // 不应有其他线程同时提交
    if (log.outstanding == 0) {
        // 最后一个 FS 系统调用负责提交
        do_commit = 1;
        log.committing = 1; // 标记正在提交，阻止新的 begin_op 进入
    } else {
        // 还有其他系统调用活跃——不提交，仅唤醒可能在 begin_op() 中等待的线程
        // （outstanding 减少意味着预留空间少了一份，可能有线程可以继续）
        wakeup(&log);
    }
    // begin_op 看到 committing == 1，会 sleep，不会进入
    // 因此 commit 虽然不持有 log.lock，但逻辑上拥有日志系统的独占权
    release(&log.lock);

    if (do_commit) {
        // commit() 不能持有自旋锁，因为它会调用 sleep-worthy I/O 操作
        commit();
        acquire(&log.lock);
        log.committing = 0;
        wakeup(&log); // 唤醒等待 committing 或等待日志空间的线程
        release(&log.lock);
    }
}

// 将事务中修改过的缓存块写入磁盘日志区 (write_log)。
// 遍历内存 log.lh.block[]:
//   from = bread(数据区目标块) — 已在 bcache 中并被修改过
//   to   = bread(日志区对应块)  — 日志区中的位置
//   memmove(to, from) → bwrite(to) — 将修改内容拷贝到日志区并写回磁盘
// 此时日志区包含了完整的修改副本，但尚未标记为"已提交"（等 write_head）。
static void write_log(void) {
    int tail;

    for (tail = 0; tail < log.lh.n; tail++) {
        struct buf *to = bread(log.dev, log.start + tail + 1); // 日志区中的目标块
        struct buf *from = bread(log.dev, log.lh.block[tail]); // 数据区中已修改的块
        memmove(to->data, from->data, BSIZE);
        bwrite(to); // 写入日志区
        brelse(from);
        brelse(to);
    }
}

// 提交当前事务 (commit)。
// 三步协议 (write-ahead logging, WAL):
//   1. write_log():      将修改过的块写入磁盘日志区（预写，write-ahead）
//   2. write_head():     写入日志头 → **这是真正的提交点 (commit point)**
//   3. install_trans(0): 将日志区块重放到数据区（正常提交路径，recovering=0）
//   4. 清空日志头 (n=0, write_head): 标记事务完成，日志区空闲
//
// 崩溃一致性保证: 若在步骤 2 之前崩溃 → 日志头 n==0，恢复时无事发生。
// 若在步骤 2 之后崩溃 → 日志头有块号列表，恢复时重放步骤 3。
static void commit() {
    if (log.lh.n > 0) {
        write_log();      // 步骤 1: 将提交的块从 buffer cache 写入磁盘日志区
        write_head();     // 步骤 2: 写入日志头 — 提交点
        install_trans(0); // 步骤 3: 将日志中的 block 写回磁盘
        log.lh.n = 0;
        write_head(); // 步骤 4: 清空日志头
    }
}

// 记录一个被修改的缓冲区块，以便后续提交 (log_write = log a modified buffer)。
//
// 调用者使用 log_write() 替代 bwrite(): 它不立即写回磁盘，而是
// 用 bpin() 将块"钉"在缓存中防止被淘汰，并在日志头中记录块号。
// 实际的磁盘写入由 commit() 中的 write_log() 批量完成。
//
// 日志吸收 (log absorption): 如果同一事务中同一磁盘块被多次修改，
// 在 log.lh.block[] 中只占一个条目——后续修改复用同一位置。
//
// 典型调用模式:
//   bp = bread(...);
//   modify bp->data[];
//   log_write(bp);   // 替代 bwrite(bp): 记录块号并 pin
//   brelse(bp);
void log_write(struct buf *b) {
    int i;

    acquire(&log.lock);
    if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
        panic("too big a transaction");
    if (log.outstanding < 1)
        panic("log_write outside of transaction"); // 必须在 begin_op/end_op 之间调用

    int is_recorded = 0;
    // 日志吸收: 检查该块是否已在当前事务中被记录过
    for (i = 0; i < log.lh.n; i++) {
        if (log.lh.block[i] == b->blockno) {
            is_recorded = 1; // 已在日志中，复用同一槽位
            break;
        }
    }

    if (!is_recorded) {
        log.lh.block[log.lh.n] = b->blockno; // 记录新块号到日志头
        bpin(b);                             // pin: refcnt++，防止被 bcache 淘汰
        log.lh.n++;                          // 增加日志头中的块计数
    }

    release(&log.lock);
}

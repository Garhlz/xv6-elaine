#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Simple logging that allows concurrent FS system calls.
//
// A log transaction contains the updates of multiple FS system
// calls. The logging system only commits when there are
// no FS system calls active. Thus there is never
// any reasoning required about whether a commit might
// write an uncommitted system call's updates to disk.
//
// A system call should call begin_op()/end_op() to mark
// its start and end. Usually begin_op() just increments
// the count of in-progress FS system calls and returns.
// But if it thinks the log is close to running out, it
// sleeps until the last outstanding end_op() commits.
//
// The log is a physical re-do log containing disk blocks.
// The on-disk log format:
//   header block, containing block #s for block A, B, C, ...
//   block A
//   block B
//   block C
//   ...
// Log appends are synchronous.

// Contents of the header block, used for both the on-disk header block
// and to keep track in memory of logged block# before commit.
struct logheader
{
  int n;
  int block[LOGSIZE];
};

struct log
{
  struct spinlock lock;
  int start;
  int size;
  int outstanding; // how many FS sys calls are executing.
  int committing;  // in commit(), please wait.
  int dev;
  struct logheader lh;
};
struct log log;

static void recover_from_log(void);
static void commit();

void initlog(int dev, struct superblock *sb)
{
  if (sizeof(struct logheader) >= BSIZE)
    panic("initlog: too big logheader");

  initlock(&log.lock, "log");
  log.start = sb->logstart;
  log.size = sb->nlog;
  log.dev = dev;
  recover_from_log();
}

// Copy committed blocks from log to their home location
// 遍历 log.lh，把日志区域的每一个块，拷贝到磁盘的数据区
static void
install_trans(int recovering)
{
  int tail;

  for (tail = 0; tail < log.lh.n; tail++)
  {
    struct buf *lbuf = bread(log.dev, log.start + tail + 1); // read log block (src)，磁盘的日志缓冲区
    struct buf *dbuf = bread(log.dev, log.lh.block[tail]);   // read dst，磁盘的数据区
    memmove(dbuf->data, lbuf->data, BSIZE);                  // copy block to dst
    bwrite(dbuf);                                            // write dst to disk
    if (recovering == 0)
      bunpin(dbuf);
    brelse(lbuf);
    brelse(dbuf);
  }
}

// Read the log header from disk into the in-memory log header
static void
read_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *lh = (struct logheader *)(buf->data);
  int i;
  log.lh.n = lh->n;
  for (i = 0; i < log.lh.n; i++)
  {
    log.lh.block[i] = lh->block[i];
  }
  brelse(buf);
}

// Write in-memory log header to disk.
// This is the true point at which the
// current transaction commits.
// 将内存中的 log.lh（包含了块的数量 n 和所有块号）写入到磁盘的日志头块。
// 这是真正的“提交点”。一旦这一步完成，即使立刻断电，系统也能恢复
static void
write_head(void)
{
  struct buf *buf = bread(log.dev, log.start);
  struct logheader *hb = (struct logheader *)(buf->data);
  int i;
  hb->n = log.lh.n;
  for (i = 0; i < log.lh.n; i++)
  {
    hb->block[i] = log.lh.block[i];
  }
  bwrite(buf);
  brelse(buf);
}

static void
recover_from_log(void)
{
  read_head(); // 这里先从磁盘读取日志头部的信息。所以，只要在崩溃之前保存过磁盘头部（第一个write_head()）
  // 就可以成功重新执行在磁盘的日志缓冲区中的已经提交的写事务（也就是把它们写入磁盘数据区）
  install_trans(1); // if committed, copy from log to disk
  log.lh.n = 0;
  write_head(); // clear the log
}

// called at the start of each FS system call.
void begin_op(void)
{
  acquire(&log.lock);
  while (1)
  {
    if (log.committing) // 正在提交
    {
      sleep(&log, &log.lock);
    }
    // 日志空间是否充足
    else if (log.lh.n + (log.outstanding + 1) * MAXOPBLOCKS > LOGSIZE)
    {
      // this op might exhaust log space; wait for commit.
      sleep(&log, &log.lock);
    }
    else
    {
      log.outstanding += 1;
      // 在“正在进行的系统调用”计数器上加一

      release(&log.lock);
      break;
    }
  }
}

// called at the end of each FS system call.
// commits if this was the last outstanding operation.
void end_op(void)
{
  int do_commit = 0;

  acquire(&log.lock);
  log.outstanding -= 1;
  if (log.committing)
    panic("log.committing");
  if (log.outstanding == 0)
  {
    do_commit = 1;
    log.committing = 1;
  }
  else
  {
    // begin_op() may be waiting for log space,
    // and decrementing log.outstanding has decreased
    // the amount of reserved space.
    // 还有其他系统调用正在进行，什么也不做，组提交
    wakeup(&log);
  }
  release(&log.lock);

  if (do_commit)
  {
    // call commit w/o holding locks, since not allowed
    // to sleep with locks.
    commit();
    acquire(&log.lock);
    log.committing = 0;
    wakeup(&log);
    release(&log.lock);
  }
}

// Copy modified blocks from cache to log.
static void
write_log(void)
{
  int tail;
  // 遍历内存中的 log.lh，把每一个被标记的、已修改的 buf 从缓存区，拷贝并写入到磁盘的日志缓冲区
  for (tail = 0; tail < log.lh.n; tail++)
  {
    struct buf *to = bread(log.dev, log.start + tail + 1); // log block
    struct buf *from = bread(log.dev, log.lh.block[tail]); // cache block，blno存储在磁盘中的logheader中
    memmove(to->data, from->data, BSIZE);
    bwrite(to); // write the log
    brelse(from);
    brelse(to);
  }
}

static void
commit()
{
  if (log.lh.n > 0)
  {
    write_log();      // Write modified blocks from cache to log
    write_head();     // Write header to disk -- the real commit
    install_trans(0); // Now install writes to home locations
    log.lh.n = 0;
    write_head(); // Erase the transaction from the log
  }
}

// Caller has modified b->data and is done with the buffer.
// Record the block number and pin in the cache by increasing refcnt.
// commit()/write_log() will do the disk write.
//
// log_write() replaces bwrite(); a typical use is:
//   bp = bread(...)
//   modify bp->data[]
//   log_write(bp)
//   brelse(bp)
void log_write(struct buf *b)
{
  int i;

  acquire(&log.lock);
  if (log.lh.n >= LOGSIZE || log.lh.n >= log.size - 1)
    panic("too big a transaction");
  if (log.outstanding < 1)
    panic("log_write outside of trans");

  for (i = 0; i < log.lh.n; i++)
  {
    // 日志吸收，同一个事务中修改的两个文件在同一个磁盘块，只需要在日志中为这个块保留一个位置
    if (log.lh.block[i] == b->blockno) // log absorption
      break;
  }
  log.lh.block[i] = b->blockno; // 先修改了头部（内存中）
  if (i == log.lh.n)
  { // Add new block to log?
    bpin(b);
    // 增加缓冲块的引用计数 refcnt
    // 这样 brelse 就不会把它移到 LRU 链表的头部，从而防止缓冲区被意外回收
    log.lh.n++; // 记录的总块数量+1
  }
  release(&log.lock);
}

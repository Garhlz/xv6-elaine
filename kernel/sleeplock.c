// 睡眠锁 (sleep lock) 的实现。
//
// 睡眠锁用于保护可能长时间持有的临界区资源（如 inode 内容和磁盘缓冲区）。
// 当锁被其他进程持有时，当前进程会调用 sleep() 让出 CPU 而非空转等待。
//
// 锁状态转换:
//   locked==0, pid==0  → 空闲（free）
//   locked==1, pid!=0  → 被进程 pid 持有（held）
//
// 内部使用自旋锁 lk 保护 locked 和 pid 字段的原子读写。
// sleep() 和 wakeup() 配合实现了"等待-唤醒"机制（详见 proc.c）。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"

// 初始化睡眠锁。
// 1. 初始化内部自旋锁 lk（名称为 "sleep lock"，供调试）。
// 2. 设置名称、锁定状态=0、持有者 pid=0。
void initsleeplock(struct sleeplock *lk, char *name) {
    initlock(&lk->lk, "sleep lock");
    lk->name = name;
    lk->locked = 0;
    lk->pid = 0;
}

// 获取睡眠锁。
// 流程:
//   1. 获取内部自旋锁 lk（保护 locked/pid 字段）。
//   2. 若 locked==1，调用 sleep()：sleep 将当前进程记录在 lk 的等待队列中，
//      原子释放自旋锁 lk 并让出 CPU（通过 sched() 切换上下文）。
//      → 被 wakeup() 唤醒后，sleep() 返回，重新获取自旋锁 lk，再次检查 locked。
//   3. locked==0 时，标记为持有：locked=1，pid=当前进程 pid。
//   4. 释放内部自旋锁 lk。
//
// 注意：调用者**不需要**持有自旋锁调用此函数（仅需处于 push_off 环境）。
// 这里 acquire(&lk->lk) 会触发 push_off，但 sleep() 会把自旋锁释放掉。
void acquiresleep(struct sleeplock *lk) {
    acquire(&lk->lk);
    while (lk->locked) {
        // 睡眠，等待锁持有者释放并唤醒我们。
        // sleep(chan, lk) 的参数含义:
        //   - chan = lk：睡眠在这个锁对象上（wakeup(lk) 会唤醒我们）。
        //   - lk = &lk->lk：睡眠前释放的自旋锁指针，唤醒后此锁会被 sleep() 重新获取。
        sleep(lk, &lk->lk);
    }
    lk->locked = 1;
    lk->pid = myproc()->pid; // 记录持有者 pid（用于 holdingsleep 检查）
    release(&lk->lk);
}

// 释放睡眠锁。
// 1. 获取内部自旋锁 lk。
// 2. 标记为未持有：locked=0, pid=0。
// 3. 调用 wakeup(lk) 唤醒所有正在 sleep(lk, ...) 上等待的进程。
//    （多个进程可能同时等待同一把锁，它们会被唤醒并重新竞争。）
// 4. 释放内部自旋锁 lk。
void releasesleep(struct sleeplock *lk) {
    acquire(&lk->lk);
    lk->locked = 0;
    lk->pid = 0;
    wakeup(lk); // 唤醒等待该锁的所有进程
    release(&lk->lk);
}

// 检查当前进程是否持有睡眠锁 lk。
// 在内部自旋锁的保护下读取 locked 和 pid 字段。
int holdingsleep(struct sleeplock *lk) {
    int r;

    acquire(&lk->lk);
    r = lk->locked && (lk->pid == myproc()->pid);
    release(&lk->lk);
    return r;
}

#ifndef XV6_SLEEPLOCK_H
#define XV6_SLEEPLOCK_H

#include "types.h"
#include "spinlock.h"

// 睡眠锁 (sleep lock) — 可长时间持有的锁。
//
// 与自旋锁 `struct spinlock` 的关键区别:
//   - 自旋锁: 获取失败时忙等待（空循环），持锁期间必须关中断，适合 ≤ 几微秒
//     的临界区（如更新链表指针、修改 refcnt）。
//   - 睡眠锁: 获取失败时调用 sleep() 让出 CPU，允许其他进程运行。适合可能
//     阻塞的操作（如磁盘 I/O），或可能持续数毫秒的临界区（如读写 inode 内容）。
//
// 内部机制:
//   睡眠锁内部有一把自旋锁 lk，用于保护 locked 和 pid 字段的原子访问。
//   获取流程：先用自旋锁锁住内部字段 → 检查 locked → 若 busy 则 sleep()
//   （原子释放自旋锁并让出 CPU）→ 被 wakeup() 唤醒后重新获取自旋锁 → 继续检查。
//   这个模式来自 xv6 的 "Sleep and wakeup" 机制（proc.c 中的 sleep/wakeup）。
struct sleeplock {
    uint locked;        // 锁是否被持有？
    struct spinlock lk; // 保护 locked / pid 的内部自旋锁——任何读写这两个字段
                        // 都必须先持有 lk

    // 调试字段:
    char *name; // 锁名称（如 "inode", "buffer"），用于调试 panic 信息
    int pid;    // 持有该锁的进程 pid
};

#endif

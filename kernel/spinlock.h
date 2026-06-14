#ifndef XV6_SPINLOCK_H
#define XV6_SPINLOCK_H

#include "types.h"

// 互斥自旋锁 (mutual exclusion spinlock)。
// 自旋锁是 xv6 中最底层的同步原语——当锁被占用时，获取者会不断循环（自旋）
// 直到锁被释放，因此只适合保护极短的临界区（critical section）。
//
// 与睡眠锁 `struct sleeplock` 的区别:
//   - 自旋锁: 忙等待，持锁期间必须关闭中断，用于保护内核数据结构。
//   - 睡眠锁: 持锁等待时让出 CPU（调用 sleep()），可长时间持有，用于保护
//     文件 inode、缓冲区等。
//
// 锁的层级 (lock hierarchy):
//   自旋锁必须在内核代码"知道自己在做什么"时获取——持锁期间必须关闭中断
//   (push_off / pop_off)，且严禁阻塞（如调用 sleep()）。获取多把自旋锁时
//   必须遵循一致的顺序避免死锁。
struct spinlock {
    uint locked; // 锁是否已被持有？（0 = 空闲，1 = 被持有）

    // 调试和统计字段:
    char *name;      // 锁的名称（如 "bcache_3", "kmem_cpu_0"），用于调试输出
    struct cpu *cpu; // 当前持有锁的 CPU（用于检测重复获取/释放）
    int nts;         // 因为锁已被占用而自旋（test-and-set）的总次数，锁竞争指标
    int n;           // acquire() 调用总次数
};

#endif

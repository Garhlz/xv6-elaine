// 互斥自旋锁 (mutual exclusion spin lock) 的实现。
//
// 自旋锁是 xv6 内核中最底层的并发同步原语。它通过忙等待（busy-waiting /
// spinning）实现互斥：如果锁已被占用，获取者会在循环中反复检查直到锁可用。
//
// 关键设计要点:
//   1. 关闭中断 (push_off/pop_off): 持自旋锁期间必须关闭中断。如果持锁时
//      发生中断，而中断处理程序也需要同一把锁，就会死锁。
//   2. 嵌套计数 (noff): push_off 和 pop_off 使用嵌套计数器而不是简单的
//      on/off 开关，这样一个函数可以安全地关闭中断并调用另一个也需要关闭
//      中断的函数。
//   3. 内存屏障 (__sync_synchronize): 在 acquire 和 release 中使用，
//      防止编译器和 CPU 对内存访问乱序，确保临界区内的读写不会泄漏到锁外。
//   4. 锁统计 (nlocks, nts): 用于评估锁竞争（lock contention）程度。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

// — 锁统计子系统 (lock statistics) —
// 所有已初始化的自旋锁指针存入 locks[] 数组，用于 statslock() 输出统计信息。
// lock_locks 是保护 locks[] 数组本身的自旋锁。
#define NLOCK 500
static struct spinlock *locks[NLOCK];
static struct spinlock lock_locks;

// 从 locks[] 注册表中删除 lk（释放锁引用）。
// 在 free 某个数据结构之前调用，避免 stats 输出野指针。
void freelock(struct spinlock *lk) {
    acquire(&lock_locks);
    for (int i = 0; i < NLOCK; i++) {
        if (locks[i] == lk) {
            locks[i] = 0;
            break;
        }
    }
    release(&lock_locks);
}

// 在 locks[] 中找一个空槽位，将 lk 注册进去。
// 如果 NLOCK 不够用则 panic——系统中的自旋锁数量应预先估算。
static void findslot(struct spinlock *lk) {
    acquire(&lock_locks);
    for (int i = 0; i < NLOCK; i++) {
        if (locks[i] == 0) {
            locks[i] = lk;
            release(&lock_locks);
            return;
        }
    }
    release(&lock_locks);
    panic("findslot");
}

// 初始化自旋锁。
// name 用于调试输出（如 "itable", "kmem_cpu_0"）；locked/cpu/nts/n 设为初始值；
// 最后将锁注册到全局统计数组 locks[] 中。
void initlock(struct spinlock *lk, char *name) {
    lk->name = name;
    lk->locked = 0;
    lk->cpu = 0;
    lk->nts = 0; // test-and-set 次数（锁竞争指标）
    lk->n = 0;   // acquire() 调用总次数
    findslot(lk);
}

// 获取自旋锁 (acquire)。
// 流程:
//   1. push_off(): 关闭中断（嵌套计数），防止相同 CPU 上的中断处理程序
//      尝试获取同一把锁导致死锁。
//   2. 检查是否重复获取 (holding) — 同一 CPU 不能再次获取已持有的锁。
//   3. 基于 __sync_lock_test_and_set 的自旋循环:
//      - 递增 n（acquire 调用次数）。
//      - test_and_set: 原子地将 lk->locked 设为 1 并返回旧值。
//        旧值 == 0 → 成功获取，退出循环。
//        旧值 != 0 → 锁被其他 CPU 持有，递增 nts（竞争计数）并继续循环。
//   4. __sync_synchronize(): 内存屏障——确保进入临界区前所有"看到锁被释放"
//      的读操作已完成，防止 CPU 乱序执行导致临界区内的访存提前。
//   5. 记录持有锁的 CPU，用于 holding() 检查和 panic 时的调试信息。
void acquire(struct spinlock *lk) {
    push_off(); // 关闭中断（嵌套），防止同一 CPU 的死锁
    if (holding(lk))
        panic("acquire"); // 不允许重复获取同一把自旋锁

    // test-and-set 自旋循环:
    // - lk->n: acquire 总次数
    // - lk->nts: 因为锁正被占用而自旋的迭代次数（越高说明竞争越激烈）
    __sync_fetch_and_add(&lk->n, 1);
    while (__sync_lock_test_and_set(&lk->locked, 1) != 0) {
        __sync_fetch_and_add(&lk->nts, 1); // 每一轮自旋都计数
    }

    // 内存屏障：告诉编译器和 CPU——"到此为止的所有内存访问之后，
    // 临界区内的代码才被执行"。和 release 中的 barrier 配对使用。
    __sync_synchronize();

    lk->cpu = mycpu(); // 记录持有锁的 CPU
}

// 释放自旋锁 (release)。
// 流程:
//   1. holding() 检查：确保调用者是锁的持有者。
//   2. 清除 cpu 记录。
//   3. __sync_synchronize(): 内存屏障——确保临界区内所有写入在释放锁之前
//      对其他 CPU 可见。
//   4. __sync_lock_release: 原子地将 lk->locked 写为 0。
//   5. pop_off(): 恢复之前的中断状态。
void release(struct spinlock *lk) {
    if (!holding(lk))
        panic("release");

    lk->cpu = 0;

    // 内存屏障：临界区内的写入必须先于锁的释放。
    // 和 acquire 中的 barrier 配对，组成完整的内存顺序约束。
    __sync_synchronize();
    // 原子释放：写 0 等效于 __sync_lock_release
    __sync_lock_release(&lk->locked);

    pop_off(); // 恢复中断状态
}

// 检查当前 CPU 是否持有锁 lk。
// 判断条件：锁已被标记为 locked 且记录的 cpu 正是当前 CPU。
int holding(struct spinlock *lk) {
    int r;
    r = (lk->locked && lk->cpu == mycpu());
    return r;
}

// 关闭当前 CPU 的中断 (push_off = push interrupt off)。
// 使用嵌套计数器 noff 而不是简单的标志位:
//   - 第一次关闭时保存原来的中断状态到 intena（用于 pop_off 恢复）。
//   - 每次 push_off 递增 noff，每次 pop_off 递减。
//   - 只有当 noff 降为 0 且原来中断是开启的，才真正重新开启中断。
// 这种设计允许任意深度的嵌套调用，每个 push 和 pop 配对。
void push_off(void) {
    int old = intr_get(); // 读取当前中断状态 (sstatus.SIE)

    intr_off(); // 关闭中断 (csrci sstatus, 2)
    if (mycpu()->noff == 0)
        mycpu()->intena = old; // 最外层 push 时保存原始中断状态
    mycpu()->noff += 1;
}

// 恢复当前 CPU 的中断状态 (pop_off = pop interrupt off)。
// 递减嵌套计数 noff；当降至 0 且原始状态是中断开启时，重新打开中断。
void pop_off(void) {
    struct cpu *c = mycpu();

    if (intr_get())
        panic("pop_off - interruptible"); // 此时中断必须是关闭的
    if (c->noff < 1)
        panic("pop_off"); // push/pop 不配对
    c->noff -= 1;
    if (c->noff == 0 && c->intena)
        intr_on(); // 恢复中断 (csrsi sstatus, 2)
}

// 无锁原子读取 8 字节 (lock-free read, 8 bytes)。
// 使用 GCC 内建 __atomic_load 实现，内存顺序为顺序一致性 (SEQ_CST)。
// 用于 lock lab 的统计代码等需要从其他 CPU 安全读取的场景。
uint64 lockfree_read8(uint64 *addr) {
    uint64 val;
    __atomic_load(addr, &val, __ATOMIC_SEQ_CST);
    return val;
}

// 无锁原子读取 4 字节 (lock-free read, 4 bytes)。
int lockfree_read4(int *addr) {
    int val;
    __atomic_load(addr, &val, __ATOMIC_SEQ_CST);
    return val;
}

// 格式化单把锁的统计信息到 buf。
// 输出格式: "lock: <name>: #test-and-set <nts> #acquire() <n>"
static int snprint_lock(char *buf, int sz, struct spinlock *lk) {
    int n = 0;
    if (lk->n > 0) {
        n = snprintf(buf, sz, "lock: %s: #test-and-set %d #acquire() %d\n", lk->name, lk->nts,
                     lk->n);
    }
    return n;
}

// 输出锁竞争统计 (stats lock)。
// 输出内容:
//   - 所有 kmem/bcache 相关锁的统计（这些是 lock lab 关注的核心锁）。
//   - 竞争最激烈的前 5 把锁（按 nts 降序），展示全局竞争热点。
//   - tot 项：kmem/bcache 锁的 test-and-set 总次数。
// 该函数用于 locktest 用户在 xv6 shell 中查看锁竞争情况。
int statslock(char *buf, int sz) {
    int n;
    int tot = 0;

    n = snprintf(buf, sz, "--- lock kmem/bcache stats\n");
    acquire(&lock_locks);
    for (int i = 0; i < NLOCK; i++) {
        if (locks[i] == 0)
            continue;
        if (strncmp(locks[i]->name, "bcache", strlen("bcache")) == 0 ||
            strncmp(locks[i]->name, "kmem", strlen("kmem")) == 0) {
            tot += locks[i]->nts;
            n += snprint_lock(buf + n, sz - n, locks[i]);
        }
    }

    // 找出竞争最激烈的前 5 把锁（所有名称前缀的锁，不仅仅是 kmem/bcache）
    n += snprintf(buf + n, sz - n, "--- top 5 contended locks:\n");
    int last = 100000000;
    for (int t = 0; t < 5; t++) {
        int top = -1;
        for (int i = 0; i < NLOCK; i++) {
            if (locks[i] == 0)
                continue;
            // 选 nts 最大的锁，但必须小于上一轮选出的（保证不重复）
            if (locks[i]->nts < last && (top < 0 || locks[i]->nts > locks[top]->nts))
                top = i;
        }
        if (top < 0)
            break;
        n += snprint_lock(buf + n, sz - n, locks[top]);
        last = locks[top]->nts;
    }
    release(&lock_locks);
    n += snprintf(buf + n, sz - n, "tot= %d\n", tot);
    return n;
}

// Mutual exclusion spin locks.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

#define NLOCK 500

static struct spinlock *locks[NLOCK];
static struct spinlock lock_locks;

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

void initlock(struct spinlock *lk, char *name) {
    lk->name = name;
    lk->locked = 0;
    lk->cpu = 0;
    lk->nts = 0;
    lk->n = 0;
    findslot(lk);
}

void acquire(struct spinlock *lk) {
    push_off();
    if (holding(lk))
        panic("acquire");

    __sync_fetch_and_add(&lk->n, 1);
    while (__sync_lock_test_and_set(&lk->locked, 1) != 0) {
        __sync_fetch_and_add(&lk->nts, 1);
    }

    __sync_synchronize();
    lk->cpu = mycpu();
}

void release(struct spinlock *lk) {
    if (!holding(lk))
        panic("release");

    lk->cpu = 0;
    __sync_synchronize();
    __sync_lock_release(&lk->locked);
    pop_off();
}

int holding(struct spinlock *lk) {
    int r;

    r = (lk->locked && lk->cpu == mycpu());
    return r;
}

void push_off(void) {
    int old = intr_get();

    intr_off();
    if (mycpu()->noff == 0)
        mycpu()->intena = old;
    mycpu()->noff += 1;
}

void pop_off(void) {
    struct cpu *c = mycpu();

    if (intr_get())
        panic("pop_off - interruptible");
    if (c->noff < 1)
        panic("pop_off");
    c->noff -= 1;
    if (c->noff == 0 && c->intena)
        intr_on();
}

uint64 lockfree_read8(uint64 *addr) {
    uint64 val;

    __atomic_load(addr, &val, __ATOMIC_SEQ_CST);
    return val;
}

int lockfree_read4(int *addr) {
    int val;

    __atomic_load(addr, &val, __ATOMIC_SEQ_CST);
    return val;
}

static int snprint_lock(char *buf, int sz, struct spinlock *lk) {
    int n = 0;

    if (lk->n > 0) {
        n = snprintf(buf, sz, "lock: %s: #test-and-set %d #acquire() %d\n", lk->name, lk->nts,
                     lk->n);
    }
    return n;
}

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

    n += snprintf(buf + n, sz - n, "--- top 5 contended locks:\n");
    int last = 100000000;
    for (int t = 0; t < 5; t++) {
        int top = -1;
        for (int i = 0; i < NLOCK; i++) {
            if (locks[i] == 0)
                continue;
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

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

uint64 sys_exit(void) {
    int n;
    if (argint(0, &n) < 0)
        return -1;
    exit(n);
    return 0; // not reached
}

uint64 sys_getpid(void) {
    return myproc()->pid;
}

uint64 sys_fork(void) {
    return fork();
}

uint64 sys_wait(void) {
    uint64 p;
    if (argaddr(0, &p) < 0)
        return -1;
    return wait(p);
}

uint64 sys_sbrk(void) {
    int addr;
    int n;

    if (argint(0, &n) < 0)
        return -1;
    addr = myproc()->sz;
    if (growproc(n) < 0)
        return -1;
    return addr;
}

uint64 sys_sleep(void) {
    int n;
    uint ticks0;

    if (argint(0, &n) < 0)
        return -1;
    acquire(&tickslock);
    ticks0 = ticks;
    while (ticks - ticks0 < n) {
        if (myproc()->killed) {
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

uint64 sys_kill(void) {
    int pid;

    if (argint(0, &pid) < 0)
        return -1;
    return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
    return xticks;
}

// Set the trace mask for the current process.
uint64 sys_trace(void) {
    int n;

    if (argint(0, &n) < 0)
        return -1;
    myproc()->tracemask = n;
    return 0;
}

// Return system info (free memory, number of processes).
uint64 sys_sysinfo(void) {
    uint64 user_addr;
    if (argaddr(0, &user_addr) < 0) {
        return -1;
    }
    struct sysinfo info;
    uint64 freemem = count_freemem();
    uint64 nproc = count_nproc();
    info.freemem = freemem;
    info.nproc = nproc;
    if (copyout(myproc()->pagetable, user_addr, (char *)&info, sizeof(info)) < 0) {
        return -1;
    }
    return 0;
}

// Return the access bits for the given range of user pages.
int sys_pgaccess(void) {
    uint64 start_addr;
    if (argaddr(0, &start_addr) < 0)
        return -1;

    int num_pages;
    if (argint(1, &num_pages) < 0)
        return -1;

    uint64 user_buf_addr;
    if (argaddr(2, &user_buf_addr) < 0)
        return -1;

    uint mask = 0;
    for (int i = 0; i < num_pages; i++) {
        uint64 current_va = start_addr + i * PGSIZE;
        pte_t *pte = walk(myproc()->pagetable, current_va, 0);
        if ((pte == 0) || ((*pte & PTE_V) == 0))
            return -1;

        if (*pte & PTE_A)
            mask |= (1 << i);

        *pte &= (~PTE_A);
    }
    copyout(myproc()->pagetable, user_buf_addr, (char *)&mask, sizeof(mask));

    return 0;
}

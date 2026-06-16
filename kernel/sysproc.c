#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"

// exit(n): 结束当前进程，返回状态码 n。
// 不会返回（调用者 exit() 中调用 panic("zombie exit")）。
uint64 sys_exit(void) {
    int n;
    if (argint(0, &n) < 0)
        return -1;
    exit(n);
    return 0; // not reached
}

// getpid(): 返回当前进程的 pid。
uint64 sys_getpid(void) {
    return myproc()->pid;
}

// fork(): 创建当前进程的副本（子进程）。
// 子进程返回 0，父进程返回子进程 pid。
uint64 sys_fork(void) {
    return fork();
}

// wait(&status): 等待子进程退出，将退出状态写入 status 指向的地址。
// 如果没有子进程立即返回 -1。
uint64 sys_wait(void) {
    uint64 p;
    if (argaddr(0, &p) < 0)
        return -1;
    return wait(p);
}

// sbrk(n): 将进程内存大小增长 n 字节。
// n>0 分配内存，n<0 释放内存，n==0 只返回当前大小。
// 返回旧内存边界地址（即增长前的 sz）。
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

// sleep(n): 让当前进程休眠 n 个时钟节拍（ticks）。
// 休眠期间进程进入 SLEEPING 状态，由调度器切换到其他进程。
// 如果进程在休眠期间被 kill，则提前返回 -1。
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

// kill(pid): 向 pid 进程发送终止信号。
// 设置目标进程的 killed=1，目标进程将在下一次陷阱返回时退出。
uint64 sys_kill(void) {
    int pid;

    if (argint(0, &pid) < 0)
        return -1;
    return kill(pid);
}

// uptime(): 返回系统启动以来经历的时钟节拍（ticks）总数。
uint64 sys_uptime(void) {
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
    return xticks;
}

// trace(mask): 为当前进程设置跟踪掩码。
// 掩码中置 1 的位对应的 syscall 编号在执行时会被内核打印，方便调试。
uint64 sys_trace(void) {
    int n;

    if (argint(0, &n) < 0)
        return -1;
    myproc()->tracemask = (uint64)n;
    return 0;
}

// sysinfo(&info): 收集系统信息（空闲内存量、活跃进程数），
// 通过 copyout 写回用户态的 struct sysinfo。
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

// pgaccess(base, num_pages, &mask): 扫描指定范围内的用户页表项，
// 收集 PTE_A（访问位），写入掩码 mask，然后清空 PTE_A 以便下次检测。
// 最多支持 32 个页面（掩码 32 位）。
uint64 sys_pgaccess(void) {
    uint64 start_addr;
    if (argaddr(0, &start_addr) < 0)
        return -1;

    int num_pages;
    if (argint(1, &num_pages) < 0)
        return -1;
    if (num_pages < 0 || num_pages > 32)
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
    if (copyout(myproc()->pagetable, user_buf_addr, (char *)&mask, sizeof(mask)) < 0)
        return -1;

    return 0;
}

// sigalarm(interval, handler): 设置周期性闹钟（alarm）。
// 每隔 interval 个时钟节拍，内核向用户进程发送一次 SIGALRM，
// 用户态 handler 函数会被调用。interval==0 禁用闹钟。
uint64 sys_sigalarm(void) {
    int interval;
    uint64 handler_addr;
    if (argint(0, &interval) < 0)
        return -1;
    if (argaddr(1, &handler_addr) < 0)
        return -1;
    acquire(&myproc()->lock);
    myproc()->alarm_interval = interval;
    myproc()->alarm_handler = handler_addr;
    myproc()->alarm_ticks_left = interval;
    release(&myproc()->lock);
    return 0;
}

// sigreturn(): 从 SIGALRM 处理函数返回。
// 恢复被 alarm 中断前保存的 trapframe 备份，恢复正常执行。
uint64 sys_sigreturn(void) {
    struct proc *p = myproc();
    acquire(&p->lock);
    *(p->trapframe) = p->alarm_trapframe_backup;
    p->in_alarm = 0;
    release(&p->lock);
    return p->trapframe->a0;
}

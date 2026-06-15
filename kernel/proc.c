#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "file.h"
#include "fcntl.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // 来自 trampoline.S
static void mmap_close(struct proc *p);
static int mmap_writeback(struct proc *p, struct vma *vma, uint64 start, uint64 end);

// 确保对 wait() 中父进程的唤醒不会丢失，
// 同时保证访问 p->parent 时的内存序正确。
// 必须在获取任何 p->lock 之前获取此锁。
struct spinlock wait_lock;

// 为每个进程分配一页内核栈，
// 映射到内存高端，其后紧接一页非法 guard page 作为溢出保护。
void proc_mapstacks(pagetable_t kpgtbl) {
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        char *pa = kalloc();
        if (pa == 0)
            panic("kalloc");
        uint64 va = KSTACK((int)(p - proc));
        kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
    }
}

// 启动时初始化进程表。
void procinit(void) {
    struct proc *p;

    initlock(&pid_lock, "nextpid");
    initlock(&wait_lock, "wait_lock");
    for (p = proc; p < &proc[NPROC]; p++) {
        initlock(&p->lock, "proc");
        p->kstack = KSTACK((int)(p - proc));
    }
}

// 必须在关中断时调用，避免与进程迁移到其他 CPU 产生竞争。
int cpuid() {
    int id = r_tp();
    return id;
}

// 返回当前 CPU 对应的 cpu 结构体。
// 调用前必须关中断。
struct cpu *mycpu(void) {
    int id = cpuid();
    struct cpu *c = &cpus[id];
    return c;
}

// 返回当前进程的 proc 结构体指针，无则返回 0。
struct proc *myproc(void) {
    push_off();
    struct cpu *c = mycpu();
    struct proc *p = c->proc;
    pop_off();
    return p;
}

int allocpid() {
    int pid;

    acquire(&pid_lock);
    pid = nextpid;
    nextpid = nextpid + 1;
    release(&pid_lock);

    return pid;
}

// 在进程表中查找一个 UNUSED 的槽位。
// 找到则初始化为可在内核中运行的状态，返回时持有 p->lock。
// 若无空闲进程或内存分配失败，返回 0。
static struct proc *allocproc(void) {
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->state == UNUSED) {
            goto found;
        } else {
            release(&p->lock);
        }
    }
    return 0;

found:
    p->pid = allocpid();
    p->state = USED;
    p->tracemask = 0;
    p->alarm_interval = 0;
    p->alarm_handler = 0;
    p->alarm_ticks_left = 0;
    memset(&p->alarm_trapframe_backup, 0, sizeof(p->alarm_trapframe_backup));
    p->in_alarm = 0;
    p->mmap_top = MMAPBASE;
    memset(p->vmas, 0, sizeof(p->vmas));

    // 分配一页 trapframe。
    if ((p->trapframe = (struct trapframe *)kalloc()) == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // 分配 USYSCALL 页，用于快速获取 pid。
    if ((p->usyscall_page = (struct usyscall *)kalloc()) == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
    }
    p->usyscall_page->pid = p->pid;

    // 创建空的用户页表。
    p->pagetable = proc_pagetable(p);
    if (p->pagetable == 0) {
        freeproc(p);
        release(&p->lock);
        return 0;
    }

    // 设置上下文，使进程首次被调度时从 forkret 开始执行，
    // forkret 最终返回用户空间。
    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64)forkret;
    p->context.sp = p->kstack + PGSIZE;

    return p;
}

// 释放 proc 结构体及其挂载的数据，包括用户页。
// 调用者必须持有 p->lock。
static void freeproc(struct proc *p) {
    if (p->trapframe)
        kfree((void *)p->trapframe);
    p->trapframe = 0;
    if (p->usyscall_page)
        kfree((void *)p->usyscall_page);
    p->usyscall_page = 0;
    if (p->pagetable)
        proc_freepagetable(p->pagetable, p->sz);
    p->pagetable = 0;
    p->sz = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->chan = 0;
    p->killed = 0;
    p->xstate = 0;
    p->tracemask = 0;
    p->alarm_interval = 0;
    p->alarm_handler = 0;
    p->alarm_ticks_left = 0;
    memset(&p->alarm_trapframe_backup, 0, sizeof(p->alarm_trapframe_backup));
    p->in_alarm = 0;
    p->mmap_top = MMAPBASE;
    memset(p->vmas, 0, sizeof(p->vmas));
    p->state = UNUSED;
}

// 为指定进程创建用户页表，初始不含用户内存，仅含 trampoline 页面。
pagetable_t proc_pagetable(struct proc *p) {
    pagetable_t pagetable;

    // 创建空页表。
    pagetable = uvmcreate();
    if (pagetable == 0)
        return 0;

    // 将 trampoline 代码映射到用户态最高虚拟地址，
    // 仅供内核在进出用户空间时使用，不加 PTE_U。
    if (mappages(pagetable, TRAMPOLINE, PGSIZE, (uint64)trampoline, PTE_R | PTE_X) < 0) {
        uvmfree(pagetable, 0);
        return 0;
    }

    // 将 trapframe 映射到 TRAMPOLINE 的正下方，供 trampoline.S 使用。
    if (mappages(pagetable, TRAPFRAME, PGSIZE, (uint64)(p->trapframe), PTE_R | PTE_W) < 0) {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    // 将 USYSCALL 页映射到 TRAPFRAME 正下方。
    if (mappages(pagetable, USYSCALL, PGSIZE, (uint64)(p->usyscall_page), PTE_R | PTE_U) < 0) {
        uvmunmap(pagetable, TRAMPOLINE, 1, 0);
        uvmunmap(pagetable, TRAPFRAME, 1, 0);
        uvmfree(pagetable, 0);
        return 0;
    }

    return pagetable;
}

// 释放进程页表及其引用的物理内存。
void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmunmap(pagetable, TRAPFRAME, 1, 0);
    uvmunmap(pagetable, USYSCALL, 1, 0);
    uvmfree(pagetable, sz);
}

// 第一个用户程序，功能等价于 exec("/init")。
// 用 od -t xC initcode 可查看原始字节。
uchar initcode[] = {0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02, 0x97, 0x05, 0x00, 0x00, 0x93,
                    0x85, 0x35, 0x02, 0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00, 0x93, 0x08,
                    0x20, 0x00, 0x73, 0x00, 0x00, 0x00, 0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e,
                    0x69, 0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// 设置并启动第一个用户进程。
void userinit(void) {
    struct proc *p;

    p = allocproc();
    initproc = p;

    // 分配一页用户内存，将 init 的指令和数据拷贝进去。
    uvminit(p->pagetable, initcode, sizeof(initcode));
    p->sz = PGSIZE;

    // 为首次从内核"返回"用户空间做准备。
    p->trapframe->epc = 0;     // 用户程序计数器
    p->trapframe->sp = PGSIZE; // 用户栈指针

    safestrcpy(p->name, "initcode", sizeof(p->name));
    p->cwd = namei("/");

    p->state = RUNNABLE;

    release(&p->lock);
}

// 按 n 字节增长或缩减用户内存。
// 成功返回 0，失败返回 -1。
int growproc(int n) {
    uint sz;
    struct proc *p = myproc();

    sz = p->sz;
    if (n > 0) {
        if (sz + n >= MMAPBASE)
            return -1;
        if ((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
            return -1;
        }
    } else if (n < 0) {
        sz = uvmdealloc(p->pagetable, sz, sz + n);
    }
    p->sz = sz;
    return 0;
}

// 创建新进程，复制父进程状态。
// 子进程内核栈将在 fork() 系统调用中"返回"。
int fork(void) {
    int i, pid;
    struct proc *child;
    struct proc *p = myproc();

    // 分配进程结构。
    if ((child = allocproc()) == 0) {
        return -1;
    }

    // 将父进程用户内存拷贝给子进程。
    if (uvmcopy(p->pagetable, child->pagetable, p->sz) < 0) {
        freeproc(child);
        release(&child->lock);
        return -1;
    }
    child->sz = p->sz;

    // 复制保存的用户寄存器。
    *(child->trapframe) = *(p->trapframe);

    // 让 fork 在子进程中返回 0。
    child->trapframe->a0 = 0;

    // 复制 trace 掩码。
    child->tracemask = p->tracemask;

    child->mmap_top = p->mmap_top;
    for (i = 0; i < NVMA; i++) {
        if (p->vmas[i].valid) {
            child->vmas[i] = p->vmas[i];
            child->vmas[i].file = filedup(p->vmas[i].file);
        }
    }

    // 增加已打开文件描述符的引用计数。
    for (i = 0; i < NOFILE; i++)
        if (p->ofile[i])
            child->ofile[i] = filedup(p->ofile[i]);
    child->cwd = idup(p->cwd);

    safestrcpy(child->name, p->name, sizeof(p->name));

    pid = child->pid;

    release(&child->lock);

    acquire(&wait_lock);
    child->parent = p;
    release(&wait_lock);

    acquire(&child->lock);
    child->state = RUNNABLE;
    release(&child->lock);

    return pid;
}

// 将进程 p 的子进程过继给 init。
// 调用者必须持有 wait_lock。
void reparent(struct proc *p) {
    struct proc *pp;

    for (pp = proc; pp < &proc[NPROC]; pp++) {
        if (pp->parent == p) {
            pp->parent = initproc;
            wakeup(initproc);
        }
    }
}

// 退出当前进程，不会返回。
// 已退出的进程保持 ZOMBIE 状态，直到父进程调用 wait()。
void exit(int status) {
    struct proc *p = myproc();

    if (p == initproc)
        panic("init exiting");

    mmap_cleanup(p, 1);
    mmap_close(p);

    // 关闭所有打开的文件。
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd]) {
            struct file *f = p->ofile[fd];
            fileclose(f);
            p->ofile[fd] = 0;
        }
    }

    begin_op();
    iput(p->cwd);
    end_op();
    p->cwd = 0;

    acquire(&wait_lock);

    // 把所有子进程过继给 init。
    reparent(p);

    // 父进程可能正在 wait() 中睡眠，唤醒它。
    wakeup(p->parent);

    acquire(&p->lock);

    p->xstate = status;
    p->state = ZOMBIE;

    release(&wait_lock);

    // 跳入调度器，不再返回。
    sched();
    panic("zombie exit");
}

// 等待任一子进程退出并返回其 pid。
// 若无子进程则返回 -1。
int wait(uint64 addr) {
    struct proc *child;
    int has_children, pid;
    struct proc *p = myproc();

    acquire(&wait_lock);

    for (;;) {
        // 扫描进程表，寻找已退出的子进程。
        has_children = 0;
        for (child = proc; child < &proc[NPROC]; child++) {
            if (child->parent == p) {
                // 确保子进程不在 exit() 或 swtch() 中途。
                acquire(&child->lock);

                has_children = 1;
                if (child->state == ZOMBIE) {
                    // 找到了一个。
                    pid = child->pid;
                    if (addr != 0 && copyout(p->pagetable, addr, (char *)&child->xstate,
                                             sizeof(child->xstate)) < 0) {
                        release(&child->lock);
                        release(&wait_lock);
                        return -1;
                    }
                    freeproc(child);
                    release(&child->lock);
                    release(&wait_lock);
                    return pid;
                }
                release(&child->lock);
            }
        }

        // 没有子进程则无需继续等待。
        if (!has_children || p->killed) {
            release(&wait_lock);
            return -1;
        }

        // 等待子进程退出。
        sleep(p, &wait_lock); // DOC: wait-sleep
    }
}

// 每个 CPU 的进程调度器。
// 各 CPU 初始化完毕后各自调用 scheduler()。
// 调度器永不返回，循环执行：
//  - 选择一个进程运行；
//  - 通过 swtch 切换到该进程；
//  - 进程最终通过 swtch 回到调度器，交出控制权。
void scheduler(void) {
    struct proc *p;
    struct cpu *c = mycpu();

    c->proc = 0;
    for (;;) {
        // 打开中断，让设备有机会发出中断，避免死锁。
        intr_on();

        for (p = proc; p < &proc[NPROC]; p++) {
            acquire(&p->lock);
            if (p->state == RUNNABLE) {
                // 切换到选定进程。进程在跳回调度器之前，
                // 会自己释放并重新获取 p->lock。
                p->state = RUNNING;
                c->proc = p;
                swtch(&c->context, &p->context);

                // 进程本轮执行结束。
                // 它回到这里时应该已经更新了 p->state。
                c->proc = 0;
            }
            release(&p->lock);
        }
    }
}

// 切换到调度器。必须只持有 p->lock，且已修改 proc->state。
// 之所以保存并恢复 intena，是因为 intena 属于当前内核线程
// 而非当前 CPU。理想情况应存为 proc->intena 和 proc->noff，
// 但这会在少数"有锁但无进程"的场景中出错。
void sched(void) {
    int intena;
    struct proc *p = myproc();

    if (!holding(&p->lock))
        panic("sched p->lock");
    if (mycpu()->noff != 1)
        panic("sched locks");
    if (p->state == RUNNING)
        panic("sched running");
    if (intr_get())
        panic("sched interruptible");

    intena = mycpu()->intena;
    swtch(&p->context, &mycpu()->context);
    mycpu()->intena = intena;
}

// 让出 CPU，进入下一轮调度。
void yield(void) {
    struct proc *p = myproc();
    acquire(&p->lock);
    p->state = RUNNABLE;
    sched();
    release(&p->lock);
}

// fork 出的子进程首次被 scheduler() 调度时，
// 会通过 swtch 跳到 forkret 执行。
void forkret(void) {
    static int first = 1;

    // 此时仍持有 scheduler 中的 p->lock。
    release(&myproc()->lock);

    if (first) {
        // 文件系统初始化必须在普通进程上下文中执行
        // （因为会调用 sleep），不能在 main() 中完成。
        first = 0;
        fsinit(ROOTDEV);
    }

    usertrapret();
}

// 原子地释放 lk 并在 chan 上睡眠，被唤醒后重新获取 lk。
void sleep(void *chan, struct spinlock *lock) {
    struct proc *p = myproc();

    // 必须先获取 p->lock 才能修改 p->state 并调用 sched。
    // 一旦持有 p->lock，就可以保证不会漏掉 wakeup
    // （wakeup 也会锁 p->lock），因此可以安全释放 lock。

    acquire(&p->lock); // DOC: sleeplock1
    release(lock);

    // 进入睡眠。
    p->chan = chan;
    p->state = SLEEPING;

    sched();

    // 清理。
    p->chan = 0;

    // 重新获取原来的锁。
    release(&p->lock);
    acquire(lock);
}

// 唤醒所有在 chan 上睡眠的进程。
// 调用时不得持有任何 p->lock。
void wakeup(void *chan) {
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        if (p != myproc()) {
            acquire(&p->lock);
            if (p->state == SLEEPING && p->chan == chan) {
                p->state = RUNNABLE;
            }
            release(&p->lock);
        }
    }
}

// 终止指定 pid 的进程。
// 目标进程在下次尝试返回用户空间时才会真正退出（见 trap.c 的 usertrap()）。
int kill(int pid) {
    struct proc *p;

    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->pid == pid) {
            p->killed = 1;
            if (p->state == SLEEPING) {
                // 将进程从 sleep() 中唤醒。
                p->state = RUNNABLE;
            }
            release(&p->lock);
            return 0;
        }
        release(&p->lock);
    }
    return -1;
}

// 根据 user_dst 标志，将数据拷贝到用户地址或内核地址。
// 成功返回 0，失败返回 -1。
int either_copyout(int user_dst, uint64 dst, void *src, uint64 len) {
    struct proc *p = myproc();
    if (user_dst) {
        return copyout(p->pagetable, dst, src, len);
    } else {
        memmove((char *)dst, src, len);
        return 0;
    }
}

// 根据 user_src 标志，从用户地址或内核地址拷贝数据。
// 成功返回 0，失败返回 -1。
int either_copyin(void *dst, int user_src, uint64 src, uint64 len) {
    struct proc *p = myproc();
    if (user_src) {
        return copyin(p->pagetable, dst, src, len);
    } else {
        memmove(dst, (char *)src, len);
        return 0;
    }
}

// 打印进程列表到控制台，用于调试。
// 用户在控制台键入 ^P 时触发。
// 不加锁，避免在已卡死的机器上进一步僵死。
void procdump(void) {
    static char *states[] = {[UNUSED] = "unused",
                             [SLEEPING] = "sleep ",
                             [RUNNABLE] = "runble",
                             [RUNNING] = "run   ",
                             [ZOMBIE] = "zombie"};
    struct proc *p;
    char *state;

    printf("\n");
    for (p = proc; p < &proc[NPROC]; p++) {
        if (p->state == UNUSED)
            continue;
        if (p->state >= 0 && p->state < NELEM(states) && states[p->state])
            state = states[p->state];
        else
            state = "???";
        printf("%d %s %s", p->pid, state, p->name);
        printf("\n");
    }
}

uint64 count_nproc(void) {
    int cnt = 0;
    struct proc *p;
    for (p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if (p->state != UNUSED)
            cnt++;
        release(&p->lock);
    }
    return cnt;
}

struct vma *find_vma(struct proc *p, uint64 va) {
    for (int i = 0; i < NVMA; i++) {
        struct vma *vma = &p->vmas[i];
        if (vma->valid && va >= vma->addr && va < vma->addr + vma->length)
            return vma;
    }
    return 0;
}

int mmap_fault(uint64 va, int write) {
    struct proc *p = myproc();
    uint64 page = PGROUNDDOWN(va);
    struct vma *vma = find_vma(p, va);
    char *mem;
    int perm = PTE_U;
    int n;

    if (vma == 0)
        return -1;
    if (write && (vma->prot & PROT_WRITE) == 0)
        return -1;
    if (!write && (vma->prot & PROT_READ) == 0)
        return -1;
    if (walkaddr(p->pagetable, page) != 0)
        return -1;

    if ((mem = kalloc()) == 0)
        return -1;
    memset(mem, 0, PGSIZE);

    ilock(vma->file->ip);
    n = readi(vma->file->ip, 0, (uint64)mem, vma->offset + page - vma->addr, PGSIZE);
    iunlock(vma->file->ip);
    if (n < 0) {
        kfree(mem);
        return -1;
    }

    if (vma->prot & PROT_READ)
        perm |= PTE_R;
    if (vma->prot & PROT_WRITE)
        perm |= PTE_R | PTE_W;
    if (vma->prot & PROT_EXEC)
        perm |= PTE_X;

    if (mappages(p->pagetable, page, PGSIZE, (uint64)mem, perm) < 0) {
        kfree(mem);
        return -1;
    }
    return 0;
}

int mmap_unmap(struct proc *p, uint64 addr, uint64 length) {
    uint64 start, end;
    struct vma *vma;
    uint64 vstart, vend;

    if (length == 0 || addr % PGSIZE)
        return -1;
    if (addr + length < addr)
        return -1;

    start = PGROUNDDOWN(addr);
    end = PGROUNDUP(addr + length);
    vma = find_vma(p, start);
    if (vma == 0 || end > vma->addr + vma->length)
        return -1;

    vstart = vma->addr;
    vend = vma->addr + vma->length;
    if (start > vstart && end < vend)
        return -1;

    if ((vma->flags & MAP_SHARED) && (vma->prot & PROT_WRITE) &&
        mmap_writeback(p, vma, start, end) < 0)
        return -1;

    uvmunmap_mmap(p->pagetable, start, (end - start) / PGSIZE, 1);

    if (start == vstart && end == vend) {
        fileclose(vma->file);
        memset(vma, 0, sizeof(*vma));
    } else if (start == vstart) {
        vma->offset += end - vstart;
        vma->addr = end;
        vma->length = vend - end;
    } else {
        vma->length = start - vstart;
    }

    return 0;
}

int mmap_cleanup(struct proc *p, int force) {
    int ret = 0;
    for (int i = 0; i < NVMA; i++) {
        struct vma *vma = &p->vmas[i];
        if (!vma->valid)
            continue;
        if (force) {
            // 尽力写回，然后无论如何都解除映射。
            if ((vma->flags & MAP_SHARED) && (vma->prot & PROT_WRITE))
                mmap_writeback(p, vma, vma->addr, vma->addr + vma->length);
            uvmunmap_mmap(p->pagetable, vma->addr, (vma->length + PGSIZE - 1) / PGSIZE, 1);
            fileclose(vma->file);
            memset(vma, 0, sizeof(*vma));
        } else {
            if (mmap_unmap(p, vma->addr, vma->length) < 0)
                ret = -1;
        }
    }
    return ret;
}

static void mmap_close(struct proc *p) {
    for (int i = 0; i < NVMA; i++) {
        if (p->vmas[i].valid) {
            fileclose(p->vmas[i].file);
            memset(&p->vmas[i], 0, sizeof(p->vmas[i]));
        }
    }
}

static int mmap_writeback(struct proc *p, struct vma *vma, uint64 start, uint64 end) {
    int max = ((MAXOPBLOCKS - 1 - 1 - 2) / 2) * BSIZE;

    for (uint64 va = start; va < end; va += PGSIZE) {
        pte_t *pte = walk(p->pagetable, va, 0);
        int total = 0;

        if (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0)
            continue;

        while (total < PGSIZE) {
            int n = PGSIZE - total;
            int r;
            if (n > max)
                n = max;

            begin_op();
            ilock(vma->file->ip);
            r = writei(vma->file->ip, 1, va + total, vma->offset + va - vma->addr + total, n);
            iunlock(vma->file->ip);
            end_op();

            if (r != n)
                return -1;
            total += n;
        }
    }
    return 0;
}

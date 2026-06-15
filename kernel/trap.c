#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[], userret[];

// 在 kernelvec.S 中定义，调用 kerneltrap()。
void kernelvec();

extern int devintr();

void trapinit(void) {
    initlock(&tickslock, "time");
}

// 配置内核态下的异常和 trap 入口。
void trapinithart(void) {
    w_stvec((uint64)kernelvec);
}

//
// 处理来自用户空间的中断、异常和系统调用。
// 由 trampoline.S 调用。
//
void usertrap(void) {
    int which_dev = 0;
    uint64 scause = r_scause();
    uint64 stval = r_stval();

    if ((r_sstatus() & SSTATUS_SPP) != 0)
        panic("usertrap: not from user mode");

    // 现在已进入内核，将中断和异常导向 kerneltrap()。
    w_stvec((uint64)kernelvec);

    struct proc *p = myproc();

    // 保存用户程序计数器。
    p->trapframe->epc = r_sepc();

    if (scause == 8) {
        // 系统调用

        if (lockfree_read4(&p->killed))
            exit(-1);

        // sepc 指向 ecall 指令，需 +4 使其指向下一条。
        p->trapframe->epc += 4;

        // 中断会修改 sstatus 等寄存器，用完这些寄存器再开中断。
        intr_on();

        syscall();
    } else if (scause == 13) {
        // 缺页异常（读），尝试 mmap 惰性装载。
        if (mmap_fault(stval, 0) < 0)
            p->killed = 1;
    } else if (scause == 15) {
        // 缺页异常（写）：可能是 COW、mmap 惰性装载或非法写入。
        uint64 fault_page = PGROUNDDOWN(stval);
        if (fault_page >= MAXVA) {
            p->killed = 1;
        } else {
            pte_t *pte = walk(p->pagetable, fault_page, 0);
            int page_not_mapped = (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0);
            if (page_not_mapped) {
                if (mmap_fault(stval, 1) < 0)
                    p->killed = 1;
            } else if (*pte & PTE_COW) {
                // COW 页面：引用计数为 1 则直接提权，否则复制。
                uint64 pa = PTE2PA(*pte);
                if (get_ref(pa) == 1) {
                    *pte &= ~PTE_COW;
                    *pte |= PTE_W;
                } else {
                    uint flags = PTE_FLAGS(*pte);
                    flags &= ~PTE_COW;
                    flags |= PTE_W;
                    char *mem = kalloc();
                    if (mem == 0) {
                        p->killed = 1;
                    } else {
                        memmove(mem, (char *)pa, PGSIZE);
                        *pte = PA2PTE((uint64)mem) | flags;
                        kfree((void *)pa);
                    }
                }
                sfence_vma();
            } else {
                if (mmap_fault(stval, 1) < 0)
                    p->killed = 1;
            }
        }
    } else if ((which_dev = devintr()) != 0) {
        // 设备中断，已处理。
    } else {
        printf("usertrap(): unexpected scause %p pid=%d\n", scause, p->pid);
        printf("            sepc=%p stval=%p\n", r_sepc(), stval);
        p->killed = 1;
    }

    if (lockfree_read4(&p->killed))
        exit(-1);

    // 如果是定时器中断，让出 CPU。
    if (which_dev == 2) {
        if (p->alarm_interval > 0) {
            if (p->alarm_ticks_left > 0)
                p->alarm_ticks_left--;
            if (p->alarm_ticks_left == 0 && p->in_alarm == 0) {
                p->in_alarm = 1;
                p->alarm_ticks_left = p->alarm_interval;
                p->alarm_trapframe_backup = *(p->trapframe);
                p->trapframe->epc = p->alarm_handler;
            }
        }
        yield();
    }

    usertrapret();
}

//
// 返回用户空间
//
void usertrapret(void) {
    struct proc *p = myproc();

    // 即将把 trap 目标从 kerneltrap() 切换为 usertrap()，
    // 先关中断，等回到用户空间后再由 usertrap() 打开。
    intr_off();

    // 让系统调用、中断和异常进入 trampoline.S。
    w_stvec(TRAMPOLINE + (uservec - trampoline));

    // 设置 trapframe 中 uservec 下次进入内核时需要的值。
    p->trapframe->kernel_satp = r_satp();         // 内核页表
    p->trapframe->kernel_sp = p->kstack + PGSIZE; // 进程内核栈
    p->trapframe->kernel_trap = (uint64)usertrap;
    p->trapframe->kernel_hartid = r_tp(); // cpuid() 所需的 hartid

    // 设置寄存器，供 trampoline.S 的 sret 返回用户空间使用。

    // 将 S 模式的特权级设回 User。
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP; // 清除 SPP 以切回用户模式
    x |= SSTATUS_SPIE; // 在用户模式下开启中断
    w_sstatus(x);

    // 将异常程序计数器指向保存的用户 pc。
    w_sepc(p->trapframe->epc);

    // 告诉 trampoline.S 应切换到哪个用户页表。
    uint64 satp = MAKE_SATP(p->pagetable);

    // 跳到内存顶端的 trampoline.S：切换用户页表、
    // 恢复用户寄存器，并通过 sret 进入用户模式。
    uint64 fn = TRAMPOLINE + (userret - trampoline);
    ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
}

// 内核代码产生的中断和异常通过 kernelvec 进入此处，
// 运行在当前的任意内核栈上。
void kerneltrap() {
    int which_dev = 0;
    uint64 sepc = r_sepc();
    uint64 sstatus = r_sstatus();
    uint64 scause = r_scause();

    if ((sstatus & SSTATUS_SPP) == 0)
        panic("kerneltrap: not from supervisor mode");
    if (intr_get() != 0)
        panic("kerneltrap: interrupts enabled");

    if ((which_dev = devintr()) == 0) {
        printf("scause %p\n", scause);
        printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
        panic("kerneltrap");
    }

    // 定时器中断时让出 CPU。
    if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
        yield();

    // yield() 可能已触发其他 trap，
    // 恢复 trap 相关寄存器，供 kernelvec.S 的 sret 指令使用。
    w_sepc(sepc);
    w_sstatus(sstatus);
}

void clockintr() {
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
}

// 检查并处理外部中断或软件中断。
// 返回 2 表示定时器中断，1 表示其他设备中断，0 表示未识别。
int devintr() {
    uint64 scause = r_scause();

    if ((scause & 0x8000000000000000L) && (scause & 0xff) == 9) {
        // 来自 PLIC 的 S 模式外部中断。

        // irq 表示哪个设备触发了中断。
        int irq = plic_claim();

        if (irq == UART0_IRQ) {
            uartintr();
        } else if (irq == VIRTIO0_IRQ) {
            virtio_disk_intr();
        } else if (irq == E1000_IRQ) {
            e1000_intr();
        } else if (irq) {
            printf("unexpected interrupt irq=%d\n", irq);
        }

        // PLIC 规定每个设备一次最多产生一个中断；
        // 通知 PLIC 该设备可以再次中断。
        if (irq)
            plic_complete(irq);

        return 1;
    } else if (scause == 0x8000000000000001L) {
        // 来自 M 模式定时器的软件中断，由 kernelvec.S 中的 timervec 转发。

        if (cpuid() == 0) {
            clockintr();
        }

        // 通过清除 sip 中的 SSIP 位来确认软件中断。
        w_sip(r_sip() & ~2);

        return 2;
    } else {
        return 0;
    }
}

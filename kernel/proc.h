#ifndef XV6_PROC_H
#define XV6_PROC_H

#include "types.h"
#include "param.h"
#include "riscv.h"
#include "spinlock.h"

// 内核上下文切换时保存的寄存器。
struct context {
    uint64 ra;
    uint64 sp;

    // 被调用者需保存的寄存器 (callee-saved)
    uint64 s0;
    uint64 s1;
    uint64 s2;
    uint64 s3;
    uint64 s4;
    uint64 s5;
    uint64 s6;
    uint64 s7;
    uint64 s8;
    uint64 s9;
    uint64 s10;
    uint64 s11;
};

// 每个 CPU 的私有状态。
struct cpu {
    struct proc *proc;      // 当前 CPU 上运行的进程，无则为空。
    struct context context; // swtch() 切到此处即进入 scheduler()。
    int noff;               // push_off() 的嵌套深度。
    int intena;             // push_off() 之前中断是否处于开启状态。
};

extern struct cpu cpus[NCPU];

// 为 trampoline.S 的 trap 处理代码准备的进程私有数据。
// 位于用户页表中 trampoline 页面的正下方，独占一页，
// 不额外映射到内核页表。
// sscratch 寄存器始终指向这里。
// trampoline.S 中的 uservec 先将用户寄存器保存到 trapframe，
// 然后用其中的 kernel_sp、kernel_hartid、kernel_satp 初始化寄存器，
// 最后跳转到 kernel_trap。
// usertrapret() 与 trampoline.S 的 userret 负责把 trapframe 的
// kernel_* 字段填好、从 trapframe 恢复用户寄存器、
// 切回用户页表并进入用户空间。
// trapframe 之所以要保存 s0-s11 等被调用者寄存器，是因为
// 通过 usertrapret() 返回用户空间的路径并不会经过
// 完整的内核调用栈。
struct trapframe {
    /*   0 */ uint64 kernel_satp;   // 内核页表
    /*   8 */ uint64 kernel_sp;     // 进程内核栈顶
    /*  16 */ uint64 kernel_trap;   // usertrap() 入口地址
    /*  24 */ uint64 epc;           // 保存的用户程序计数器 (epc)
    /*  32 */ uint64 kernel_hartid; // 保存的内核 tp (hartid)
    /*  40 */ uint64 ra;
    /*  48 */ uint64 sp;
    /*  56 */ uint64 gp;
    /*  64 */ uint64 tp;
    /*  72 */ uint64 t0;
    /*  80 */ uint64 t1;
    /*  88 */ uint64 t2;
    /*  96 */ uint64 s0;
    /* 104 */ uint64 s1;
    /* 112 */ uint64 a0;
    /* 120 */ uint64 a1;
    /* 128 */ uint64 a2;
    /* 136 */ uint64 a3;
    /* 144 */ uint64 a4;
    /* 152 */ uint64 a5;
    /* 160 */ uint64 a6;
    /* 168 */ uint64 a7;
    /* 176 */ uint64 s2;
    /* 184 */ uint64 s3;
    /* 192 */ uint64 s4;
    /* 200 */ uint64 s5;
    /* 208 */ uint64 s6;
    /* 216 */ uint64 s7;
    /* 224 */ uint64 s8;
    /* 232 */ uint64 s9;
    /* 240 */ uint64 s10;
    /* 248 */ uint64 s11;
    /* 256 */ uint64 t3;
    /* 264 */ uint64 t4;
    /* 272 */ uint64 t5;
    /* 280 */ uint64 t6;
};

enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

#define NVMA 16
#define MMAPBASE 0x40000000

struct vma {
    uint64 addr;
    uint64 length;
    uint64 offset;
    int prot;
    int flags;
    struct file *file;
    int valid;
};

// 每个进程的私有状态。
struct proc {
    struct spinlock lock;

    // 访问以下字段时必须持有 p->lock：
    enum procstate state; // 进程状态
    void *chan;           // 非零值表示正在 chan 上睡眠
    int killed;           // 非零表示已被终止
    int xstate;           // 退出状态，由父进程的 wait() 获取
    int pid;              // 进程 ID

    // 访问此字段时必须持有 wait_lock：
    struct proc *parent; // 父进程

    // 以下字段为进程私有，访问时无需持有 p->lock。
    uint64 kstack;                           // 内核栈虚拟地址
    uint64 sz;                               // 进程内存大小（字节）
    pagetable_t pagetable;                   // 用户页表
    struct trapframe *trapframe;             // trampoline.S 使用的数据页
    struct context context;                  // swtch() 切到此处以运行进程
    struct file *ofile[NOFILE];              // 打开的文件
    struct inode *cwd;                       // 当前工作目录
    char name[16];                           // 进程名（调试用）
    uint64 tracemask;                        // 系统调用跟踪掩码
    struct usyscall *usyscall_page;          // 快速获取 pid 的 USYSCALL 页
    int alarm_interval;                      // 定时器间隔（tick 数）
    uint64 alarm_handler;                    // 用户态信号处理函数地址
    int alarm_ticks_left;                    // 距离下次 alarm 还剩的 tick 数
    struct trapframe alarm_trapframe_backup; // alarm 触发时保存的寄存器快照
    int in_alarm;                            // 防止 alarm 重入
    struct vma vmas[NVMA];                   // 文件映射的惰性分配区
    uint64 mmap_top;                         // 下次 mmap 分配的起始地址
};

uint64 count_nproc(void);
struct vma *find_vma(struct proc *, uint64);
int mmap_fault(uint64, int);
int mmap_unmap(struct proc *, uint64, uint64);
int mmap_cleanup(struct proc *, int force);

#endif

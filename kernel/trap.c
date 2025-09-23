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

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

// handle an interrupt, exception, or system call from user space.
// called from trampoline.S
void usertrap(void)
{
  int which_dev = 0;
  // 并非来自内核态
  if ((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");
  // send interrupts and exceptions to kerneltrap(),
  // since we're now in the kernel.
  // stvec指向kernelvec
  w_stvec((uint64)kernelvec);
  struct proc *p = myproc();
  // save user program counter.
  // 记录用户程序的pc
  p->trapframe->epc = r_sepc();
  if (r_scause() == 8)
  {
    // system call
    // 用户程序主动发起的 ecall 系统调用
    if (p->killed)
      exit(-1);

    // sepc points to the ecall instruction,
    // but we want to return to the next instruction.
    // 处理完后返回到 ecall 的下一条指令
    p->trapframe->epc += 4;

    // an interrupt will change sstatus &c registers,
    // so don't enable until done with those registers.
    // 在这里重新打开中断，允许其他设备（比如时钟）中断这个系统调用
    intr_on();
    // 转交给真正的系统调用处理中心
    syscall();
  }
  else if ((which_dev = devintr()) != 0)
  {
    // 不是系统调用，就调用 devintr() 检查是不是外部设备（磁盘、键盘、时钟等）引发的中断
    // give up the CPU if this is a timer interrupt.
    if (which_dev == 2)
    {
      // devintr() 函数确认刚刚发生的是一次时钟中断
      // hw3
      if (myproc()->alarm_interval > 0) // 检查是否调用alarm程序
      {
        if (myproc()->alarm_ticks_left > 0)
        {
          myproc()->alarm_ticks_left--;
        }
        if (myproc()->alarm_ticks_left == 0)
        {
          if (p->in_alarm == 0)
          {
            p->in_alarm = 1;
            myproc()->alarm_ticks_left = myproc()->alarm_interval;     // 重置时钟
            myproc()->alarm_trapframe_backup = *(myproc()->trapframe); // 备份寄存器
            p->trapframe->epc = myproc()->alarm_handler;
          } // 篡改返回位置
        }
      }
    }
  }
  else
  {
    // 其他异常，比如缺页、访问非法内存等，之后的实验需要修改这里
    printf("usertrap(): unexpected scause %p pid=%d\n", r_scause(), p->pid);
    printf(" sepc=%p stval=%p\n", r_sepc(), r_stval());
    p->killed = 1;
  }
  if (p->killed)
    exit(-1);

  // If it was a timer interrupt, yield the CPU.
  // This is checked regardless of what the process was doing.
  // if (which_dev == 2)
  //   yield();
  // 加入之后使得通过的测试变少了

  // 从trapframe中恢复所有用户寄存器，然后调用sret返回用户空间
  usertrapret();
}

//
// return to user space
//
void usertrapret(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(), so turn off interrupts until
  // we're back in user space, where usertrap() is correct.
  intr_off();

  // send syscalls, interrupts, and exceptions to trampoline.S
  w_stvec(TRAMPOLINE + (uservec - trampoline));

  // set up trapframe values that uservec will need when
  // the process next re-enters the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp(); // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.

  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);

  // tell trampoline.S the user page table to switch to.
  uint64 satp = MAKE_SATP(p->pagetable);

  // jump to trampoline.S at the top of memory, which
  // switches to the user page table, restores user registers,
  // and switches to user mode with sret.
  uint64 fn = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64, uint64))fn)(TRAPFRAME, satp);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();

  if ((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if (intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if ((which_dev = devintr()) == 0)
  {
    printf("scause %p\n", scause);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if (which_dev == 2 && myproc() != 0 && myproc()->state == RUNNING)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void clockintr()
{
  acquire(&tickslock);
  ticks++;
  wakeup(&ticks);
  release(&tickslock);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int devintr()
{
  uint64 scause = r_scause();

  if ((scause & 0x8000000000000000L) &&
      (scause & 0xff) == 9)
  {
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if (irq == UART0_IRQ)
    {
      uartintr();
    }
    else if (irq == VIRTIO0_IRQ)
    {
      virtio_disk_intr();
    }
    else if (irq)
    {
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if (irq)
      plic_complete(irq);

    return 1;
  }
  else if (scause == 0x8000000000000001L)
  {
    // software interrupt from a machine-mode timer interrupt,
    // forwarded by timervec in kernelvec.S.

    if (cpuid() == 0)
    {
      clockintr();
    }

    // acknowledge the software interrupt by clearing
    // the SSIP bit in sip.
    w_sip(r_sip() & ~2);

    return 2;
  }
  else
  {
    return 0;
  }
}

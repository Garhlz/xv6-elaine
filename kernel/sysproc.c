#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sysinfo.h"
uint64
sys_exit(void)
{
  int n;
  if (argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if (argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  int addr;
  int n;

  if (argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  if (argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n)
  {
    if (myproc()->killed)
    {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if (argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// 新增， 追踪掩码表示的系统调用
uint64 sys_trace(void)
{
  int n;

  if (argint(0, &n) < 0)
    return -1;
  // myproc()获得当前进程的指针
  myproc()->tracemask = n;
  return 0;
}

uint64 sys_sysinfo(void)
{
  uint64 user_addr;
  if (argaddr(0, &user_addr) < 0)
  {
    return -1;
  }
  struct sysinfo info;
  uint64 freemem = count_freemem();
  uint64 nproc = count_nproc();
  info.freemem = freemem;
  info.nproc = nproc;
  if (copyout(myproc()->pagetable, user_addr, (char *)&info, sizeof(info)) < 0)
  {
    // 使用copy_out安全地把内核态数据复制到用户态
    return -1;
  }
  return 0;
}
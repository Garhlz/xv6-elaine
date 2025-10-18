// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
  struct run *next;
};

struct kmem
{
  struct spinlock lock;
  struct run *freelist;
};

struct kmem kmems[NCPU];

// 所有CPU共享一个内存链表，共享同一把锁，这会导致效率降低
void kinit()
{
  for (int i = 0; i < NCPU; i++)
  {
    char lock_name[8];
    snprintf(lock_name, sizeof(lock_name), "kmem_%d", i);
    initlock(&kmems[i].lock, lock_name);
  }

  freerange(end, (void *)PHYSTOP);
}

void kfree_cpu(void *pa, int cpu_id)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run *)pa;

  acquire(&kmems[cpu_id].lock);
  r->next = kmems[cpu_id].freelist;
  kmems[cpu_id].freelist = r;
  release(&kmems[cpu_id].lock);
}

// 初始内存分配
void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);

  // 将所有初始页面都释放到当前 CPU 的空闲列表中
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
  {
    // 我们直接在这里实现 kfree 的核心逻辑，
    // 并且明确地将内存页添加到 CPU 0 的空闲列表
    struct run *r = (struct run *)p;

    // 注意：这里我们不能调用 acquire(&kmems[0].lock)
    // 因为在 kinit 阶段，系统是单线程启动的，还没有其他 CPU 在运行，
    // 中断也是关闭的，不存在竞争，所以可以直接操作。
    r->next = kmems[0].freelist;
    kmems[0].freelist = r;
  }
}

// 释放一块当前cpu的内存列表中的内存
void kfree(void *pa)
{
  push_off();
  int cpu_id = cpuid();
  pop_off();
  kfree_cpu(pa, cpu_id);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
// void kfree(void *pa)

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{

  push_off();
  int cpu = cpuid();
  pop_off();
  struct run *r;

  // 2. 尝试从当前 CPU 的列表中分配 (Happy Path)
  acquire(&kmems[cpu].lock);
  r = kmems[cpu].freelist;
  if (r)
    kmems[cpu].freelist = r->next;
  release(&kmems[cpu].lock);

  // 3. 如果成功，直接返回
  if (r)
  {
    memset((char *)r, 5, PGSIZE); // fill with junk
    return (void *)r;
  }

  // 4. 如果当前 CPU 列表为空，从其他CPU的空闲内存列表中偷取一个head
  for (int other_cpu = 0; other_cpu < NCPU; other_cpu++)
  {
    if (other_cpu == cpu)
      continue;

    acquire(&kmems[other_cpu].lock);
    r = kmems[other_cpu].freelist;
    if (r)
    {
      kmems[other_cpu].freelist = r->next;
    }
    release(&kmems[other_cpu].lock);
    if (r)
    {
      memset((char *)r, 5, PGSIZE);
      return (void *)r;
    }
  }
  return 0;
}

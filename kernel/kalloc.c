// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

#define PA2INDEX(pa) (((uint64)pa - KERNBASE) / PGSIZE)

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run
{
  struct run *next;
};

struct
{
  struct spinlock lock;
  struct run *freelist;
  // TODO 物理页的引用计数数组, 是共享资源需要上锁
  int ref_count[PHYSTOP / PGSIZE];
} kmem;

void kinit()
{
  initlock(&kmem.lock, "kmem");
  freerange(end, (void *)PHYSTOP);
}

// void freerange(void *pa_start, void *pa_end)
// {
//   char *p;
//   p = (char *)PGROUNDUP((uint64)pa_start);
//   for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
//     kfree(p);
// }
// TODO 在初始化的时候， 不再调用kfree函数
void freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char *)PGROUNDUP((uint64)pa_start);
  for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
  {
    // 1. 确保这个页面的引用计数为 0
    kmem.ref_count[PA2INDEX(p)] = 0;

    // 2. 将它加入空闲链表 (这是原来 kfree 的核心逻辑)
    struct run *r = (struct run *)p;
    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
  }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa)
{
  struct run *r;

  if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  acquire(&kmem.lock);

  int index = PA2INDEX(pa);

  // TODO 修改为减少引用数量
  // if (kmem.ref_count[index] <= 0) // 防御性检查
  //   panic("kfree: ref count is zero or negative");

  r = (struct run *)pa;

  kmem.ref_count[index]--;
  if (kmem.ref_count[index] == 0)
  {
    memset(pa, 1, PGSIZE);
    r->next = kmem.freelist;
    kmem.freelist = r;
  }
  release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;

  acquire(&kmem.lock);
  r = kmem.freelist;
  if (r)
  {
    kmem.freelist = r->next;
    // 这里的r就是物理地址pa
    // TODO 初始化引用计数为1
    kmem.ref_count[PA2INDEX(r)] = 1;
  }
  release(&kmem.lock);

  if (r)
    memset((char *)r, 5, PGSIZE); // fill with junk
  return (void *)r;
}

// TODO 新建增减引用的函数，暴露接口
void increase_ref(uint64 pa)
{
  acquire(&kmem.lock);
  kmem.ref_count[PA2INDEX(pa)]++;
  release(&kmem.lock);
}

void decrease_ref(uint64 pa)
{
  acquire(&kmem.lock);
  kmem.ref_count[PA2INDEX(pa)]--;
  release(&kmem.lock);
}

int get_ref(uint64 pa)
{
  acquire(&kmem.lock);
  int ret = kmem.ref_count[PA2INDEX(pa)];
  release(&kmem.lock);
  return ret;
}
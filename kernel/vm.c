#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[]; // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// 在系统启动时，创建并初始化内核自己的页表。这个函数只被调用一次kvminithart
// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  // 1. 为根页表(L2)分配一个物理页
  kpgtbl = (pagetable_t)kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // 2. 映射硬件设备 (Memory-Mapped I/O)
  // 将物理地址上的 UART0 (串口) 映射到等值的虚拟地址上
  kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

  // 3. 映射内核代码和数据 (直接映射)
  // 将内核代码段映射为可读 + 可执行
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);
  // 将内核数据段和直到PHYSTOP的全部物理内存映射为可读 + 可写
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

  // 4. 映射 Trampoline 页
  // 将物理地址 `trampoline` 映射到虚拟地址空间的顶部 TRAMPOLINE
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // 5. 映射所有进程的内核栈
  proc_mapstacks(kpgtbl);

  return kpgtbl;
}

// Initialize the one kernel_pagetable
void kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch h/w page table register to the kernel's page table,
// and enable paging.
void kvminithart()
{
  w_satp(MAKE_SATP(kernel_pagetable)); // 将刚刚创建好的内核页表的地址写入 satp 寄存器
  sfence_vma();                        // 刷新 TLB，确保旧的、无效的地址翻译缓存被清空
}

// 输入虚拟地址va, 返回对应的物理地址，如果alloc==1就分配新的页表
// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// The risc-v Sv39 scheme has three levels of page-table
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   39..63 -- must be zero.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
// pagetable_t是指向一个 4KB 页表页的指针，可看作一个含 512 个 pte_t 的数组。
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if (va >= MAXVA)
    panic("walk");

  for (int level = 2; level > 0; level--)
  {
    // PX 通过虚拟地址中的9个位确定页表中数组中的位置
    pte_t *pte = &pagetable[PX(level, va)];
    if (*pte & PTE_V)
    {
      // 页表的PTE编码直接右移10左移12就获得物理地址
      pagetable = (pagetable_t)PTE2PA(*pte);
    }
    else
    {
      if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
        return 0;
      // 分配一块新页存储这个页表
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if (va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    return 0;
  if ((*pte & PTE_V) == 0) // 最终PTE无效
    return 0;
  if ((*pte & PTE_U) == 0) // 用户不可访问的页
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if (mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// 建立虚拟内存和物理内存的映射页表PTE
// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned. Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  a = PGROUNDDOWN(va);               // 起始地址向下取整
  last = PGROUNDDOWN(va + size - 1); // 结束地址向下取整

  for (;;)
  {
    // 1. "勘探"：调用 walk 找到 va 对应的 L0 PTE 的地址
    //    alloc=1 表示如果中间的 L1/L0 页表不存在，就创建它们。
    if ((pte = walk(pagetable, a, 1)) == 0)
      return -1;

    // 2. 检查这块地是否已被占用（重复映射是bug）
    if (*pte & PTE_V)
      panic("mappages: remap");

    // 3. "建设": 在PTE中填入物理地址、权限和有效位，完成映射
    *pte = PA2PTE(pa) | perm | PTE_V;

    if (a == last) // 映射完最后一页，退出
      break;

    // 4. 移动到下一页
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. The mappings must exist.
// Optionally free the physical memory.
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if ((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for (a = va; a < va + npages * PGSIZE; a += PGSIZE)
  {
    if ((pte = walk(pagetable, a, 0)) == 0)
      panic("uvmunmap: walk");
    if ((*pte & PTE_V) == 0)
      panic("uvmunmap: not mapped");
    if (PTE_FLAGS(*pte) == PTE_V)
      panic("uvmunmap: not a leaf");
    if (do_free)
    {
      uint64 pa = PTE2PA(*pte);
      kfree((void *)pa);
    }
    *pte = 0;
  }
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t)kalloc();
  if (pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Load the user initcode into address 0 of pagetable,
// for the very first process.
// sz must be less than a page.
void uvminit(pagetable_t pagetable, uchar *src, uint sz)
{
  char *mem;

  if (sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
  memmove(mem, src, sz);
}

// Allocate PTEs and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  char *mem;
  uint64 a;

  if (newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for (a = oldsz; a < newsz; a += PGSIZE)
  {
    mem = kalloc();
    if (mem == 0)
    {
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U) != 0)
    {
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if (newsz >= oldsz)
    return oldsz;

  if (PGROUNDUP(newsz) < PGROUNDUP(oldsz))
  {
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0)
    {
      // this PTE points to a lower-level page table.
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    }
    else if (pte & PTE_V)
    {
      // 假设所有叶子都应该在调用freewalk之前被 uvmunmap 清理干净
      panic("freewalk: leaf");
    }
  }
  kfree((void *)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void uvmfree(pagetable_t pagetable, uint64 sz)
{
  if (sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for (i = 0; i < sz; i += PGSIZE)
  {
    if ((pte = walk(old, i, 0)) == 0)
      panic("uvmcopy: pte should exist");
    if ((*pte & PTE_V) == 0)
      panic("uvmcopy: page not present");
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if ((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char *)pa, PGSIZE);
    if (mappages(new, i, PGSIZE, (uint64)mem, flags) != 0)
    {
      kfree(mem);
      goto err;
    }
  }
  return 0;

err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;

  pte = walk(pagetable, va, 0);
  if (pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// 安全地将内核的数据，复制到指定用户进程的虚拟地址
// 对于内核来说，在 RAM 这个范围内，虚拟地址和物理地址是等价的, 可以直接操作任何物理内存
// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0)
  {
    // 1. 计算目标虚拟地址 dstva 所在页的起始地址 va0
    va0 = PGROUNDDOWN(dstva);

    // 2. 关键：在【用户的页表】中，将用户的虚拟页地址 va0 翻译成物理地址 pa0
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0) // 如果地址无效（未映射），则失败
      return -1;

    // 3. 计算在当前页内还能复制多少字节
    n = PGSIZE - (dstva - va0);
    if (n > len)
      n = len;

    // 4. 执行复制：直接操作物理地址
    //    pa0 + (dstva - va0) 就是目标虚拟地址 dstva 对应的精确物理地址, 物理页基地址 + 页内偏移量
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    // 5. 更新计数器，准备处理跨页的剩余部分
    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while (len > 0)
  {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while (got_null == 0 && max > 0)
  {
    va0 = PGROUNDDOWN(srcva);
    pa0 = walkaddr(pagetable, va0);
    if (pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if (n > max)
      n = max;

    char *p = (char *)(pa0 + (srcva - va0));
    while (n > 0)
    {
      if (*p == '\0')
      {
        *dst = '\0';
        got_null = 1;
        break;
      }
      else
      {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if (got_null)
  {
    return 0;
  }
  else
  {
    return -1;
  }
}

// hw2, 内部函数
static void vmprint_recursive(pagetable_t pagetable, int depth)
{
  for (int i = 0; i < 512; i++)
  {
    pte_t pte = pagetable[i];
    if (pte & PTE_V)
    {
      for (int j = 0; j < depth; j++)
      {
        if (j != 0)
          printf(" ");
        printf("..");
      }
      printf("%d: pte %p pa %p\n", i, pte, PTE2PA(pte));
      // 如果有效但是其他权限都没有， 不是叶子节点
      if ((pte & (PTE_R | PTE_W | PTE_X)) == 0)
      {
        // this PTE points to a lower-level page table.
        uint64 child = PTE2PA(pte);
        vmprint_recursive((pagetable_t)child, depth + 1);
      }
    }
  }
}

// 外部接口
void vmprint(pagetable_t pagetable)
{
  printf("page table %p\n", pagetable);
  vmprint_recursive(pagetable, 1);
}
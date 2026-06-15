#include "param.h"
#include "types.h"
#include "memlayout.h"
#include "elf.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"

/*
 * 内核页表。
 */
pagetable_t kernel_pagetable;

extern char etext[]; // kernel.ld 将其设为内核代码段的结束位置。

extern char trampoline[]; // 来自 trampoline.S

static int user_faultin(pagetable_t pagetable, uint64 va, int write) {
    struct proc *p = myproc();

    if (p == 0 || pagetable != p->pagetable)
        return -1;
    return mmap_fault(va, write);
}

// 创建内核的直接映射页表。
pagetable_t kvmmake(void) {
    pagetable_t kpgtbl;

    kpgtbl = (pagetable_t)kalloc();
    memset(kpgtbl, 0, PGSIZE);

    // UART 寄存器
    kvmmap(kpgtbl, UART0, UART0, PGSIZE, PTE_R | PTE_W);

    // virtio MMIO 磁盘接口
    kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);

    // PCI-E ECAM 配置空间，供 pci.c 使用
    kvmmap(kpgtbl, 0x30000000L, 0x30000000L, 0x10000000, PTE_R | PTE_W);

    // pci.c 将 e1000 的寄存器映射到此。
    kvmmap(kpgtbl, 0x40000000L, 0x40000000L, 0x20000, PTE_R | PTE_W);

    // PLIC 中断控制器
    kvmmap(kpgtbl, PLIC, PLIC, 0x400000, PTE_R | PTE_W);

    // 内核代码段：可读可执行。
    kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext - KERNBASE, PTE_R | PTE_X);

    // 内核数据段及可用物理内存。
    kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP - (uint64)etext, PTE_R | PTE_W);

    // 将 trampoline 映射到内核最高虚拟地址，用于 trap 进出。
    kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

    // 映射内核栈
    proc_mapstacks(kpgtbl);

    return kpgtbl;
}

// 初始化唯一的内核页表。
void kvminit(void) {
    kernel_pagetable = kvmmake();
}

// 将硬件页表寄存器切换到内核页表并开启分页。
void kvminithart() {
    w_satp(MAKE_SATP(kernel_pagetable));
    sfence_vma();
}

// 返回虚拟地址 va 在页表 pagetable 中对应的 PTE 地址。
// 若 alloc≠0，则按需创建各级页表页。
//
// RISC-V Sv39 页表方案共有三级页表页，
// 每页包含 512 个 64 位 PTE。
// 64 位虚拟地址分为五段：
//   39..63 — 必须全零。
//   30..38 — 9 位 L2 索引。
//   21..29 — 9 位 L1 索引。
//   12..20 — 9 位 L0 索引。
//    0..11 — 12 位页内偏移。
pte_t *walk(pagetable_t pagetable, uint64 va, int alloc) {
    if (va >= MAXVA)
        panic("walk");

    for (int level = 2; level > 0; level--) {
        pte_t *pte = &pagetable[PX(level, va)];
        if (*pte & PTE_V) {
            pagetable = (pagetable_t)PTE2PA(*pte);
        } else {
            if (!alloc || (pagetable = (pde_t *)kalloc()) == 0)
                return 0;
            memset(pagetable, 0, PGSIZE);
            *pte = PA2PTE(pagetable) | PTE_V;
        }
    }
    return &pagetable[PX(0, va)];
}

// 查虚拟地址对应的物理地址，未映射则返回 0。
// 仅用于查找用户页。
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
    pte_t *pte;
    uint64 pa;

    if (va >= MAXVA)
        return 0;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        return 0;
    if ((*pte & PTE_V) == 0)
        return 0;
    if ((*pte & PTE_U) == 0)
        return 0;
    pa = PTE2PA(*pte);
    return pa;
}

// 向内核页表添加映射。仅启动时使用。
// 不刷新 TLB，也不开启分页。
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm) {
    if (mappages(kpgtbl, va, sz, pa, perm) != 0)
        panic("kvmmap");
}

// 为从 va 开始的一段虚拟地址创建 PTE，映射到从 pa 开始的物理地址。
// va 和 size 可以非页对齐。成功返回 0，若 walk() 无法分配页表页则返回 -1。
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm) {
    uint64 a, last;
    pte_t *pte;

    if (size == 0)
        panic("mappages: size");

    a = PGROUNDDOWN(va);
    last = PGROUNDDOWN(va + size - 1);
    for (;;) {
        if ((pte = walk(pagetable, a, 1)) == 0)
            return -1;
        if (*pte & PTE_V)
            panic("mappages: remap");
        *pte = PA2PTE(pa) | perm | PTE_V;
        if (a == last)
            break;
        a += PGSIZE;
        pa += PGSIZE;
    }
    return 0;
}

// 从 va 开始移除 npages 个页映射。va 必须页对齐。
// 映射必须存在。若 do_free 非零则释放对应物理内存。
void uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
    uint64 a;
    pte_t *pte;

    if ((va % PGSIZE) != 0)
        panic("uvmunmap: not aligned");

    for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
        if ((pte = walk(pagetable, a, 0)) == 0)
            panic("uvmunmap: walk");
        if ((*pte & PTE_V) == 0) {
            printf("va=%p pte=%p\n", a, *pte);
            panic("uvmunmap: not mapped");
        }
        if (PTE_FLAGS(*pte) == PTE_V)
            panic("uvmunmap: not a leaf");
        if (do_free) {
            uint64 pa = PTE2PA(*pte);
            kfree((void *)pa);
        }
        *pte = 0;
    }
}

// 移除 mmap 页，允许页面尚未被惰性装载。
void uvmunmap_mmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free) {
    uint64 a;
    pte_t *pte;

    if ((va % PGSIZE) != 0)
        panic("uvmunmap_mmap: not aligned");

    for (a = va; a < va + npages * PGSIZE; a += PGSIZE) {
        if ((pte = walk(pagetable, a, 0)) == 0)
            continue;
        if ((*pte & PTE_V) == 0)
            continue;
        if (PTE_FLAGS(*pte) == PTE_V)
            panic("uvmunmap_mmap: not a leaf");
        if (do_free) {
            uint64 pa = PTE2PA(*pte);
            kfree((void *)pa);
        }
        *pte = 0;
    }
}

// 创建空的用户页表。内存不足时返回 0。
pagetable_t uvmcreate() {
    pagetable_t pagetable;
    pagetable = (pagetable_t)kalloc();
    if (pagetable == 0)
        return 0;
    memset(pagetable, 0, PGSIZE);
    return pagetable;
}

// 将 initcode 装载到页表地址 0 处，供第一个进程使用。
// sz 必须小于一页。
void uvminit(pagetable_t pagetable, uchar *src, uint sz) {
    char *mem;

    if (sz >= PGSIZE)
        panic("inituvm: more than a page");
    mem = kalloc();
    memset(mem, 0, PGSIZE);
    mappages(pagetable, 0, PGSIZE, (uint64)mem, PTE_W | PTE_R | PTE_X | PTE_U);
    memmove(mem, src, sz);
}

// 分配 PTE 和物理内存，将进程从 oldsz 扩展到 newsz（无需页对齐）。
// 返回新大小，失败返回 0。
uint64 uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
    char *mem;
    uint64 a;

    if (newsz < oldsz)
        return oldsz;

    oldsz = PGROUNDUP(oldsz);
    for (a = oldsz; a < newsz; a += PGSIZE) {
        mem = kalloc();
        if (mem == 0) {
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
        memset(mem, 0, PGSIZE);
        if (mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_W | PTE_X | PTE_R | PTE_U) != 0) {
            kfree(mem);
            uvmdealloc(pagetable, a, oldsz);
            return 0;
        }
    }
    return newsz;
}

// 释放用户页，将进程大小从 oldsz 缩减到 newsz。
// oldsz 和 newsz 无需页对齐，newsz 也不必小于 oldsz。
// oldsz 可比实际进程大小更大。返回新的进程大小。
uint64 uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz) {
    if (newsz >= oldsz)
        return oldsz;

    if (PGROUNDUP(newsz) < PGROUNDUP(oldsz)) {
        int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
        uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
    }

    return newsz;
}

// 递归释放页表页。所有叶子映射必须已提前移除。
void freewalk(pagetable_t pagetable) {
    // 每个页表页有 2^9 = 512 个 PTE。
    for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];
        if ((pte & PTE_V) && (pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            // 该 PTE 指向下级页表。
            uint64 child = PTE2PA(pte);
            freewalk((pagetable_t)child);
            pagetable[i] = 0;
        } else if (pte & PTE_V) {
            panic("freewalk: leaf");
        }
    }
    kfree((void *)pagetable);
}

// 先释放用户内存页，再释放页表页。
void uvmfree(pagetable_t pagetable, uint64 sz) {
    if (sz > 0)
        uvmunmap(pagetable, 0, PGROUNDUP(sz) / PGSIZE, 1);
    freewalk(pagetable);
}

// 将父进程页表中的内存复制到子进程页表。
// 同时复制页表结构和物理内存。
// 成功返回 0，失败返回 -1 并释放已分配的页面。
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
    pte_t *pte;
    uint64 pa, i;
    uint flags;

    for (i = 0; i < sz; i += PGSIZE) {
        if ((pte = walk(old, i, 0)) == 0)
            panic("uvmcopy: pte should exist");
        if ((*pte & PTE_V) == 0)
            panic("uvmcopy: page not present");
        pa = PTE2PA(*pte);
        flags = PTE_FLAGS(*pte);

        // 仅可写页面参与 COW。只读映射在父子进程中均应保持只读。
        if (flags & PTE_W) {
            *pte &= ~PTE_W;
            *pte |= PTE_COW;
            flags &= ~PTE_W;
            flags |= PTE_COW;
        }

        if (mappages(new, i, PGSIZE, pa, flags) != 0)
            goto err;
        increase_ref(pa);
    }
    return 0;

err:
    uvmunmap(new, 0, i / PGSIZE, 1);
    return -1;
}

// 将 PTE 的用户访问位清零，用于 exec 设置用户栈 guard page。
void uvmclear(pagetable_t pagetable, uint64 va) {
    pte_t *pte;

    pte = walk(pagetable, va, 0);
    if (pte == 0)
        panic("uvmclear");
    *pte &= ~PTE_U;
}

// 递归打印页表项。
static void vmprint_recursive(pagetable_t pagetable, int depth) {
    for (int i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];
        if (pte & PTE_V) {
            for (int j = 0; j < depth; j++) {
                if (j != 0)
                    printf(" ");
                printf("..");
            }
            printf("%d: pte %p pa %p\n", i, pte, PTE2PA(pte));
            if ((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
                uint64 child = PTE2PA(pte);
                vmprint_recursive((pagetable_t)child, depth + 1);
            }
        }
    }
}

// 打印页表。
void vmprint(pagetable_t pagetable) {
    printf("page table %p\n", pagetable);
    vmprint_recursive(pagetable, 1);
}

// 从内核向用户空间拷贝数据。
// 将 len 字节从 src 拷贝到指定页表中的虚拟地址 dstva。
// 成功返回 0，失败返回 -1。
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
    uint64 n, va0, pa0;

    while (len > 0) {
        va0 = PGROUNDDOWN(dstva);
        if (va0 >= MAXVA)
            return -1;
        pte_t *pte = walk(pagetable, va0, 0);
        int page_not_mapped = (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0);
        if (page_not_mapped) {
            if (user_faultin(pagetable, va0, 1) < 0)
                return -1;
            pte = walk(pagetable, va0, 0);
            page_not_mapped = (pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0);
            if (page_not_mapped)
                return -1;
        }
        pa0 = PTE2PA(*pte);
        if (pa0 == 0)
            return -1;
        n = PGSIZE - (dstva - va0);
        if (n > len)
            n = len;

        // 目标页不可写时，先处理 COW（写时复制）。
        if (!(*pte & PTE_W)) {
            if ((*pte & PTE_COW) == 0)
                return -1;
            uint64 cow_pa = PTE2PA(*pte);
            uint flags = PTE_FLAGS(*pte);
            if (get_ref(cow_pa) > 1) {
                char *mem = kalloc();
                if (mem == 0)
                    return -1;
                memmove(mem, (char *)cow_pa, PGSIZE);
                flags &= ~PTE_COW;
                flags |= PTE_W;
                *pte = PA2PTE((uint64)mem) | flags;
                kfree((void *)cow_pa);
            } else {
                *pte &= ~PTE_COW;
                *pte |= PTE_W;
            }
            sfence_vma();
            pa0 = PTE2PA(*pte);
        }

        memmove((void *)(pa0 + (dstva - va0)), src, n);

        len -= n;
        src += n;
        dstva = va0 + PGSIZE;
    }
    return 0;
}

// 从用户空间向内核拷贝数据。
// 从指定页表中的虚拟地址 srcva 拷贝 len 字节到 dst。
// 成功返回 0，失败返回 -1。
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
    uint64 n, va0, pa0;

    while (len > 0) {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0) {
            if (user_faultin(pagetable, va0, 0) < 0)
                return -1;
            pa0 = walkaddr(pagetable, va0);
            if (pa0 == 0)
                return -1;
        }
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

// 从用户空间向内核拷贝一个以空字符结尾的字符串。
// 从指定页表中的虚拟地址 srcva 向 dst 逐字节拷贝，
// 遇 '\0' 停止或达到 max 上限。
// 成功返回 0，失败返回 -1。
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
    uint64 n, va0, pa0;
    int got_null = 0;

    while (got_null == 0 && max > 0) {
        va0 = PGROUNDDOWN(srcva);
        pa0 = walkaddr(pagetable, va0);
        if (pa0 == 0) {
            if (user_faultin(pagetable, va0, 0) < 0)
                return -1;
            pa0 = walkaddr(pagetable, va0);
            if (pa0 == 0)
                return -1;
        }
        n = PGSIZE - (srcva - va0);
        if (n > max)
            n = max;

        char *p = (char *)(pa0 + (srcva - va0));
        while (n > 0) {
            if (*p == '\0') {
                *dst = '\0';
                got_null = 1;
                break;
            } else {
                *dst = *p;
            }
            --n;
            --max;
            p++;
            dst++;
        }

        srcva = va0 + PGSIZE;
    }
    if (got_null) {
        return 0;
    } else {
        return -1;
    }
}

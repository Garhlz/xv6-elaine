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

struct run {
    struct run *next;
};

struct {
    struct spinlock lock;
    struct run *freelist;
} kmems[NCPU];

static char kmem_lock_names[NCPU][16];

struct {
    struct spinlock lock;
    int ref_count[PHYSTOP / PGSIZE];
} page_refs;

void kinit(void) {
    for (int i = 0; i < NCPU; i++) {
        snprintf(kmem_lock_names[i], sizeof(kmem_lock_names[i]), "kmem_%d", i);
        initlock(&kmems[i].lock, kmem_lock_names[i]);
        kmems[i].freelist = 0;
    }
    initlock(&page_refs.lock, "page_refs");
    freerange(end, (void *)PHYSTOP);
}

static void kfree_cpu(void *pa, int cpu_id) {
    struct run *r;

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    memset(pa, 1, PGSIZE);
    r = (struct run *)pa;

    acquire(&kmems[cpu_id].lock);
    r->next = kmems[cpu_id].freelist;
    kmems[cpu_id].freelist = r;
    release(&kmems[cpu_id].lock);
}

void freerange(void *pa_start, void *pa_end) {
    char *p;

    p = (char *)PGROUNDUP((uint64)pa_start);
    for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) {
        page_refs.ref_count[PA2INDEX(p)] = 0;
        kfree_cpu(p, 0);
    }
}

void kfree(void *pa) {
    int free_it = 0;

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    acquire(&page_refs.lock);
    page_refs.ref_count[PA2INDEX(pa)]--;
    if (page_refs.ref_count[PA2INDEX(pa)] == 0)
        free_it = 1;
    release(&page_refs.lock);

    if (!free_it)
        return;

    push_off();
    int cpu_id = cpuid();
    pop_off();
    kfree_cpu(pa, cpu_id);
}

void *kalloc(void) {
    struct run *r = 0;

    push_off();
    int cpu = cpuid();
    pop_off();

    acquire(&kmems[cpu].lock);
    r = kmems[cpu].freelist;
    if (r)
        kmems[cpu].freelist = r->next;
    release(&kmems[cpu].lock);

    if (r == 0) {
        for (int other_cpu = 0; other_cpu < NCPU; other_cpu++) {
            if (other_cpu == cpu)
                continue;

            acquire(&kmems[other_cpu].lock);
            r = kmems[other_cpu].freelist;
            if (r)
                kmems[other_cpu].freelist = r->next;
            release(&kmems[other_cpu].lock);

            if (r)
                break;
        }
    }

    if (r) {
        acquire(&page_refs.lock);
        page_refs.ref_count[PA2INDEX(r)] = 1;
        release(&page_refs.lock);
        memset((char *)r, 5, PGSIZE);
    }

    return (void *)r;
}

void increase_ref(uint64 pa) {
    acquire(&page_refs.lock);
    page_refs.ref_count[PA2INDEX(pa)]++;
    release(&page_refs.lock);
}

void decrease_ref(uint64 pa) {
    acquire(&page_refs.lock);
    page_refs.ref_count[PA2INDEX(pa)]--;
    release(&page_refs.lock);
}

int get_ref(uint64 pa) {
    int ret;

    acquire(&page_refs.lock);
    ret = page_refs.ref_count[PA2INDEX(pa)];
    release(&page_refs.lock);
    return ret;
}

uint64 count_freemem(void) {
    struct run *r;
    uint64 cnt = 0;

    for (int i = 0; i < NCPU; i++) {
        acquire(&kmems[i].lock);
        r = kmems[i].freelist;
        while (r) {
            cnt++;
            r = r->next;
        }
        release(&kmems[i].lock);
    }

    return cnt * PGSIZE;
}

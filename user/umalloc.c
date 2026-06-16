//
// umalloc —— xv6 用户态内存分配器。
//
// 基于 Kernighan & Ritchie, "The C Programming Language" 2nd ed. Section 8.7
// 的实现，采用隐式空闲链表（implicit free list）管理堆空间。
//
// 核心设计:
//   - 空闲块通过单向循环链表组织，按地址排序以支持合并相邻空闲块。
//   - 每个块前面有一个 Header，记录块大小和指向下一个空闲块的指针。
//   - malloc 采用 first-fit 策略：从头遍历空闲链，返回第一个足够大的块。
//   - free 将释放块按地址插入空闲链，并尝试与前后相邻块合并（coalescing）。
//   - 当堆空间不足时，通过 sbrk() 系统调用向内核申请更多内存。
//
// 注意：这是一个教学级分配器——无对齐保证、无碎片整理、无线程安全。
// 但它完整展示了 malloc/free 的核心思想。
//

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

// Align — 最严格对齐的类型，用于保证 Header 按最大对齐分配。
// 将 Align 放入 Header 的联合体中，迫使编译器按最坏情况对齐。
typedef long Align;

// Header 联合体：兼顾元数据存储和内存对齐。
//   s.ptr  — 指向空闲链表中下一个空闲块
//   s.size — 当前块大小（单位为 sizeof(Header)，即块包含多少个 Header）
//   x      — 仅用于对齐，不存储实际数据
// 分配给用户时，Header 紧邻用户数据之前，
// malloc 返回的是 Header 之后的数据区起始地址。
union header {
    struct {
        union header *ptr;
        uint size;
    } s;
    Align x;
};

typedef union header Header;

static Header base;   // 空闲链表的哨兵节点（哑元，不管理实际内存）
static Header *freep; // 空闲链表起始指针

// free(ap): 将 ap 指向的已分配内存归还到空闲链表。
//
// 三步：
//   1. 定位：在空闲链表中找到 bp 的地址顺序位置（p 为前驱，p->s.ptr 为后继）。
//      链表按地址升序排列，支持相邻块合并。
//   2. 合并（高地址方向）：如果 bp 尾部紧邻下一个空闲块，吸收之。
//   3. 合并（低地址方向）：如果 p 尾部紧邻 bp，吸收之。
//   完成时 freep 指向最近释放的块，优化后续分配搜索。
void free(void *user_ptr) {
    Header *blk, *pos;

    // blk 指向用户指针之前的 Header（块的真实起始位置）
    blk = (Header *)user_ptr - 1;

    // 在按地址排序的循环空闲链表中找到 blk 的前驱位置 pos。
    // 循环条件 !(blk > pos && blk < pos->s.ptr)：
    //   不满足"blk 在 pos 和 pos->s.ptr 之间"则继续向后搜索。
    // 额外条件 pos >= pos->s.ptr 检测链表回绕点（最高地址→最低地址），
    //   此时如果 blk 在回绕区间之外（blk > pos 或 blk < pos->s.ptr）则在此插入。
    for (pos = freep; !(blk > pos && blk < pos->s.ptr); pos = pos->s.ptr)
        if (pos >= pos->s.ptr && (blk > pos || blk < pos->s.ptr))
            break;

    // 合并高地址方向：blk 与紧随其后的空闲块相邻？
    if (blk + blk->s.size == pos->s.ptr) {
        blk->s.size += pos->s.ptr->s.size; // 吸收后继的大小
        blk->s.ptr = pos->s.ptr->s.ptr;    // 跳过已合并的后继
    } else
        blk->s.ptr = pos->s.ptr;

    // 合并低地址方向：pos 与 blk 相邻？
    if (pos + pos->s.size == blk) {
        pos->s.size += blk->s.size; // pos 吸收 blk 的大小
        pos->s.ptr = blk->s.ptr;    // 跳过已合并的 blk
    } else
        pos->s.ptr = blk;

    freep = pos;
}

// morecore(nu): 通过 sbrk() 向内核申请 nu 个 Header 单位的新堆空间。
// 最少申请 4096 字节（一页），减少频繁的系统调用。
// 新空间包装为一个大空闲块并插入空闲链表。
// 返回空闲链表中空闲块的 Header 指针，失败返回 0。
static Header *morecore(uint header_count) {
    char *p;
    Header *hp;

    if (header_count < 4096)
        header_count = 4096; // 至少申请一页，减少 sbrk 调用
    p = sbrk(header_count * sizeof(Header));
    if (p == (char *)-1)
        return 0; // sbrk 失败
    hp = (Header *)p;
    hp->s.size = header_count;
    free((void *)(hp + 1)); // 包装为"用户已释放"的空闲块插入链表
    return freep;
}

// malloc(nbytes): 分配至少 nbytes 字节的内存。
//
// 采用 first-fit 策略：
//   1. 从 freep 开始遍历空闲链表，找第一个 size >= nunits 的块。
//   2. 如果块恰好等于所需大小，整个移除。
//   3. 如果块更大，从末尾切出所需大小，剩余部分留在链表中。
//   4. 遍历一圈无合适块 → 调用 morecore() 扩展堆 → 继续。
//
// 返回指向数据区的指针（已跳过 Header），失败返回 0。
void *malloc(uint nbytes) {
    Header *p, *prev;
    uint nunits;

    // 将字节转为 Header 单位（+1 用于 Header 自身占用的空间）
    nunits = (nbytes + sizeof(Header) - 1) / sizeof(Header) + 1;

    // 首次调用：初始化空闲链表，base 自己指向自己构成循环
    if ((prev = freep) == 0) {
        base.s.ptr = freep = prev = &base;
        base.s.size = 0;
    }

    // 遍历循环空闲链表，first-fit 搜索
    for (p = prev->s.ptr;; prev = p, p = p->s.ptr) {
        if (p->s.size >= nunits) {
            // 找到足够大的块
            if (p->s.size == nunits)
                prev->s.ptr = p->s.ptr; // 精确匹配：从链表移除
            else {
                // 切割：从块的末尾切出 nunits，剩余留在空闲链
                p->s.size -= nunits;
                p += p->s.size; // 移到切出块的起始位置
                p->s.size = nunits;
            }
            freep = prev;           // 下次搜索从这里开始
            return (void *)(p + 1); // 跳过 Header，返回数据区地址
        }
        // 遍历一圈无果？申请更多堆空间
        if (p == freep)
            if ((p = morecore(nunits)) == 0)
                return 0; // 内存耗尽
    }
}

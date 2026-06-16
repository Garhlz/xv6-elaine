//
// exec() —— 加载并执行新程序。
//
// 核心流程:
//   1. 打开 ELF 文件，校验 header  → open_exec()
//   2. 创建新用户页表                → proc_pagetable()
//   3. 加载 ELF 各 LOAD 段到新页表   → load_elf()
//   4. 构造用户栈，压入 argv         → setup_user_stack()
//   5. 设置 a1=argv，保存进程名
//   6. 清理旧 mmap，提交换页          → commit_exec()
//
// 整个过程中，旧地址空间在步骤 6 之前一直保持有效——
// 如果任一步骤失败，goto bad 会释放新页表并返回 -1，
// 调用者（通常是 shell）可以继续正常执行。
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz);
static struct inode *open_exec(char *path, struct elfhdr *elf);
static int load_elf(pagetable_t pagetable, struct inode *ip, struct elfhdr *elf, uint64 *sz);
static int setup_user_stack(pagetable_t pagetable, char **argv, uint64 sz, uint64 *newsz,
                            uint64 *sp_out, uint64 *argc_out);
static void save_proc_name(struct proc *p, char *path);
static void commit_exec(struct proc *p, pagetable_t pagetable, uint64 sz, uint64 sp, uint64 entry,
                        uint64 oldsz);

int exec(char *path, char **argv) {
    uint64 argc = 0, oldsz, sp = 0, sz = 0;
    struct elfhdr elf;
    struct inode *ip = 0;
    pagetable_t pagetable = 0;
    struct proc *p = myproc();

    begin_op();
    // 1. 打开 ELF 文件，读取并校验 header
    if ((ip = open_exec(path, &elf)) == 0) {
        end_op();
        return -1;
    }

    // 2. 创建全新用户页表（只有 trampoline/trapframe/USYSCALL 映射）
    if ((pagetable = proc_pagetable(p)) == 0)
        goto bad;

    // 3. 加载 ELF LOAD 段到新页表
    if (load_elf(pagetable, ip, &elf, &sz) < 0)
        goto bad;

    // inode 用毕，释放文件系统资源
    iunlockput(ip);
    end_op();
    ip = 0; // 置空避免 bad 路径重复释放

    // 4. 构造用户栈：guard page ← stack page，压入 argv 字符串和指针数组
    if (setup_user_stack(pagetable, argv, sz, &sz, &sp, &argc) < 0)
        goto bad;

    // 5. 设置 a1=argv，保存进程名
    p->trapframe->a1 = sp;
    oldsz = p->sz;
    save_proc_name(p, path);

    // 6. 清理旧 mmap 映射，提交新页表
    if (mmap_cleanup(p, 0) < 0)
        goto bad;
    commit_exec(p, pagetable, sz, sp, elf.entry, oldsz);

    if (p->pid == 1)
        vmprint(p->pagetable); // init 进程：打印页表结构便于调试

    // 返回 argc，由 syscall() 写入 a0，传递给用户态 main()
    return argc;

bad:
    // 失败路径：释放新页表（如果已分配）和 inode 引用
    if (pagetable)
        proc_freepagetable(pagetable, sz);
    if (ip) {
        iunlockput(ip);
        end_op();
    }
    return -1;
}

// 打开 ELF 文件并校验 header。
// 通过 namei() 查找 path → ilock → 读取 ELF header → 校验 magic。
// 成功时返回已加锁 inode（调用者负责 iunlockput），失败时返回 0。
static struct inode *open_exec(char *path, struct elfhdr *elf) {
    struct inode *ip;

    if ((ip = namei(path)) == 0)
        return 0;
    ilock(ip);

    if (readi(ip, 0, (uint64)elf, 0, sizeof(*elf)) != sizeof(*elf))
        goto bad;
    if (elf->magic != ELF_MAGIC)
        goto bad;

    return ip;

bad:
    iunlockput(ip);
    return 0;
}

// 加载 ELF 文件的所有 LOAD 段到新页表。
// 对每个 LOAD segment:
//   - 校验 memsz/filesz/对齐
//   - 通过 uvmalloc 分配虚拟地址区间 [*sz, vaddr+memsz) 的页
//   - 通过 loadseg 将文件数据拷入对应物理页
//   - 更新 *sz 为当前内存上界
// 返回 0 表示成功，-1 表示失败。
static int load_elf(pagetable_t pagetable, struct inode *ip, struct elfhdr *elf, uint64 *sz) {
    int ph_index;
    uint ph_offset;
    struct proghdr ph;
    uint64 newsz;

    for (ph_index = 0, ph_offset = elf->phoff; ph_index < elf->phnum;
         ph_index++, ph_offset += sizeof(ph)) {
        if (readi(ip, 0, (uint64)&ph, ph_offset, sizeof(ph)) != sizeof(ph))
            return -1;
        if (ph.type != ELF_PROG_LOAD) // 跳过非 LOAD 段（如 NOTE, GNU_STACK 等）
            continue;
        if (ph.memsz < ph.filesz) // 内存需求不能小于文件数据
            return -1;
        if (ph.vaddr + ph.memsz < ph.vaddr) // 地址回绕检查
            return -1;
        if ((ph.vaddr % PGSIZE) != 0) // vaddr 必须页对齐
            return -1;
        if ((newsz = uvmalloc(pagetable, *sz, ph.vaddr + ph.memsz)) == 0)
            return -1;
        *sz = newsz;
        if (loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
            return -1;
    }

    return 0;
}

// 构造新进程的用户栈布局（从高地址到低地址）：
//
//   ┌─────────────┐ sp = sz（初始栈顶）
//   │  stack page │ ← 可用栈空间，进程运行时 sp 从此向下生长
//   ├─────────────┤ sz - PGSIZE
//   │ guard page  │ ← PTE_U 已清零，访问时触发 page fault（检测栈溢出）
//   └─────────────┘ sz - 2*PGSIZE
//
// 在 stack page 内部（从高到低）：
//   argv 字符串 → 对齐填充 → argv 指针数组 → argc（通过 a0 返回）
//
// 返回 0 表示成功，-1 表示失败。
static int setup_user_stack(pagetable_t pagetable, char **argv, uint64 sz, uint64 *newsz,
                            uint64 *sp_out, uint64 *argc_out) {
    uint64 argc, sp, stackbase, new_sz, ustack[MAXARG];

    // 分配两页：guard page + stack page
    sz = PGROUNDUP(sz);
    if ((new_sz = uvmalloc(pagetable, sz, sz + 2 * PGSIZE)) == 0)
        return -1;
    sz = new_sz;
    uvmclear(pagetable, sz - 2 * PGSIZE); // 低位页废除 PTE_U，变成 guard page
    sp = sz;
    stackbase = sp - PGSIZE; // 栈可用区间：[stackbase, sp)

    // 压入 argv 字符串（从高地址向低地址方向逐个写入）
    for (argc = 0; argv[argc]; argc++) {
        if (argc >= MAXARG)
            return -1;
        sp -= strlen(argv[argc]) + 1;
        sp -= sp % 16; // RISC-V ABI 要求 sp 保持 16 字节对齐
        if (sp < stackbase)
            return -1;
        if (copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
            return -1;
        ustack[argc] = sp; // 记录该字符串在用户栈中的地址
    }
    ustack[argc] = 0; // argv 指针数组以 NULL 结尾

    // 压入 argv 指针数组
    sp -= (argc + 1) * sizeof(uint64);
    sp -= sp % 16; // 数组起始也对齐
    if (sp < stackbase)
        return -1;
    if (copyout(pagetable, sp, (char *)ustack, (argc + 1) * sizeof(uint64)) < 0)
        return -1;

    *newsz = sz;
    *sp_out = sp;
    *argc_out = argc;
    return 0;
}

// 从 path 中提取最终文件名，保存到 proc->name。
static void save_proc_name(struct proc *p, char *path) {
    char *last, *s;

    for (last = s = path; *s; s++) {
        if (*s == '/')
            last = s + 1;
    }
    safestrcpy(p->name, last, sizeof(p->name));
}

// 原子提交新用户镜像：替换页表指针、进程大小、用户态入口地址和栈指针，
// 然后释放旧页表及其物理页。
static void commit_exec(struct proc *p, pagetable_t pagetable, uint64 sz, uint64 sp, uint64 entry,
                        uint64 oldsz) {
    pagetable_t oldpagetable = p->pagetable;

    p->pagetable = pagetable;                // 切换到新页表
    p->sz = sz;                              // 更新进程内存上界
    p->trapframe->epc = entry;               // 用户态 PC 起始地址
    p->trapframe->sp = sp;                   // 用户栈指针
    proc_freepagetable(oldpagetable, oldsz); // 释放旧页表及其物理页
}

// 将一个 ELF 程序段从 inode 加载到页表 pagetable 的虚拟地址 va 处。
// va 必须页对齐，且 [va, va+sz) 范围内的页必须已通过 uvmalloc 预先分配。
// 通过 walkaddr() 查找虚拟地址对应的物理地址，再用 readi() 将数据从磁盘读入。
// 返回 0 表示成功，-1 表示失败。
static int loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz) {
    uint offset_in_seg;
    uint chunk_bytes;
    uint64 pa;

    for (offset_in_seg = 0; offset_in_seg < sz; offset_in_seg += PGSIZE) {
        pa = walkaddr(pagetable, va + offset_in_seg);
        if (pa == 0)
            panic("loadseg: address should exist");
        chunk_bytes = sz - offset_in_seg;
        if (chunk_bytes > PGSIZE)
            chunk_bytes = PGSIZE;
        if (readi(ip, 0, (uint64)pa, offset + offset_in_seg, chunk_bytes) != chunk_bytes)
            return -1;
    }

    return 0;
}

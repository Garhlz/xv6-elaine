//
// mmap 系统调用 (mmap system calls)。
// 负责创建和取消 VMA，实际 lazy fault-in 和写回逻辑在 VM / mmap helpers 中完成。
//

#include "types.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// mmap(addr, length, prot, flags, fd, offset): 将文件映射到进程地址空间。
uint64 sys_mmap(void) {
    uint64 addr, length, offset;
    int prot, flags, fd;
    struct file *file;
    struct proc *proc = myproc();
    struct vma *vma = 0;
    uint64 rounded_len;

    if (argaddr(0, &addr) < 0 || argaddr(1, &length) < 0 || argint(2, &prot) < 0 ||
        argint(3, &flags) < 0 || argint(4, &fd) < 0 || argaddr(5, &offset) < 0) {
        return -1;
    }

    // 校验 fd 有效性
    if (length == 0 || fd < 0 || fd >= NOFILE || (file = proc->ofile[fd]) == 0)
        return -1;
    if (file->type != FD_INODE)
        return -1;
    // 校验 flags 和 prot 的合法性
    if ((flags & (MAP_SHARED | MAP_PRIVATE)) == 0)
        return -1;
    if ((flags & MAP_SHARED) && (flags & MAP_PRIVATE))
        return -1;
    if ((prot & PROT_READ) && !file->readable)
        return -1;
    if ((flags & MAP_SHARED) && (prot & PROT_WRITE) && !file->writable)
        return -1;

    // 长度向上对齐到页边界
    rounded_len = PGROUNDUP(length);
    if (proc->mmap_top + rounded_len < proc->mmap_top || proc->mmap_top + rounded_len >= TRAPFRAME)
        return -1;

    // 找到空闲 VMA 槽位
    for (int i = 0; i < NVMA; i++) {
        if (!proc->vmas[i].valid) {
            vma = &proc->vmas[i];
            break;
        }
    }
    if (vma == 0)
        return -1;

    // 填充 VMA 元数据
    vma->addr = proc->mmap_top;
    vma->length = rounded_len;
    vma->offset = offset;
    vma->prot = prot;
    vma->flags = flags;
    vma->file = filedup(file); // filedup 增加 file 引用计数
    vma->valid = 1;
    proc->mmap_top += rounded_len;

    return vma->addr;
}

// munmap(addr, length): 取消内存映射。
uint64 sys_munmap(void) {
    uint64 addr, length;

    if (argaddr(0, &addr) < 0 || argaddr(1, &length) < 0)
        return -1;
    return mmap_unmap(myproc(), addr, length);
}

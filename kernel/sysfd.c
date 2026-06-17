//
// fd 系统调用 (file-descriptor system calls)。
// 负责用户态 fd 与内核 struct file 之间的桥接，不处理路径解析和 inode 创建。
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
#include "fd_internal.h"

// 取第 arg_index 个系统调用参数作为文件描述符，返回描述符编号和对应的 `struct file` 指针。
// out_fd / out_file 可以为 0，表示调用者只关心其中一项。
static int argfd(int arg_index, int *out_fd, struct file **out_file) {
    int fd;
    struct file *file;

    if (argint(arg_index, &fd) < 0)
        return -1;
    if (fd < 0 || fd >= NOFILE || (file = myproc()->ofile[fd]) == 0)
        return -1;
    if (out_fd)
        *out_fd = fd;
    if (out_file)
        *out_file = file;
    return 0;
}

// 为 file 分配一个空闲的文件描述符编号。
// 成功时接管 file 的引用（将 file 指针存入 ofile[] 数组）。
int fdalloc(struct file *file) {
    struct proc *proc = myproc();

    for (int fd = 0; fd < NOFILE; fd++) {
        if (proc->ofile[fd] == 0) {
            proc->ofile[fd] = file;
            return fd;
        }
    }
    return -1;
}

// dup(fd): 复制文件描述符，返回新 fd。
uint64 sys_dup(void) {
    struct file *file;
    int fd;

    if (argfd(0, 0, &file) < 0)
        return -1;
    if ((fd = fdalloc(file)) < 0)
        return -1;
    filedup(file); // 增加 file 引用计数
    return fd;
}

// read(fd, buf, n): 从文件描述符 fd 读取 n 字节到用户态缓冲区 buf。
uint64 sys_read(void) {
    struct file *file;
    int nbytes;
    uint64 buf_addr;

    if (argfd(0, 0, &file) < 0 || argint(2, &nbytes) < 0 || argaddr(1, &buf_addr) < 0)
        return -1;
    return fileread(file, buf_addr, nbytes);
}

// write(fd, buf, n): 将用户态缓冲区 buf 中的 n 字节写入文件描述符 fd。
uint64 sys_write(void) {
    struct file *file;
    int nbytes;
    uint64 buf_addr;

    if (argfd(0, 0, &file) < 0 || argint(2, &nbytes) < 0 || argaddr(1, &buf_addr) < 0)
        return -1;
    return filewrite(file, buf_addr, nbytes);
}

// close(fd): 关闭文件描述符 fd。
uint64 sys_close(void) {
    int fd;
    struct file *file;

    if (argfd(0, &fd, &file) < 0)
        return -1;
    myproc()->ofile[fd] = 0;
    fileclose(file); // fileclose 负责清理引用和释放资源
    return 0;
}

// fstat(fd, st): 将文件描述符 fd 对应的文件元数据写入用户态 stat 结构体。
uint64 sys_fstat(void) {
    struct file *file;
    uint64 stat_addr; // 用户态 struct stat 指针

    if (argfd(0, 0, &file) < 0 || argaddr(1, &stat_addr) < 0)
        return -1;
    return filestat(file, stat_addr);
}

// pipe(fdarray): 创建管道，将读写两端的 fd 写入用户态数组 fdarray[2]。
uint64 sys_pipe(void) {
    uint64 fdarray_addr; // 用户态 int[2] 数组地址
    struct file *read_file, *write_file;
    int fd_read, fd_write;
    struct proc *proc = myproc();

    if (argaddr(0, &fdarray_addr) < 0)
        return -1;
    if (pipealloc(&read_file, &write_file) < 0)
        return -1;

    fd_read = -1;
    if ((fd_read = fdalloc(read_file)) < 0 || (fd_write = fdalloc(write_file)) < 0) {
        // 分配 fd 失败——回滚
        if (fd_read >= 0)
            proc->ofile[fd_read] = 0;
        fileclose(read_file);
        fileclose(write_file);
        return -1;
    }

    // 将两个 fd 编号写回用户态 fdarray[0..1]
    if (copyout(proc->pagetable, fdarray_addr, (char *)&fd_read, sizeof(fd_read)) < 0 ||
        copyout(proc->pagetable, fdarray_addr + sizeof(fd_read), (char *)&fd_write,
                sizeof(fd_write)) < 0) {
        proc->ofile[fd_read] = 0;
        proc->ofile[fd_write] = 0;
        fileclose(read_file);
        fileclose(write_file);
        return -1;
    }
    return 0;
}

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

uint64 sys_lseek(void) {
    struct file *cur_file;
    int offset;
    int whence;
    long base;
    long newoff;

    if (argfd(0, 0, &cur_file) < 0)
        return -1;
    if (argint(1, &offset) < 0)
        return -1;
    if (argint(2, &whence) < 0)
        return -1;

    if (cur_file->type != FD_INODE)
        return -1;

    ilock(cur_file->ip);
    if (cur_file->ip->type != T_FILE) {
        iunlock(cur_file->ip);
        return -1;
    }

    switch (whence) {
    case SEEK_SET:
        base = 0;
        break;
    case SEEK_CUR:
        base = cur_file->off;
        break;
    case SEEK_END:
        base = cur_file->ip->size;
        break;
    default:
        iunlock(cur_file->ip);
        return -1;
    }

    newoff = base + offset;
    if (newoff < 0 || newoff > 0x7fffffff) {
        iunlock(cur_file->ip);
        return -1;
    }

    cur_file->off = (uint)newoff;
    iunlock(cur_file->ip);
    return (uint64)newoff;
}

// dup2(oldfd, newfd): 将 oldfd 复制到 newfd，若 newfd 已打开则先关闭。
uint64 sys_dup2(void) {
    struct proc *proc;
    struct file *old_file;
    struct file *replaced_file;
    int old_fd;
    int new_fd;

    if (argfd(0, &old_fd, &old_file) < 0) {
        return -1;
    }

    if (argint(1, &new_fd) < 0) {
        return -1;
    }

    if (new_fd < 0 || new_fd >= NOFILE) {
        return -1;
    }

    if (old_fd == new_fd) {
        return new_fd;
    }

    proc = myproc();

    // 如果新文件在当前进程中已经打开，就将其关闭
    if ((replaced_file = proc->ofile[new_fd]) != 0) {
        // 先清空槽位再关闭
        proc->ofile[new_fd] = 0;
        fileclose(replaced_file);
    }

    proc->ofile[new_fd] = filedup(old_file);

    return new_fd;
}

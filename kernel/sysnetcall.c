//
// 网络相关系统调用 (network syscall handlers)。
// 这里负责 syscall 参数校验和 fd 分配，socket 的实际实现位于 sysnet.c。
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
#include "net.h"
#include "sysfile_internal.h"

// connect(raddr, lport, rport): 创建 UDP socket（net lab）。
uint64 sys_connect(void) {
    struct file *file;
    uint32 raddr, lport, rport;

    if (argint(0, (int *)&raddr) < 0 || argint(1, (int *)&lport) < 0 ||
        argint(2, (int *)&rport) < 0) {
        return -1;
    }

    if (sockalloc(&file, raddr, lport, rport) < 0)
        return -1;
    int fd = fdalloc(file);
    if (fd < 0) {
        fileclose(file);
        return -1;
    }
    return fd;
}

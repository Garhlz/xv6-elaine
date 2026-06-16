//
// ugetpid —— 通过 USYSCALL 共享页快速读取 pid。
//

#include "kernel/types.h"
#include "kernel/riscv.h"
#include "kernel/memlayout.h"
#include "user/ulib.h"

int ugetpid(void) {
    struct usyscall *u = (struct usyscall *)USYSCALL;

    return u->pid;
}

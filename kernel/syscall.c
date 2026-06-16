#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall_internal.h"
#include "defs.h"

// 内核态系统调用入口。
// 调用流程：
//   1. 用户态 ecall → trampoline → trap.c 的 usertrap()
//   2. usertrap() 检查 scause==8（U-mode ecall）后调用 syscall()
//   3. 从 a7 寄存器获取 syscall 编号，查 syscall_table[] 表调对应函数
//   4. 返回值写入 a0，如果 tracemask 中对该调用置位则打印 trace 信息
//   5. 返回 usertrap()，最终通过 sret 回到用户态
void syscall(void) {
    int num;
    struct proc *p = myproc();

    num = p->trapframe->a7;
    if (num > 0 && num < syscall_table_size && syscall_table[num].fn) {
        const struct syscall_entry *entry = &syscall_table[num];
        uint64 ret = entry->fn();
        p->trapframe->a0 = ret;
        if (num < 64 && (p->tracemask & (1ULL << num))) {
            printf("%d: syscall %s -> %d\n", p->pid, entry->name, (int)ret);
        }
    } else {
        printf("%d %s: unknown sys call %d\n", p->pid, p->name, num);
        p->trapframe->a0 = -1;
    }
}

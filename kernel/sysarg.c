#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// 从当前进程的用户态地址 addr 读取一个 uint64。
// 返回 0 表示成功，-1 表示失败（地址超出进程内存或 copyin 失败）。
int fetchaddr(uint64 addr, uint64 *ip) {
    struct proc *p = myproc();
    if (addr >= p->sz || addr + sizeof(uint64) > p->sz)
        return -1;
    if (copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
        return -1;
    return 0;
}

// 从当前进程的用户态地址 addr 读取以 NUL 结尾的字符串。
// 返回字符串长度（不含 NUL），失败返回 -1。
int fetchstr(uint64 addr, char *buf, int max) {
    struct proc *p = myproc();
    int err = copyinstr(p->pagetable, buf, addr, max);
    if (err < 0)
        return err;
    return strlen(buf);
}

// 从当前进程的 trapframe 中取出第 n 个系统调用参数的原始 uint64 值。
// RISC-V 调用约定：参数依次存放在 a0~a5 寄存器中。
static uint64 argraw(int n) {
    struct proc *p = myproc();
    switch (n) {
    case 0:
        return p->trapframe->a0;
    case 1:
        return p->trapframe->a1;
    case 2:
        return p->trapframe->a2;
    case 3:
        return p->trapframe->a3;
    case 4:
        return p->trapframe->a4;
    case 5:
        return p->trapframe->a5;
    }
    panic("argraw");
    return -1;
}

// 获取第 n 个 32 位系统调用参数。
int argint(int n, int *ip) {
    *ip = argraw(n);
    return 0;
}

// 获取第 n 个系统调用参数，作为指针（uint64 地址）。
// 此处不做合法性检查——copyin/copyout 在使用时进行校验。
int argaddr(int n, uint64 *ip) {
    *ip = argraw(n);
    return 0;
}

// 获取第 n 个字长大小的系统调用参数作为 NUL 结尾字符串。
// 将字符串内容复制到 buf 中，最多 max 字节。
// 返回字符串长度（含 NUL），失败返回 -1。
int argstr(int n, char *buf, int max) {
    uint64 addr;
    if (argaddr(n, &addr) < 0)
        return -1;
    return fetchstr(addr, buf, max);
}

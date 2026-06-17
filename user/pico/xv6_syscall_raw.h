// Raw xv6 syscall 声明 —— 用于 picolibc OS glue 层。
//
// 所有符号带 __xv6_ 前缀，避免与 picolibc（或任何标准 libc）的
// POSIX 符号名冲突（如 write、read、exit 等）。
//
// 重要：不要 include "user/user.h" 或 native user 头文件，
// 否则会把 native 的 libc/syscall 声明带入，导致原型冲突。
//
// __xv6_exit 标记为 void + noreturn，因为 exec() 后不会回到调用者。
// 其余 stub 返回 int 或 void * —— 返回值语义由 OS glue 包装。
//
// 新增 raw syscall 步骤：
//   1. 在下方添加声明
//   2. 在 usys_pico.py 的 SYSCALLS 列表中添加名称

#ifndef XV6_USER_PICO_XV6_SYSCALL_RAW_H
#define XV6_USER_PICO_XV6_SYSCALL_RAW_H

#include <stddef.h>
#include <stdint.h>

// P0: 最小集合（启动和基本 I/O 必需）
void __xv6_exit(int status) __attribute__((noreturn));
int __xv6_read(int fd, void *buf, int n);
int __xv6_write(int fd, const void *buf, int n);
int __xv6_close(int fd);
int __xv6_fstat(int fd, void *st);
void *__xv6_sbrk(int n);

// P1: 文件 API 和进程管理
int __xv6_open(const char *path, int flags);
int __xv6_unlink(const char *path);
int __xv6_getpid(void);
int __xv6_kill(int pid);

// P2: 时间、休眠和文件定位
int __xv6_sleep(int ticks);
int __xv6_uptime(void);
int __xv6_lseek(int fd, int offset, int whence);

#endif

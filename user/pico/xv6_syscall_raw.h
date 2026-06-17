#ifndef XV6_USER_PICO_XV6_SYSCALL_RAW_H
#define XV6_USER_PICO_XV6_SYSCALL_RAW_H

#include <stddef.h>
#include <stdint.h>

void __xv6_exit(int status) __attribute__((noreturn));
int __xv6_read(int fd, void *buf, int n);
int __xv6_write(int fd, const void *buf, int n);
int __xv6_close(int fd);
int __xv6_fstat(int fd, void *st);
void *__xv6_sbrk(int n);
int __xv6_open(const char *path, int flags);
int __xv6_unlink(const char *path);
int __xv6_getpid(void);
int __xv6_kill(int pid);
int __xv6_sleep(int ticks);
int __xv6_uptime(void);

#endif

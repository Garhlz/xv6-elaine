#ifndef XV6_USER_PICO_XV6_SYSCALL_RAW_H
#define XV6_USER_PICO_XV6_SYSCALL_RAW_H

#include <stddef.h>
#include <stdint.h>

int __xv6_exit(int status) __attribute__((noreturn));
int __xv6_read(int fd, void *buf, int n);
int __xv6_write(int fd, const void *buf, int n);
int __xv6_close(int fd);
int __xv6_fstat(int fd, void *st);
void *__xv6_sbrk(int n);

#endif

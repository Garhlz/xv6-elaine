#ifndef XV6_USER_USYSCALL_H
#define XV6_USER_USYSCALL_H

#include "kernel/types.h"

struct stat;
struct sysinfo;

// 系统调用声明。
// 用户程序通过调用这些 C 函数发起系统调用；每个函数由 usys.py 生成
// 一段汇编桩，将 SYS_* 编号装入 a7 后执行 ecall 指令。
int fork(void);
int exit(int) __attribute__((noreturn));
int wait(int *);
int pipe(int *);
int write(int, const void *, int);
int read(int, void *, int);
int close(int);
int kill(int);
int exec(char *, char **);
int open(const char *, int);
int mknod(const char *, short, short);
int unlink(const char *);
int fstat(int fd, struct stat *);
int link(const char *, const char *);
int mkdir(const char *);
int chdir(const char *);
int dup(int);
int getpid(void);
char *sbrk(int);
int sleep(int);
int uptime(void);
int trace(int);
int sysinfo(struct sysinfo *);
int sigalarm(int ticks, void (*handler)());
int sigreturn(void);
int symlink(const char *, const char *);
void *mmap(void *, uint64, int, int, int, uint64);
int munmap(void *, uint64);
int connect(uint32, uint16, uint16);
int pgaccess(void *base, int len, void *mask);

int lseek(int fd, int offset, int whence);

#endif

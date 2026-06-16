#ifndef XV6_USER_ULIB_H
#define XV6_USER_ULIB_H

#include "kernel/types.h"

struct stat;

// Native xv6 用户态 C 标准库子集。
// 这些声明只服务当前 xv6 用户程序；picolibc 实验链路应使用标准 libc 头。

// 字符串与内存操作。
char *strcpy(char *, const char *);
int strcmp(const char *, const char *);
uint strlen(const char *);
char *strchr(const char *, char c);
void *memset(void *, int, uint);
void *memmove(void *, const void *, int);
void *memcpy(void *, const void *, uint);
int memcmp(const void *, const void *, uint);

// 输入、文件和基础工具。
char *gets(char *, int max);
int atoi(const char *);
int stat(const char *, struct stat *);
int ugetpid(void);
int statistics(void *, int);

// 格式化输出。
void printf(const char *, ...);
void fprintf(int, const char *, ...);

// 动态内存分配。
void *malloc(uint);
void free(void *);

#endif

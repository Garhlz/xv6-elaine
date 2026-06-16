//
// ustring —— native xv6 用户态字符串和内存函数。
//
// 这些函数名和语义尽量贴近标准 C 库，但只服务 xv6 自己的用户程序。
//

#include "kernel/types.h"
#include "user/ulib.h"

// strcpy(dst, src): 将 src 指向的字符串（包括结尾 '\0'）复制到 dst。
// 返回 dst 的原始值，方便链式调用。调用者需保证 dst 缓冲区足够大。
char *strcpy(char *s, const char *t) {
    char *os;

    os = s;
    while ((*s++ = *t++) != 0)
        ;
    return os;
}

// strcmp(a, b): 按字典序比较两个字符串。
int strcmp(const char *p, const char *q) {
    while (*p && *p == *q)
        p++, q++;
    return (uchar)*p - (uchar)*q;
}

// strlen(s): 返回字符串 s 的长度（不含结尾 '\0'）。
uint strlen(const char *s) {
    int n;

    for (n = 0; s[n]; n++)
        ;
    return n;
}

// memset(dst, c, n): 将 dst 起始的 n 个字节全部设置为 c。
void *memset(void *dst, int c, uint n) {
    char *cdst = (char *)dst;
    int i;

    for (i = 0; i < n; i++)
        cdst[i] = c;
    return dst;
}

// strchr(s, c): 在字符串 s 中查找字符 c 首次出现的位置。
char *strchr(const char *s, char c) {
    for (; *s; s++)
        if (*s == c)
            return (char *)s;
    return 0;
}

// memmove(dst, src, n): 将 src 处的 n 个字节复制到 dst，支持重叠区间。
void *memmove(void *vdst, const void *vsrc, int n) {
    char *dst;
    const char *src;

    dst = vdst;
    src = vsrc;
    if (src > dst) {
        while (n-- > 0)
            *dst++ = *src++;
    } else {
        dst += n;
        src += n;
        while (n-- > 0)
            *--dst = *--src;
    }
    return vdst;
}

// memcmp(a, b, n): 逐字节比较两块内存的前 n 个字节。
int memcmp(const void *s1, const void *s2, uint n) {
    const char *p1 = s1, *p2 = s2;

    while (n-- > 0) {
        if (*p1 != *p2)
            return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

// memcpy(dst, src, n): 将 src 处的 n 个字节复制到 dst。
void *memcpy(void *dst, const void *src, uint n) {
    return memmove(dst, src, n);
}

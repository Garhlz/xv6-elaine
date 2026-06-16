//
// printf —— xv6 用户态格式化输出。
//
// 只支持有限的格式说明符：%d, %l, %x, %p, %s, %c, %%。
// 不支持字段宽度、精度、填充、浮点数等完整 printf 功能。
// 所有输出最终通过 write() 系统调用写入文件描述符。
//

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

#include <stdarg.h>

// 十六进制数字表
static char digits[] = "0123456789ABCDEF";

// putc(fd, c): 将单个字符写入 fd。
// 每次调用触发一次 write 系统调用——效率不高，但实现简单。
static void putc(int fd, char c) {
    write(fd, &c, 1);
}

// printint(fd, value, base, sgn): 以 base 进制打印整数 value 到 fd。
// 若 sgn != 0 且 value < 0，则按有符号数处理，先输出 '-'。
// 数字是反向生成的（从低位到高位存在 buf 中），最后反向输出。
static void printint(int fd, int value, int base, int sgn) {
    char buf[16];
    int digit_count, neg;
    uint abs_value;

    neg = 0;
    if (sgn && value < 0) {
        neg = 1;
        abs_value = -value; // 注意：INT_MIN 的负值在补码下会溢出，但 xv6 不依赖此边界
    } else {
        abs_value = value;
    }

    digit_count = 0;
    do {
        buf[digit_count++] = digits[abs_value % base]; // 取最低位数字
    } while ((abs_value /= base) != 0); // 去掉最低位，继续循环
    if (neg)
        buf[digit_count++] = '-'; // 有符号数且为负，补上负号

    // buf 中是反向的，从后往前输出即为正确顺序
    while (--digit_count >= 0)
        putc(fd, buf[digit_count]);
}

// printptr(fd, x): 以 "0x" + 十六进制形式打印 64 位指针/地址。
// 固定输出 16 个十六进制数字（64 位 / 每 4 位一个数字）。
static void printptr(int fd, uint64 x) {
    int shift_count;
    putc(fd, '0');
    putc(fd, 'x');
    // 每次输出最高 4 位对应的十六进制数字，然后 x 左移 4 位
    for (shift_count = 0; shift_count < (sizeof(uint64) * 2); shift_count++, x <<= 4)
        putc(fd, digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// vprintf(fd, fmt, ap): printf 的核心实现。
// 使用简单的状态机：state==0 表示正常字符，state=='%' 表示刚遇到 %。
// 支持的格式说明符：
//   %d  — 有符号十进制 int
//   %l  — 无符号十进制 uint64（"long"）
//   %x  — 无符号十六进制 int
//   %p  — 指针（64 位十六进制，带 0x 前缀）
//   %s  — 字符串（char*）；NULL 打印为 "(null)"
//   %c  — 单个字符
//   %%  — 百分号本身
//   其他 — 原样输出 % 和该字符，方便调试时发现不支持的格式
void vprintf(int fd, const char *fmt, va_list ap) {
    char *s;
    int c, i, state;

    state = 0;
    for (i = 0; fmt[i]; i++) {
        c = fmt[i] & 0xff; // 取低 8 位，避免符号扩展问题
        if (state == 0) {
            if (c == '%') {
                state = '%'; // 进入格式说明符解析状态
            } else {
                putc(fd, c); // 普通字符直接输出
            }
        } else if (state == '%') {
            // 解析 % 后面的字符，决定输出格式
            if (c == 'd') {
                printint(fd, va_arg(ap, int), 10, 1);
            } else if (c == 'l') {
                printint(fd, va_arg(ap, uint64), 10, 0);
            } else if (c == 'x') {
                printint(fd, va_arg(ap, int), 16, 0);
            } else if (c == 'p') {
                printptr(fd, va_arg(ap, uint64));
            } else if (c == 's') {
                s = va_arg(ap, char *);
                if (s == 0)
                    s = "(null)";
                while (*s != 0) {
                    putc(fd, *s);
                    s++;
                }
            } else if (c == 'c') {
                putc(fd, va_arg(ap, uint));
            } else if (c == '%') {
                putc(fd, c);
            } else {
                // 不支持的格式说明符——原样输出 % 和字符，提醒开发者
                putc(fd, '%');
                putc(fd, c);
            }
            state = 0; // 回到普通字符状态
        }
    }
}

// fprintf(fd, fmt, ...): 格式化输出到指定文件描述符。
void fprintf(int fd, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vprintf(fd, fmt, ap);
}

// printf(fmt, ...): 格式化输出到标准输出（fd=1）。
void printf(const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    vprintf(1, fmt, ap);
}

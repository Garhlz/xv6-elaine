//
// ufile —— native xv6 用户态输入、文件和基础转换 helper。
//

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

// gets(buf, max): 从标准输入（fd=0）读取一行到 buf。
char *gets(char *buf, int max) {
    int i, nread;
    char c;

    for (i = 0; i + 1 < max;) {
        nread = read(0, &c, 1);
        if (nread < 1)
            break;
        buf[i++] = c;
        if (c == '\n' || c == '\r')
            break;
    }
    buf[i] = '\0';
    return buf;
}

// stat(path, st): 通过 open + fstat 获取文件元数据。
int stat(const char *path, struct stat *st) {
    int fd;
    int result;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    result = fstat(fd, st);
    close(fd);
    return result;
}

// atoi(s): 将无符号十进制数字字符串转换为 int。
int atoi(const char *s) {
    int result;

    result = 0;
    while ('0' <= *s && *s <= '9')
        result = result * 10 + *s++ - '0';
    return result;
}

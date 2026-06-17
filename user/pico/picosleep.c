// picosleep — picolibc 版 sleep 的迁移前置 PoC。
//
// 用 picolibc 的 sleep() 实现 POSIX sleep 语义。
// 后续目标是将 native sleep.c 编译为 picolibc 变体（_pico_sleep）。

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    char *end;
    long seconds;

    if (argc != 2) {
        printf("usage: picosleep seconds\n");
        return 1;
    }

    errno = 0;
    seconds = strtol(argv[1], &end, 10); // 用 picolibc 的 strtol 解析秒数
    if (errno != 0 || *end != '\0' || seconds < 0) {
        printf("picosleep: invalid seconds: %s\n", argv[1]);
        return 1;
    }

    sleep((unsigned int)seconds);
    return 0;
}

// picoecho — picolibc 版 echo 的迁移前置 PoC。
//
// 用 picolibc 的 printf 实现 echo 的基本功能：打印命令行参数。
// 后续目标是将 native echo.c 编译为 picolibc 变体（_pico_echo）。

#include <stdio.h>

int main(int argc, char **argv) {
    int i;

    for (i = 1; i < argc; i++) {
        if (i > 1)
            printf(" ");     // 参数之间用空格分隔
        printf("%s", argv[i]);
    }
    printf("\n");

    return 0;
}

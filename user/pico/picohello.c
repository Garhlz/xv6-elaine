// picohello — picolibc 最小 PoC 程序。
//
// 验证目标：
//   - printf() 可通过 write syscall 输出到 stdout
//   - argc / argv 与 xv6 exec() ABI 正确对接
//   - malloc/free 可通过 sbrk 路径工作
//   - main() return 后经 exit() → _exit() 正确返回内核

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    printf("hello from picolibc\n");
    printf("argc = %d\n", argc);

    for (int i = 0; i < argc; i++)
        printf("argv[%d] = %s\n", i, argv[i]);

    void *p = malloc(32);
    printf("malloc(32) = %p\n", p);
    free(p);

    return 0;
}

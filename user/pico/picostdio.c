// picostdio — picolibc 标准 I/O (stdio) PoC 程序。
//
// 验证目标：
//   - fopen() / fread() / fwrite() / fclose() 正常工作
//   - stdio 缓冲层与底层 OS glue 协作正确

#include <stdio.h>

int main(int argc, char **argv) {
    const char *path;
    char buf[64];
    FILE *file;
    size_t n;

    path = argc > 1 ? argv[1] : "README";

    file = fopen(path, "r");
    if (file == NULL) {
        printf("fopen %s failed\n", path);
        return 1;
    }

    n = fread(buf, 1, sizeof(buf), file);
    if (fclose(file) != 0) {
        printf("fclose %s failed\n", path);
        return 1;
    }

    printf("fread %ld bytes from %s\n", (long)n, path);
    fwrite(buf, 1, n, stdout); // 输出到 stdout（带缓冲）
    printf("\n");

    return 0;
}

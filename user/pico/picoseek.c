// picoseek — picolibc 文件定位 (fseek/ftell/rewind) PoC 程序。
//
// 验证目标：
//   - ftell() 在 fread 后返回正确偏移
//   - rewind() 回到文件开头
//   - fseek(SEEK_SET) / fseek(SEEK_CUR) / fseek(SEEK_END) 正确工作
//   - fgetc() 读取单个字符
//   - 文件末尾 fgetc() 返回 EOF

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *path;
    char first[16];
    char again[16];
    FILE *file;
    long end;
    int c1;
    int c2;

    path = argc > 1 ? argv[1] : "README";

    file = fopen(path, "r");
    if (file == NULL) {
        printf("picoseek: fopen %s failed\n", path);
        return 1;
    }

    // 首次读取，记录位置和内容
    if (fread(first, 1, sizeof(first), file) != sizeof(first)) {
        printf("picoseek: initial fread failed\n");
        goto bad;
    }

    if (ftell(file) != (long)sizeof(first)) {
        printf("picoseek: ftell after fread failed\n");
        goto bad;
    }

    // rewind → 回到开头 → 再次读取 → 内容应该一致
    rewind(file);
    if (ftell(file) != 0) {
        printf("picoseek: rewind failed\n");
        goto bad;
    }

    if (fread(again, 1, sizeof(again), file) != sizeof(again)) {
        printf("picoseek: reread failed\n");
        goto bad;
    }

    if (memcmp(first, again, sizeof(first)) != 0) {
        printf("picoseek: reread mismatch\n");
        goto bad;
    }

    // SEEK_SET: 跳转到绝对偏移 17
    if (fseek(file, 17, SEEK_SET) != 0 || ftell(file) != 17) {
        printf("picoseek: SEEK_SET failed\n");
        goto bad;
    }

    c1 = fgetc(file);
    if (c1 < 0) {
        printf("picoseek: first fgetc failed\n");
        goto bad;
    }

    // SEEK_CUR: 后退 1 字节，再次读取应该得到相同字符
    if (fseek(file, -1, SEEK_CUR) != 0 || ftell(file) != 17) {
        printf("picoseek: SEEK_CUR failed\n");
        goto bad;
    }

    c2 = fgetc(file);
    if (c1 != c2) {
        printf("picoseek: SEEK_CUR reread mismatch\n");
        goto bad;
    }

    // SEEK_END: 跳到文件末尾
    if (fseek(file, 0, SEEK_END) != 0) {
        printf("picoseek: SEEK_END failed\n");
        goto bad;
    }

    end = ftell(file);
    if (end <= 0) {
        printf("picoseek: invalid end offset %ld\n", end);
        goto bad;
    }

    // 末尾 fgetc 应该返回 EOF
    if (fgetc(file) != EOF) {
        printf("picoseek: EOF read unexpectedly succeeded\n");
        goto bad;
    }

    if (fclose(file) != 0) {
        printf("picoseek: fclose failed\n");
        return 1;
    }

    printf("picoseek: OK\n");
    return 0;

bad:
    fclose(file);
    return 1;
}

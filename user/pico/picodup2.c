// picodup2 — picolibc dup2 PoC 程序。
//
// 验证目标：
//   - dup2(fd, fd) 保持原 fd 并直接返回
//   - dup2(file_fd, STDOUT_FILENO) 可将 printf 输出重定向到文件
//   - 恢复 stdout 后可读回并校验文件内容

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *path;
    const char *msg;
    char buf[64];
    FILE *file;
    int console_fd;
    int fd;
    size_t n;

    (void)argc;
    (void)argv;

    path = "picodup2.tmp";
    msg = "picodup2 redirect here\n";

    unlink(path);

    console_fd = open("console", O_WRONLY);
    if (console_fd < 0) {
        printf("picodup2: open console failed\n");
        return 1;
    }

    fd = open(path, O_CREAT | O_TRUNC | O_RDWR);
    if (fd < 0) {
        printf("picodup2: open output failed\n");
        close(console_fd);
        return 1;
    }

    if (dup2(fd, fd) != fd) {
        printf("picodup2: dup2 same fd failed\n");
        close(fd);
        close(console_fd);
        return 1;
    }

    if (dup2(fd, STDOUT_FILENO) != STDOUT_FILENO) {
        printf("picodup2: redirect stdout failed\n");
        close(fd);
        close(console_fd);
        return 1;
    }

    printf("%s", msg);
    fflush(stdout);
    close(fd);

    if (dup2(console_fd, STDOUT_FILENO) != STDOUT_FILENO) {
        return 1;
    }
    close(console_fd);

    file = fopen(path, "r");
    if (file == NULL) {
        printf("picodup2: reopen failed\n");
        return 1;
    }

    n = fread(buf, 1, sizeof(buf) - 1, file);
    fclose(file);
    unlink(path);

    buf[n] = '\0';
    if (strcmp(buf, msg) != 0) {
        printf("picodup2: content mismatch: %s\n", buf);
        return 1;
    }

    printf("picodup2: OK\n");
    return 0;
}

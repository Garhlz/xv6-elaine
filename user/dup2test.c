#include "kernel/fcntl.h"
#include "kernel/param.h"
#include "user/user.h"

static int streq(const char *a, const char *b) {
    return strcmp(a, b) == 0;
}

int main(int argc, char **argv) {
    char buf[64];
    const char *path = "dup2_tmp";
    const char *msg = "dup2 redirect here\n";
    int saved_stdout_fd;
    int fd;
    int new_fd;
    int n;

    (void)argc;
    (void)argv;

    unlink(path);

    saved_stdout_fd = dup(1);
    if (saved_stdout_fd < 0) {
        printf("dup2test: dup stdout failed\n");
        return 1;
    }

    fd = open(path, O_CREATE | O_RDWR | O_TRUNC);
    if (fd < 0) {
        printf("dup2test: open write file failed\n");
        close(saved_stdout_fd);
        return 1;
    }

    if (dup2(fd, fd) != fd) {
        printf("dup2test: dup2 same fd failed\n");
        close(fd);
        close(saved_stdout_fd);
        return 1;
    }

    if (dup2(-1, 1) != -1 || dup2(fd, NOFILE) != -1) {
        printf("dup2test: invalid dup2 unexpectedly succeeded\n");
        close(fd);
        close(saved_stdout_fd);
        return 1;
    }

    // 把 stdout 重定向到临时文件。
    if (dup2(fd, 1) != 1) {
        printf("dup2test: redirect stdout failed\n");
        close(fd);
        close(saved_stdout_fd);
        return 1;
    }

    printf("%s", msg);
    close(fd);

    if (dup2(saved_stdout_fd, 1) != 1) {
        return 1;
    }
    close(saved_stdout_fd);

    new_fd = open(path, O_RDONLY);
    if (new_fd < 0) {
        printf("dup2test: reopen failed\n");
        return 1;
    }

    n = read(new_fd, buf, sizeof(buf) - 1);
    close(new_fd);
    unlink(path);

    if (n < 0) {
        printf("dup2test: read failed\n");
        return 1;
    }
    buf[n] = 0;

    if (!streq(buf, msg)) {
        printf("dup2test: content mismatch: %s\n", buf);
        return 1;
    }

    printf("dup2test: OK\n");
    return 0;
}

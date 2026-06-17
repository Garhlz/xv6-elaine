// picosys — picolibc 已有 xv6 syscall glue 综合 PoC。
//
// 验证目标：
//   - dup() 复制 fd 后可写入同一文件
//   - pipe() 可在同一进程内写入并读回
//   - mkdir() / chdir() 可创建并进入目录
//   - link() / symlink() 可创建硬链接和符号链接

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int read_file(const char *path, char *buf, int size) {
    int fd;
    int n;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    n = read(fd, buf, (size_t)(size - 1));
    close(fd);
    if (n < 0)
        return -1;

    buf[n] = '\0';
    return n;
}

static int test_pipe(void) {
    char buf[16];
    int fdarray[2];
    int n;

    if (pipe(fdarray) < 0)
        return -1;

    if (write(fdarray[1], "pipe-ok", 7) != 7) {
        close(fdarray[0]);
        close(fdarray[1]);
        return -1;
    }

    close(fdarray[1]);
    n = read(fdarray[0], buf, sizeof(buf) - 1);
    close(fdarray[0]);
    if (n < 0)
        return -1;

    buf[n] = '\0';
    return strcmp(buf, "pipe-ok") == 0 ? 0 : -1;
}

static int test_paths(void) {
    char buf[64];
    int dup_fd;
    int fd;

    unlink("picosys.dir");
    if (mkdir("picosys.dir", 0777) < 0)
        return -1;
    if (chdir("picosys.dir") < 0)
        return -1;

    fd = open("base", O_CREAT | O_TRUNC | O_RDWR);
    if (fd < 0)
        goto bad;

    dup_fd = dup(fd);
    if (dup_fd < 0) {
        close(fd);
        goto bad;
    }

    if (write(dup_fd, "dup-ok\n", 7) != 7) {
        close(dup_fd);
        close(fd);
        goto bad;
    }
    close(dup_fd);
    close(fd);

    if (read_file("base", buf, sizeof(buf)) != 7 || strcmp(buf, "dup-ok\n") != 0)
        goto bad;

    if (link("base", "hard") < 0)
        goto bad;
    if (symlink("base", "sym") < 0)
        goto bad;

    if (read_file("hard", buf, sizeof(buf)) != 7 || strcmp(buf, "dup-ok\n") != 0)
        goto bad;
    if (read_file("sym", buf, sizeof(buf)) != 7 || strcmp(buf, "dup-ok\n") != 0)
        goto bad;

    unlink("sym");
    unlink("hard");
    unlink("base");
    chdir("..");
    unlink("picosys.dir");
    return 0;

bad:
    unlink("sym");
    unlink("hard");
    unlink("base");
    chdir("..");
    unlink("picosys.dir");
    return -1;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (test_pipe() < 0) {
        printf("picosys: pipe failed\n");
        return 1;
    }

    if (test_paths() < 0) {
        printf("picosys: path/fd test failed\n");
        return 1;
    }

    printf("picosys: OK\n");
    return 0;
}

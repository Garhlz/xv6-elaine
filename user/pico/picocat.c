// picocat — picolibc 版 cat 的迁移前置 PoC。
//
// 验证目标：
//   - open() / read() / write() / close() 可承载真实 cat 数据路径
//   - write() 短写时继续写完剩余内容
//   - 无参数时从 stdin 读取，带参数时依次输出文件内容

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static char buf[1024];

static int write_all(int fd, const char *data, ssize_t size) {
    ssize_t written;
    ssize_t offset;

    offset = 0;
    while (offset < size) {
        written = write(fd, data + offset, (size_t)(size - offset));
        if (written < 0)
            return -1;
        if (written == 0)
            return -1;
        offset += written;
    }

    return 0;
}

static int cat_fd(int fd) {
    ssize_t n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (write_all(STDOUT_FILENO, buf, n) < 0) {
            fprintf(stderr, "picocat: write error\n");
            return -1;
        }
    }

    if (n < 0) {
        fprintf(stderr, "picocat: read error\n");
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    int fd;
    int status;

    if (argc <= 1)
        return cat_fd(STDIN_FILENO) < 0 ? 1 : 0;

    status = 0;
    for (int i = 1; i < argc; i++) {
        fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "picocat: cannot open %s\n", argv[i]);
            status = 1;
            continue;
        }

        if (cat_fd(fd) < 0)
            status = 1;
        close(fd);
    }

    return status;
}

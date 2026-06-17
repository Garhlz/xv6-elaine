// picoio — picolibc 文件 I/O PoC 程序。
//
// 验证目标：
//   - stat() 可获取文件元数据（size）
//   - open() / read() / write() / close() 正常工作
//   - getpid() 返回 > 0 的 pid

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *path;
    struct stat st;
    char buf[64];
    int fd;
    ssize_t n;

    path = argc > 1 ? argv[1] : "README";

    printf("picoio pid = %d\n", getpid());

    // stat 验证
    if (stat(path, &st) == 0)
        printf("stat %s size = %ld\n", path, (long)st.st_size);
    else
        printf("stat %s failed\n", path);

    // open → read → close 验证
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("open %s failed\n", path);
        return 1;
    }

    n = read(fd, buf, sizeof(buf));
    close(fd);

    if (n < 0) {
        printf("read %s failed\n", path);
        return 1;
    }

    printf("read %ld bytes from %s\n", (long)n, path);
    write(1, buf, (size_t)n); // 将读取内容输出到 stdout
    printf("\n");

    return 0;
}

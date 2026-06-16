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

    if (stat(path, &st) == 0)
        printf("stat %s size = %ld\n", path, (long)st.st_size);
    else
        printf("stat %s failed\n", path);

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
    write(1, buf, (size_t)n);
    printf("\n");

    return 0;
}

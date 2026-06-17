#include "kernel/fcntl.h"
#include "user/user.h"

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

static int read_exact(int fd, char *buf, int n) {
    int got;

    got = read(fd, buf, n);
    if (got != n) {
        printf("lseektest: read got %d, want %d\n", got, n);
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    char first[16];
    char again[16];
    char byte1;
    char byte2;
    int fd;
    int n;

    (void)argc;
    (void)argv;

    fd = open("README", O_RDONLY);
    if (fd < 0) {
        printf("lseektest: open failed\n");
        return 1;
    }

    if (read_exact(fd, first, sizeof(first)) < 0)
        goto bad;

    if (lseek(fd, 0, SEEK_SET) != 0) {
        printf("lseektest: SEEK_SET 0 failed\n");
        goto bad;
    }

    if (read_exact(fd, again, sizeof(again)) < 0)
        goto bad;

    if (memcmp(first, again, sizeof(first)) != 0) {
        printf("lseektest: reread mismatch\n");
        goto bad;
    }

    if (lseek(fd, 17, SEEK_SET) != 17) {
        printf("lseektest: SEEK_SET 17 failed\n");
        goto bad;
    }

    if (read_exact(fd, &byte1, 1) < 0)
        goto bad;

    if (lseek(fd, -1, SEEK_CUR) != 17) {
        printf("lseektest: SEEK_CUR -1 failed\n");
        goto bad;
    }

    if (read_exact(fd, &byte2, 1) < 0)
        goto bad;

    if (byte1 != byte2) {
        printf("lseektest: SEEK_CUR reread mismatch\n");
        goto bad;
    }

    if (lseek(fd, 0, SEEK_END) < 0) {
        printf("lseektest: SEEK_END failed\n");
        goto bad;
    }

    n = read(fd, again, sizeof(again));
    if (n != 0) {
        printf("lseektest: read at EOF got %d\n", n);
        goto bad;
    }

    close(fd);
    printf("lseektest: OK\n");
    return 0;

bad:
    close(fd);
    return 1;
}

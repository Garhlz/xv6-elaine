#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "xv6_syscall_raw.h"

#define XV6_T_DIR 1
#define XV6_T_FILE 2
#define XV6_T_DEVICE 3

struct xv6_stat {
    int dev;
    unsigned int ino;
    short type;
    short nlink;
    uint64_t size;
};

static int set_errno(int value) {
    errno = value;
    return -1;
}

void _exit(int status) {
    __xv6_exit(status);
    for (;;)
        ;
}

ssize_t write(int fd, const void *buf, size_t n) {
    int ret;

    if (n > INT32_MAX)
        return set_errno(EINVAL);

    ret = __xv6_write(fd, buf, (int)n);
    if (ret < 0)
        return set_errno(EIO);

    return ret;
}

ssize_t read(int fd, void *buf, size_t n) {
    int ret;

    if (n > INT32_MAX)
        return set_errno(EINVAL);

    ret = __xv6_read(fd, buf, (int)n);
    if (ret < 0)
        return set_errno(EIO);

    return ret;
}

int close(int fd) {
    int ret = __xv6_close(fd);

    if (ret < 0)
        return set_errno(EBADF);

    return ret;
}

void *sbrk(intptr_t incr) {
    void *ret;

    if (incr < INT32_MIN || incr > INT32_MAX) {
        errno = ENOMEM;
        return (void *)-1;
    }

    ret = __xv6_sbrk((int)incr);
    if (ret == (void *)-1)
        errno = ENOMEM;

    return ret;
}

off_t lseek(int fd, off_t offset, int whence) {
    (void)fd;
    (void)offset;
    (void)whence;
    errno = ESPIPE;
    return (off_t)-1;
}

int isatty(int fd) {
    return fd >= 0 && fd <= 2;
}

int fstat(int fd, struct stat *st) {
    struct xv6_stat xs;

    if (st == 0)
        return set_errno(EFAULT);

    memset(st, 0, sizeof(*st));

    if (__xv6_fstat(fd, &xs) < 0)
        return set_errno(EBADF);

    st->st_ino = xs.ino;
    st->st_nlink = xs.nlink;
    st->st_size = xs.size;

    switch (xs.type) {
    case XV6_T_DIR:
        st->st_mode = S_IFDIR;
        break;
    case XV6_T_DEVICE:
        st->st_mode = S_IFCHR;
        break;
    case XV6_T_FILE:
    default:
        st->st_mode = S_IFREG;
        break;
    }

    return 0;
}

ssize_t _write(int fd, const void *buf, size_t n) {
    return write(fd, buf, n);
}

ssize_t _read(int fd, void *buf, size_t n) {
    return read(fd, buf, n);
}

int _close(int fd) {
    return close(fd);
}

int _fstat(int fd, struct stat *st) {
    return fstat(fd, st);
}

off_t _lseek(int fd, off_t offset, int whence) {
    return lseek(fd, offset, whence);
}

void *_sbrk(intptr_t incr) {
    return sbrk(incr);
}

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/types.h>
#include <time.h>

#include "xv6_syscall_raw.h"

#define XV6_T_DIR 1
#define XV6_T_FILE 2
#define XV6_T_DEVICE 3
#define XV6_T_SYMLINK 4

#define XV6_O_RDONLY 0x000
#define XV6_O_WRONLY 0x001
#define XV6_O_RDWR 0x002
#define XV6_O_CREATE 0x200
#define XV6_O_TRUNC 0x400
#define XV6_TICKS_PER_SECOND 10
#define XV6_SUPPORTED_OPEN_FLAGS (O_ACCMODE | O_CREAT | O_TRUNC)
#define XV6_SEEK_SET 0
#define XV6_SEEK_CUR 1
#define XV6_SEEK_END 2

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

static int check_fd(int fd) {
    if (fd < 0)
        return set_errno(EBADF);

    return 0;
}

static int check_io_buffer(const void *buf, size_t n) {
    if (buf == 0 && n > 0)
        return set_errno(EFAULT);
    if (n > INT32_MAX)
        return set_errno(EINVAL);

    return 0;
}

static int to_xv6_open_flags(int flags) {
    int xv6_flags;

    switch (flags & O_ACCMODE) {
    case O_WRONLY:
        xv6_flags = XV6_O_WRONLY;
        break;
    case O_RDWR:
        xv6_flags = XV6_O_RDWR;
        break;
    case O_RDONLY:
    default:
        xv6_flags = XV6_O_RDONLY;
        break;
    }

    if (flags & O_CREAT)
        xv6_flags |= XV6_O_CREATE;
    if (flags & O_TRUNC)
        xv6_flags |= XV6_O_TRUNC;

    return xv6_flags;
}

static void copy_stat(struct stat *st, const struct xv6_stat *xs) {
    memset(st, 0, sizeof(*st));
    st->st_ino = xs->ino;
    st->st_nlink = xs->nlink;
    st->st_size = xs->size;

    switch (xs->type) {
    case XV6_T_DIR:
        st->st_mode = S_IFDIR | 0755;
        break;
    case XV6_T_DEVICE:
        st->st_mode = S_IFCHR | 0666;
        break;
    case XV6_T_SYMLINK:
        st->st_mode = S_IFLNK | 0777;
        break;
    case XV6_T_FILE:
    default:
        st->st_mode = S_IFREG | 0644;
        break;
    }
}

__attribute__((noreturn)) void _exit(int status) {
    __xv6_exit(status);
    for (;;)
        ;
}

ssize_t write(int fd, const void *buf, size_t n) {
    int ret;

    if (check_fd(fd) < 0)
        return -1;
    if (check_io_buffer(buf, n) < 0)
        return -1;

    ret = __xv6_write(fd, buf, (int)n);
    if (ret < 0)
        return set_errno(EIO);

    return ret;
}

ssize_t read(int fd, void *buf, size_t n) {
    int ret;

    if (check_fd(fd) < 0)
        return -1;
    if (check_io_buffer(buf, n) < 0)
        return -1;

    ret = __xv6_read(fd, buf, (int)n);
    if (ret < 0)
        return set_errno(EIO);

    return ret;
}

int close(int fd) {
    int ret;

    if (check_fd(fd) < 0)
        return -1;

    ret = __xv6_close(fd);
    if (ret < 0)
        return set_errno(EBADF);

    return ret;
}

int open(const char *path, int flags, ...) {
    int ret;

    if (path == 0)
        return set_errno(EFAULT);
    if ((flags & ~XV6_SUPPORTED_OPEN_FLAGS) != 0)
        return set_errno(EINVAL);

    ret = __xv6_open(path, to_xv6_open_flags(flags));

    if (ret < 0)
        return set_errno(ENOENT);

    return ret;
}

int unlink(const char *path) {
    int ret;

    if (path == 0)
        return set_errno(EFAULT);

    ret = __xv6_unlink(path);

    if (ret < 0)
        return set_errno(ENOENT);

    return ret;
}

int getpid(void) {
    return __xv6_getpid();
}

int kill(int pid, int sig) {
    int ret;

    (void)sig;

    if (pid <= 0)
        return set_errno(EINVAL);

    ret = __xv6_kill(pid);
    if (ret < 0)
        return set_errno(ESRCH);

    return ret;
}

int xv6_sleep_ticks(int ticks) {
    int ret;

    if (ticks < 0)
        return set_errno(EINVAL);

    ret = __xv6_sleep(ticks);
    if (ret < 0)
        return set_errno(EINTR);

    return 0;
}

int gettimeofday(struct timeval *tv, void *tz) {
    int ticks;

    (void)tz;

    if (tv == 0)
        return set_errno(EFAULT);

    ticks = __xv6_uptime();
    if (ticks < 0)
        return set_errno(EIO);

    tv->tv_sec = ticks / XV6_TICKS_PER_SECOND;
    tv->tv_usec = (ticks % XV6_TICKS_PER_SECOND) * 100000;
    return 0;
}

clock_t times(struct tms *buf) {
    int ticks;

    if (buf == 0) {
        errno = EFAULT;
        return (clock_t)-1;
    }

    ticks = __xv6_uptime();
    if (ticks < 0) {
        errno = EIO;
        return (clock_t)-1;
    }

    memset(buf, 0, sizeof(*buf));
    return (clock_t)ticks;
}

int getentropy(void *buffer, size_t length) {
    unsigned char *bytes;
    uint32_t state;
    size_t i;

    if (length > 256)
        return set_errno(EIO);
    if (buffer == 0 && length > 0)
        return set_errno(EFAULT);

    bytes = buffer;
    state = (uint32_t)__xv6_uptime() ^ (uint32_t)(uintptr_t)buffer ^ 0x9e3779b9U;
    for (i = 0; i < length; i++) {
        state = state * 1664525U + 1013904223U;
        bytes[i] = (unsigned char)(state >> 24);
    }

    return 0;
}

__attribute__((noreturn)) void abort(void) {
    static const char msg[] = "abort\n";

    write(2, msg, sizeof(msg) - 1);
    _exit(127);
    for (;;)
        ;
}

unsigned int sleep(unsigned int seconds) {
    unsigned int ticks;

    if (seconds > INT32_MAX / XV6_TICKS_PER_SECOND)
        return seconds;

    ticks = seconds * 10;
    if (xv6_sleep_ticks((int)ticks) < 0)
        return seconds;

    return 0;
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
    (void)offset;

    if (check_fd(fd) < 0)
        return (off_t)-1;
    if (whence != XV6_SEEK_SET && whence != XV6_SEEK_CUR && whence != XV6_SEEK_END) {
        errno = EINVAL;
        return (off_t)-1;
    }

    errno = ESPIPE;
    return (off_t)-1;
}

int isatty(int fd) {
    return fd >= 0 && fd <= 2;
}

int fstat(int fd, struct stat *st) {
    struct xv6_stat xs;

    if (check_fd(fd) < 0)
        return -1;
    if (st == 0)
        return set_errno(EFAULT);

    if (__xv6_fstat(fd, &xs) < 0)
        return set_errno(EBADF);

    copy_stat(st, &xs);
    return 0;
}

int stat(const char *path, struct stat *st) {
    int fd;
    int ret;

    if (path == 0)
        return set_errno(EFAULT);
    if (st == 0)
        return set_errno(EFAULT);

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;

    ret = fstat(fd, st);
    close(fd);
    return ret;
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

int _stat(const char *path, struct stat *st) {
    return stat(path, st);
}

off_t _lseek(int fd, off_t offset, int whence) {
    return lseek(fd, offset, whence);
}

void *_sbrk(intptr_t incr) {
    return sbrk(incr);
}

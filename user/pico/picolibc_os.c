//
// picolibc_os.c —— xv6 的 picolibc OS glue 层。
//
// 这个文件是 picolibc 和 xv6 内核之间的桥梁。picolibc 期望宿主机提供
// 一组 POSIX 风格的接口（_exit, write, read, sbrk, close 等），
// 本文件将这些 POSIX 接口翻译为 xv6 的 __xv6_* raw syscall。
//
// 架构分层：
//   picolibc 应用程序 (picohello.c, picoio.c, ...)
//     → picolibc (printf, malloc, fopen, exit, ...)
//       → OS glue (本文件: _exit, write, read, sbrk, stat, ...)
//         → raw syscall stub (__xv6_write, __xv6_read, ...)
//           → ecall → 内核 (sys_write, sys_read, ...)
//
// 头文件隔离：
//   - 只 include 标准 libc/POSIX 头 + xv6_syscall_raw.h
//   - 绝对不 include "user/user.h" 或 native 头文件
//     （native 原型与 libc 原型不一致，混用会类型冲突）
//

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

// ---- xv6 内核的类型和常量映射 ----
// 这些宏将 xv6 内核的 inode 类型、open flags 等映射到本地常量，
// 避免在 OS glue 中直接 include kernel/*.h 导致类型冲突。

#define XV6_T_DIR 1     // 目录
#define XV6_T_FILE 2    // 普通文件
#define XV6_T_DEVICE 3  // 设备文件
#define XV6_T_SYMLINK 4 // 符号链接

#define XV6_O_RDONLY 0x000
#define XV6_O_WRONLY 0x001
#define XV6_O_RDWR 0x002
#define XV6_O_CREATE 0x200
#define XV6_O_TRUNC 0x400
#define XV6_TICKS_PER_SECOND 10                                  // xv6 时钟频率：每秒 10 个 tick
#define XV6_SUPPORTED_OPEN_FLAGS (O_ACCMODE | O_CREAT | O_TRUNC) // 当前支持的 open flags
#define XV6_SEEK_SET 0                                           // 从文件开头偏移
#define XV6_SEEK_CUR 1                                           // 从当前位置偏移
#define XV6_SEEK_END 2                                           // 从文件末尾偏移

#define XV6_NOFILE 16
// xv6 内核的 stat 结构体私有副本。
// 不能直接使用 kernel/stat.h，因为会和 <sys/stat.h> 冲突。
struct xv6_stat {
    int dev;
    unsigned int ino;
    short type;
    short nlink;
    uint64_t size;
};

// ---- 内部辅助函数 ----

// 设置 errno 并返回 -1。几乎所有 OS glue 函数的错误路径都走这里。
static int set_errno(int value) {
    errno = value;
    return -1;
}

// 检查文件描述符是否合法。
static int check_fd(int fd) {
    if (fd < 0 || fd >= XV6_NOFILE)
        return set_errno(EBADF);
    return 0;
}

// 检查 I/O 缓冲区参数的合法性。
// buf==0 且 n>0 表示空指针错误，n>INT32_MAX 超出 xv6 syscall 能力。
static int check_io_buffer(const void *buf, size_t n) {
    if (buf == 0 && n > 0)
        return set_errno(EFAULT);
    if (n > INT32_MAX)
        return set_errno(EINVAL);
    return 0;
}

// 将 POSIX open flags 转换为 xv6 内核的 open flags。
// POSIX: O_RDONLY / O_WRONLY / O_RDWR / O_CREAT / O_TRUNC
// xv6:  0x000    0x001    0x002     0x200    0x400
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

// 将 xv6 内核的 stat 结构转换为 POSIX struct stat。
// 传递的字段有限（ino, nlink, size, mode），其余填零。
static void copy_stat(struct stat *st, const struct xv6_stat *xs) {
    memset(st, 0, sizeof(*st));
    st->st_ino = xs->ino;
    st->st_nlink = xs->nlink;
    st->st_size = xs->size;

    // 根据 xv6 的 inode 类型设置 POSIX 文件模式
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

// ---- POSIX 接口实现 ----

// _exit(status): 终止当前进程，将 status 返回给父进程。
// 这是 exit() 的最终步骤——picolibc 的 exit() 会先做 atexit/stdio/fini，
// 然后调用 _exit 完成实际的 syscall。
// 不应该返回（noreturn），但加 for(;;) 作为防御。
__attribute__((noreturn)) void _exit(int status) {
    __xv6_exit(status);
    for (;;)
        ;
}

// write(fd, buf, n): 向文件描述符 fd 写入 n 字节。
// picolibc 的 printf/fprintf/fwrite 最终都会调到这里。
// 返回值：实际写入的字节数，失败返回 -1 并设置 errno。
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

// read(fd, buf, n): 从文件描述符 fd 读取最多 n 字节。
// 返回实际读取的字节数，失败返回 -1。
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

// close(fd): 关闭文件描述符。
int close(int fd) {
    int ret;

    if (check_fd(fd) < 0)
        return -1;

    ret = __xv6_close(fd);
    if (ret < 0)
        return set_errno(EBADF);

    return ret;
}

int dup(int fd) {
    int ret;

    if (check_fd(fd) < 0)
        return -1;

    ret = __xv6_dup(fd);
    if (ret < 0)
        return set_errno(EBADF);

    return ret;
}

int pipe(int fdarray[2]) {
    int ret;

    if (fdarray == 0)
        return set_errno(EFAULT);

    ret = __xv6_pipe(fdarray);
    if (ret < 0)
        return set_errno(EMFILE);

    return ret;
}

// open(path, flags, ...): 打开文件，返回文件描述符。
// flags 支持 O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC。
// 目前不支持 O_CREAT 时的 mode 参数，变长参数会被忽略。
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

int chdir(const char *path) {
    int ret;

    if (path == 0)
        return set_errno(EFAULT);

    ret = __xv6_chdir(path);
    if (ret < 0)
        return set_errno(ENOENT);

    return ret;
}

int mkdir(const char *path, mode_t mode) {
    int ret;

    (void)mode;

    if (path == 0)
        return set_errno(EFAULT);

    ret = __xv6_mkdir(path);
    if (ret < 0)
        return set_errno(EIO);

    return ret;
}

int dup2(int old_fd, int new_fd) {
    int ret;

    if (check_fd(old_fd) < 0) {
        return -1;
    }
    if (check_fd(new_fd) < 0) {
        return -1;
    }

    ret = __xv6_dup2(old_fd, new_fd);
    if (ret < 0) {
        return set_errno(EBADF);
    }

    return ret;
}

int link(const char *oldpath, const char *newpath) {
    int ret;

    if (oldpath == 0 || newpath == 0)
        return set_errno(EFAULT);

    ret = __xv6_link(oldpath, newpath);
    if (ret < 0)
        return set_errno(ENOENT);

    return ret;
}

int symlink(const char *target, const char *path) {
    int ret;

    if (target == 0 || path == 0)
        return set_errno(EFAULT);

    ret = __xv6_symlink(target, path);
    if (ret < 0)
        return set_errno(EIO);

    return ret;
}

// unlink(path): 删除文件。
int unlink(const char *path) {
    int ret;

    if (path == 0)
        return set_errno(EFAULT);

    ret = __xv6_unlink(path);
    if (ret < 0)
        return set_errno(ENOENT);

    return ret;
}

// getpid(): 返回当前进程的 pid。
int getpid(void) {
    return __xv6_getpid();
}

// kill(pid, sig): 向进程 pid 发送信号 sig。
// xv6 不支持 POSIX 信号机制，sig 参数被忽略。
// 调用 xv6 的 kill syscall — 它只是设置 killed 标志，目标进程在下次陷阱返回时退出。
int kill(int pid, int sig) {
    int ret;

    (void)sig; // xv6 不区分信号类型

    if (pid <= 0)
        return set_errno(EINVAL);

    ret = __xv6_kill(pid);
    if (ret < 0)
        return set_errno(ESRCH);

    return ret;
}

// ---- xv6 特有的扩展接口 ----

// xv6_sleep_ticks(ticks): 休眠指定 tick 数（1 tick = 0.1 秒）。
// 这是 xv6 特有的接口，不是 POSIX 标准。
// Picolibc 原生 sleep() 使用 seconds，内部调用此函数。
int xv6_sleep_ticks(int ticks) {
    int ret;

    if (ticks < 0)
        return set_errno(EINVAL);

    ret = __xv6_sleep(ticks);
    if (ret < 0)
        return set_errno(EINTR);

    return 0;
}

// ---- 时间与熵接口 ----

// gettimeofday(tv, tz): 获取当前时间。
// xv6 没有实时时钟，使用 uptime ticks 近似：从启动到现在的秒数。
// tz 参数被忽略（xv6 不支持时区）。
int gettimeofday(struct timeval *tv, void *tz) {
    int ticks;

    (void)tz;

    if (tv == 0)
        return set_errno(EFAULT);

    ticks = __xv6_uptime();
    if (ticks < 0)
        return set_errno(EIO);

    // 将 ticks 转换为秒和微秒
    tv->tv_sec = ticks / XV6_TICKS_PER_SECOND;
    tv->tv_usec = (ticks % XV6_TICKS_PER_SECOND) * 100000;
    return 0;
}

// times(buf): 获取进程时间信息。
// xv6 不跟踪 per-process 用户/系统时间，所以返回系统中所有字段为零。
// 返回值为自启动以来的 tick 总数（与 uptime 一致）。
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

    memset(buf, 0, sizeof(*buf)); // xv6 不跟踪进程时间
    return (clock_t)ticks;
}

// getentropy(buffer, length): 填充伪随机字节。
// PoC 级别实现：使用 LCG (Linear Congruential Generator)，
// 种子来自 uptime 和 buffer 地址的哈希组合。
// 注意：不适用于安全用途，仅提供 getentropy 的 API 存根。
int getentropy(void *buffer, size_t length) {
    unsigned char *bytes;
    uint32_t state;
    size_t i;

    if (length > 256)
        return set_errno(EIO);
    if (buffer == 0 && length > 0)
        return set_errno(EFAULT);

    bytes = buffer;
    // 混合种子：uptime ^ buffer 地址 ^ 黄金比例常数
    state = (uint32_t)__xv6_uptime() ^ (uint32_t)(uintptr_t)buffer ^ 0x9e3779b9U;
    for (i = 0; i < length; i++) {
        state = state * 1664525U + 1013904223U; // LCG 参数
        bytes[i] = (unsigned char)(state >> 24);
    }

    return 0;
}

// abort(): 异常终止当前进程。
// 向 stderr 输出 "abort\n"，然后以状态码 127 退出。
__attribute__((noreturn)) void abort(void) {
    static const char msg[] = "abort\n";

    write(2, msg, sizeof(msg) - 1);
    _exit(127);
    for (;;)
        ;
}

// ---- 休眠、堆和文件偏移接口 ----

// sleep(seconds): POSIX sleep，休眠指定秒数。
// 内部转换为 xv6 ticks（1 tick = 0.1 秒）后调用 xv6_sleep_ticks。
// 被信号中断时返回剩余秒数（xv6 不支持信号，所以总是返回 0）。
unsigned int sleep(unsigned int seconds) {
    unsigned int ticks;

    if (seconds > INT32_MAX / XV6_TICKS_PER_SECOND)
        return seconds; // 溢出保护：太大则拒绝

    ticks = seconds * XV6_TICKS_PER_SECOND;
    if (xv6_sleep_ticks((int)ticks) < 0)
        return seconds; // 被中断时返回剩余秒数

    return 0;
}

// sbrk(incr): 调整进程数据段大小（堆边界）。
// malloc/free 通过此函数向内核申请或归还内存。
// incr > 0: 扩展堆；incr < 0: 收缩；incr == 0: 返回当前堆地址。
// 失败时返回 (void *)-1 并设置 errno=ENOMEM。
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

// lseek(fd, offset, whence): 移动文件读写位置。
// whence: SEEK_SET(0)=从开头, SEEK_CUR(1)=从当前位置, SEEK_END(2)=从末尾。
// 返回新的文件偏移量，失败返回 -1。
off_t lseek(int fd, off_t offset, int whence) {
    int ret;

    if (check_fd(fd))
        return -1;
    if (offset < INT32_MIN || offset > INT32_MAX)
        return set_errno(EINVAL);
    if (whence != XV6_SEEK_SET && whence != XV6_SEEK_CUR && whence != XV6_SEEK_END)
        return set_errno(EINVAL);

    ret = __xv6_lseek(fd, (int)offset, whence);
    if (ret < 0)
        return set_errno(ESPIPE);
    return (off_t)ret;
}

// isatty(fd): 检查 fd 是否指向终端。
// xv6 没有终端概念——简单认为 fd 0/1/2 (stdin/stdout/stderr) 是 tty。
int isatty(int fd) {
    return fd >= 0 && fd <= 2;
}

// fstat(fd, st): 通过 fd 获取文件元数据。
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

// stat(path, st): 通过路径获取文件元数据。
// 实现方式：open → fstat → close。
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

// ---- picolibc 内部符号适配 ----
// picolibc 期望某些符号不带前导下划线（如 write, read），
// 但某些构建配置下可能期望带下划线的版本（如 _write, _read）。
// 下面提供一个简单重映射，确保两种符号名都能找到实现。

ssize_t _write(int fd, const void *buf, size_t n) {
    return write(fd, buf, n);
}

ssize_t _read(int fd, void *buf, size_t n) {
    return read(fd, buf, n);
}

int _close(int fd) {
    return close(fd);
}

int _dup(int fd) {
    return dup(fd);
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

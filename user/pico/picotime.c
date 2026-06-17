// picotime — picolibc 时间/熵/errno 综合 PoC 程序。
//
// 验证目标：
//   - gettimeofday() / times() 返回合理的值
//   - getentropy() 填充伪随机字节
//   - 各 OS glue 接口的错误路径正确设置并传播 errno

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <unistd.h>

int main(int argc, char **argv) {
    struct timeval tv;
    struct tms tms;
    struct stat st;
    unsigned char entropy[8];
    char byte;
    clock_t ticks;
    int fd;
    int i;

    (void)argc;
    (void)argv;

    // ---- 正常路径 ----

    if (gettimeofday(&tv, 0) < 0) {
        printf("gettimeofday failed errno=%d\n", errno);
        return 1;
    }
    printf("gettimeofday sec=%ld usec=%ld\n", (long)tv.tv_sec, (long)tv.tv_usec);

    ticks = times(&tms);
    if (ticks == (clock_t)-1) {
        printf("times failed errno=%d\n", errno);
        return 1;
    }
    printf("times ticks=%ld utime=%ld stime=%ld\n", (long)ticks, (long)tms.tms_utime,
           (long)tms.tms_stime);

    if (getentropy(entropy, sizeof(entropy)) < 0) {
        printf("getentropy failed errno=%d\n", errno);
        return 1;
    }

    printf("entropy:");
    for (i = 0; i < (int)sizeof(entropy); i++)
        printf(" %02x", entropy[i]);
    printf("\n");

    // ---- 错误路径验证 ----
    // 以下测试确认各 OS glue 接口在非法参数时返回 -1 并正确设置 errno

    // open 不存在的文件
    errno = 0;
    fd = open("missing-picotime-file", O_RDONLY);
    if (fd >= 0) {
        close(fd);
        printf("missing file unexpectedly opened\n");
        return 1;
    }
    printf("missing open errno=%d\n", errno);

    // write 到非法 fd
    errno = 0;
    if (write(-1, "x", 1) != -1) {
        printf("write bad fd unexpectedly succeeded\n");
        return 1;
    }
    printf("bad write errno=%d\n", errno);

    // read 从非法 fd
    errno = 0;
    if (read(-1, &byte, 1) != -1) {
        printf("read bad fd unexpectedly succeeded\n");
        return 1;
    }
    printf("bad read errno=%d\n", errno);

    // stat 时 path==NULL
    errno = 0;
    if (stat(0, &st) != -1) {
        printf("stat null path unexpectedly succeeded\n");
        return 1;
    }
    printf("null stat errno=%d\n", errno);

    // gettimeofday 时 tv==NULL
    errno = 0;
    if (gettimeofday(0, 0) != -1) {
        printf("gettimeofday null unexpectedly succeeded\n");
        return 1;
    }
    printf("null gettimeofday errno=%d\n", errno);

    // getentropy 时 buf==NULL 但 length>0
    errno = 0;
    if (getentropy(0, 1) != -1) {
        printf("getentropy null unexpectedly succeeded\n");
        return 1;
    }
    printf("null getentropy errno=%d\n", errno);

    // kill 非法 pid (pid=0)
    errno = 0;
    if (kill(0, 0) != -1) {
        printf("kill invalid pid unexpectedly succeeded\n");
        return 1;
    }
    printf("bad kill errno=%d\n", errno);

    // lseek 到非法 fd
    errno = 0;
    if (lseek(-1, 0, SEEK_SET) != (off_t)-1) {
        printf("lseek bad fd unexpectedly succeeded\n");
        return 1;
    }
    printf("bad lseek errno=%d\n", errno);

    return 0;
}

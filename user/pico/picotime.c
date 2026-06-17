#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/times.h>
#include <unistd.h>

int main(int argc, char **argv) {
    struct timeval tv;
    struct tms tms;
    unsigned char entropy[8];
    clock_t ticks;
    int fd;
    int i;

    (void)argc;
    (void)argv;

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

    errno = 0;
    fd = open("missing-picotime-file", O_RDONLY);
    if (fd >= 0) {
        close(fd);
        printf("missing file unexpectedly opened\n");
        return 1;
    }
    printf("missing open errno=%d\n", errno);

    return 0;
}

// picogetdents — 验证 picolibc raw getdents syscall 路径。

#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "kernel/dirent.h"
#include "kernel/stat.h"
#include "xv6_syscall_raw.h"

int main(int argc, char **argv) {
    struct xv6_dent dents[4];
    int dent_size;
    int fd;
    int n;
    int saw_readme;

    (void)argc;
    (void)argv;

    dent_size = sizeof(dents[0]);
    fd = __xv6_open(".", O_RDONLY);
    if (fd < 0) {
        printf("picogetdents: open failed\n");
        return 1;
    }

    saw_readme = 0;
    while ((n = __xv6_getdents(fd, dents, sizeof(dents))) > 0) {
        if (n % dent_size != 0) {
            printf("picogetdents: bad byte count\n");
            __xv6_close(fd);
            return 1;
        }
        for (int i = 0; i < n / dent_size; i++) {
            if (dents[i].d_reclen != dent_size || dents[i].d_ino == 0) {
                printf("picogetdents: bad dent\n");
                __xv6_close(fd);
                return 1;
            }
            if (strcmp(dents[i].d_name, "README") == 0 && dents[i].d_type == T_FILE)
                saw_readme = 1;
        }
    }
    __xv6_close(fd);

    if (n < 0 || !saw_readme) {
        printf("picogetdents: README not found\n");
        return 1;
    }

    printf("picogetdents: OK\n");
    return 0;
}

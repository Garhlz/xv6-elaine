#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

#define MAXLINE 512

int main(int argc, char **argv) {
    char buf[MAXLINE];
    char *args[MAXARG];
    int base_argc;
    int n;

    if (argc < 2) {
        fprintf(2, "usage: xargs command [args ...]\n");
        exit(1);
    }

    if (argc > MAXARG - 1) {
        fprintf(2, "xargs: too many arguments\n");
        exit(1);
    }

    base_argc = argc - 1;
    for (n = 0; n < base_argc; n++) {
        args[n] = argv[n + 1];
    }

    while (1) {
        int len = 0;
        char c;

        while ((n = read(0, &c, 1)) == 1 && c != '\n') {
            if (len + 1 >= sizeof(buf)) {
                fprintf(2, "xargs: input line too long\n");
                exit(1);
            }
            buf[len++] = c;
        }

        if (n < 0) {
            fprintf(2, "xargs: read failed\n");
            exit(1);
        }
        if (n == 0 && len == 0) {
            break;
        }

        buf[len] = '\0';
        args[base_argc] = buf;
        args[base_argc + 1] = 0;

        if (fork() == 0) {
            exec(args[0], args);
            fprintf(2, "xargs: exec %s failed\n", args[0]);
            exit(1);
        }
        wait(0);

        if (n == 0) {
            break;
        }
    }

    exit(0);
}

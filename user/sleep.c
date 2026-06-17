#ifdef PICOLIBC_USER
#include <stdio.h>
#include <stdlib.h>

#include "user/pico/xv6_pico.h"
#else
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#endif

static void print_error(const char *message) {
#ifdef PICOLIBC_USER
    fprintf(stderr, "%s", message);
#else
    fprintf(2, "%s", message);
#endif
}

int main(int argc, char **argv) {
    int ticks;

    if (argc != 2) {
        print_error("usage: sleep ticks\n");
        return 1;
    }

    ticks = atoi(argv[1]);
    if (ticks < 0) {
        print_error("sleep: ticks must be non-negative\n");
        return 1;
    }

#ifdef PICOLIBC_USER
    if (xv6_sleep_ticks(ticks) < 0) {
        print_error("sleep: interrupted\n");
        return 1;
    }
#else
    sleep(ticks);
#endif
    return 0;
}

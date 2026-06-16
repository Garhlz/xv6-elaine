#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

static void run_stage(int input_fd);

static void spawn_stage(int input_fd) {
    run_stage(input_fd);
}

static void run_stage(int input_fd) {
    int next;
    int prime;
    int p[2];

    if (read(input_fd, &prime, sizeof(prime)) != sizeof(prime)) {
        close(input_fd);
        exit(0);
    }

    printf("prime %d\n", prime);

    if (pipe(p) < 0) {
        fprintf(2, "primes: pipe failed\n");
        close(input_fd);
        exit(1);
    }

    if (fork() == 0) {
        close(p[1]);
        close(input_fd);
        spawn_stage(p[0]);
    }

    close(p[0]);
    while (read(input_fd, &next, sizeof(next)) == sizeof(next)) {
        if (next % prime != 0) {
            write(p[1], &next, sizeof(next));
        }
    }
    close(input_fd);
    close(p[1]);
    wait(0);
    exit(0);
}

int main(int argc, char **argv) {
    int i;
    int p[2];

    if (argc != 1) {
        fprintf(2, "usage: primes\n");
        exit(1);
    }

    if (pipe(p) < 0) {
        fprintf(2, "primes: pipe failed\n");
        exit(1);
    }

    if (fork() == 0) {
        close(p[1]);
        spawn_stage(p[0]);
    }

    close(p[0]);
    for (i = 2; i <= 35; i++) {
        write(p[1], &i, sizeof(i));
    }
    close(p[1]);
    wait(0);
    exit(0);
}

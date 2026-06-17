#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv) {
    char *end;
    long seconds;

    if (argc != 2) {
        printf("usage: picosleep seconds\n");
        return 1;
    }

    errno = 0;
    seconds = strtol(argv[1], &end, 10);
    if (errno != 0 || *end != '\0' || seconds < 0) {
        printf("picosleep: invalid seconds: %s\n", argv[1]);
        return 1;
    }

    sleep((unsigned int)seconds);
    return 0;
}

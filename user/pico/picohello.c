#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    printf("hello from picolibc\n");
    printf("argc = %d\n", argc);

    for (int i = 0; i < argc; i++)
        printf("argv[%d] = %s\n", i, argv[i]);

    void *p = malloc(32);
    printf("malloc(32) = %p\n", p);
    free(p);

    return 0;
}

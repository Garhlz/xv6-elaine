#include <stddef.h>
#include <stdlib.h>

typedef void (*init_func)(void);

extern init_func __preinit_array_start[];
extern init_func __preinit_array_end[];
extern init_func __init_array_start[];
extern init_func __init_array_end[];
extern int main(int argc, char **argv);

static void run_init_array(init_func *begin, init_func *end) {
    while (begin < end) {
        (*begin)();
        begin++;
    }
}

__attribute__((noreturn)) void pico_crt0_main(int argc, char **argv) {
    int status;

    run_init_array(__preinit_array_start, __preinit_array_end);
    run_init_array(__init_array_start, __init_array_end);
    status = main(argc, argv);
    exit(status);
}

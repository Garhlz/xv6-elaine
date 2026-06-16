#include <stdlib.h>

extern int main(int argc, char **argv);

__attribute__((noreturn)) void pico_crt0_main(int argc, char **argv) {
    exit(main(argc, argv));
}

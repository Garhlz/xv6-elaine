#include <stdio.h>

static int constructor_ran;
static int destructor_ran;

__attribute__((constructor)) static void before_main(void) {
    constructor_ran = 1;
    printf("constructor ran\n");
}

__attribute__((destructor)) static void after_main(void) {
    destructor_ran = 1;
    printf("destructor ran\n");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("main sees constructor_ran = %d\n", constructor_ran);
    printf("main sees destructor_ran = %d\n", destructor_ran);

    return constructor_ran == 1 && destructor_ran == 0 ? 0 : 1;
}

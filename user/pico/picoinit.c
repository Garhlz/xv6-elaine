// picoinit — picolibc constructor/destructor PoC 程序。
//
// 验证目标：
//   - __attribute__((constructor)) 在 main() 之前被调用
//   - __attribute__((destructor)) 在 exit() 期间被调用
//   - main() 返回后 destructor 按预期运行

#include <stdio.h>

// 用于在 main 中检查 constructor/destructor 是否已运行
static int constructor_ran;
static int destructor_ran;

// constructor: 由 crt0 在 main() 之前通过 init_array 调用
__attribute__((constructor)) static void before_main(void) {
    constructor_ran = 1;
    printf("constructor ran\n");
}

// destructor: 由 picolibc exit() 在 main() 返回后通过 fini_array 调用
__attribute__((destructor)) static void after_main(void) {
    destructor_ran = 1;
    printf("destructor ran\n");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("main sees constructor_ran = %d\n", constructor_ran);
    printf("main sees destructor_ran = %d\n", destructor_ran);

    // destructor 此时尚未运行（要等 main 返回后 exit() 才触发）
    return constructor_ran == 1 && destructor_ran == 0 ? 0 : 1;
}

//
// Picolibc 实验链路的 C 启动包装。
//
// 执行顺序：
//   1. 运行 .preinit_array 中的 constructor 函数（编译器/链接器生成的初始化）
//   2. 运行 .init_array  中的 constructor 函数（__attribute__((constructor))）
//   3. 调用 main(argc, argv)
//   4. 调用 exit(status) → 最终触发 _exit syscall 回到内核
//
// 与 native crt0.c 的关键区别：
//   - 使用 picolibc 提供的 exit()（支持 atexit、stdio flush、fini_array）
//   - 调用 init_array 以支持 __attribute__((constructor)) 和未来的 C++ 全局构造
//   - native crt0.c 是 exit(main(argc, argv)) 的简单模型，不做 init/fini
//

#include <stddef.h>
#include <stdlib.h>

typedef void (*init_func)(void);

// 由 linker script (user_pico.ld) 在链接时定义
extern init_func __preinit_array_start[];
extern init_func __preinit_array_end[];
extern init_func __init_array_start[];
extern init_func __init_array_end[];
extern int main(int argc, char **argv);

// 运行 init_func 数组中的所有函数。
// 数组由链接器按地址升序排列。
static void run_init_array(init_func *begin, init_func *end) {
    while (begin < end) {
        (*begin)();
        begin++;
    }
}

// pico_crt0_main: 从 _start 调用的 C 入口。
// 先运行所有 constructor，再执行 main，最后通过 picolibc 的 exit() 退出。
// 标记 noreturn — exit() 内部调用 _exit syscall 回到内核。
__attribute__((noreturn)) void pico_crt0_main(int argc, char **argv) {
    int status;

    // 运行全局构造函数（preinit 在 init 之前）
    run_init_array(__preinit_array_start, __preinit_array_end);
    run_init_array(__init_array_start, __init_array_end);

    // 执行用户程序 main 并获取返回值
    status = main(argc, argv);

    // 通过 picolibc 退出：会调用 atexit 注册的函数、刷新 stdio 缓冲区、
    // 运行 fini_array destructor，最终调用 _exit(status)
    exit(status);
}

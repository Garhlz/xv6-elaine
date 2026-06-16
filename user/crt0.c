#include "kernel/types.h"
#include "user/user.h"

extern int main(int argc, char **argv);

// 最小用户态启动包装：
// exec() 已经把 argc 放在 a0、argv 放在 a1，这里直接调用 main，
// 如果 main 返回，则把返回值作为 exit status 交回内核。
__attribute__((noreturn))
void crt0_main(int argc, char **argv) {
    exit(main(argc, argv));
}

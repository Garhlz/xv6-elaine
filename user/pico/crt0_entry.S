# Picolibc 实验链路的 ELF 入口。
# exec() 将 argc 放在 a0，argv 放在 a1，epc 指向 _start。
# _start 直接调用 C 启动函数 pico_crt0_main(argc, argv)，
# 该函数负责运行 init_array → main → exit，永不返回。
# 如果异常返回，下面 1: j 1b 作为兜底死循环防止 PC 跑飞。

.section .text
.globl _start
_start:
    call pico_crt0_main
1:  j 1b          # 兜底死循环：pico_crt0_main 不应返回

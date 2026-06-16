# 用户态 ELF 入口。
# exec() 约定：a0=argc, a1=argv, epc 指向程序 entry。
# 这里不重建栈布局，只把寄存器参数直接传给 C 侧启动包装。

.section .text
.globl _start
_start:
    call crt0_main
1:  j 1b          # crt0_main 不应该返回；兜底死循环防止跑飞

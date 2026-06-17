// xv6 扩展声明 —— 供 Picolibc 迁移版程序使用的公共头。
//
// 与 xv6_syscall_raw.h 的区别：
//   xv6_syscall_raw.h    — raw syscall stub 声明，给 OS glue 用
//   xv6_pico.h           — xv6 特有的辅助接口声明，给迁移程序用
//
// 当前只包含 xv6_sleep_ticks，后续如有 xv6 特有的工具函数可放这里。

#ifndef XV6_USER_PICO_XV6_PICO_H
#define XV6_USER_PICO_XV6_PICO_H

// 休眠指定 tick 数（每 tick = 0.1 秒）。
// 实现在 picolibc_os.c 中，包装 __xv6_sleep。
int xv6_sleep_ticks(int ticks);

#endif

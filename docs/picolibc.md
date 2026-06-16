# Picolibc Integration Plan

本文档记录 `dev/all` 分支接入 `picolibc` 的分阶段计划。目标不是一次性替换 xv6 现有全部用户态支持层，而是先建立一条**独立、可验证、可回滚**的 `picolibc` 实验链路，再逐步扩展覆盖范围。

## 1. 目标与约束

本阶段目标：

- 在不影响现有 xv6 用户程序的前提下，引入一条 `picolibc` PoC 链路。
- 让 `build/user/_picohello` 可以被打包进 `fs.img` 并在 xv6 shell 中运行。
- 先跑通：
  - `printf`
  - `malloc/free`
  - `main(argc, argv)` 返回后正常 `exit`
- 保持现有 native 用户程序和测试入口不变：
  - `cat`
  - `sh`
  - `usertests`
  - `nettests`
  - `mmaptest`
  - `make test-quick`

核心约束：

- 不直接替换当前 `ULIB`。
- 不把 `picolibc` 和当前 `ulib.o` / `printf.o` / `umalloc.o` 混链。
- 先静态链接，不做动态链接。
- 先使用 xv6 自己的 `_start` / `crt0` 模型，不直接切到 `picocrt`。
- 第一阶段不修改 `kernel/exec.c` 的 ELF 加载语义。
- 第一阶段不追求完整 POSIX，只提供 `picolibc` PoC 所需的最小 OS glue。

## 2. 当前仓库基线

当前用户态基础已经具备：

- 用户程序 ELF entry 已统一为 `_start`
- `user/crt0_entry.S` 提供入口
- `user/crt0.c` 提供 `crt0_main(argc, argv) -> exit(main(argc, argv))`
- 用户程序 `main` 签名已统一为 `int main(int argc, char **argv)`

当前 xv6 用户态 ABI：

- `exec()` 将 `argc` 放入 `a0`
- `exec()` 将 `argv` 放入 `a1`
- `exec()` 将 `epc` 设为 ELF entry
- `_start` 直接承接 `argc/argv`

这意味着第一阶段不需要重做 native 启动链，只需要为 `picolibc` 增加一条独立链接路径。

## 3. 关键官方文档

接入过程中优先参考以下 `picolibc` 官方文档：

- OS integration: <https://github.com/picolibc/picolibc/blob/main/doc/os.md>
- Build options: <https://github.com/picolibc/picolibc/blob/main/doc/build.md>
- Linking: <https://github.com/picolibc/picolibc/blob/main/doc/linking.md>
- Init / constructors: <https://github.com/picolibc/picolibc/blob/main/doc/init.md>

这些文档对当前计划最相关的点：

- `os.md`
  - `picolibc` 不内嵌 OS 支持，期望目标系统提供 POSIX 风格接口
  - 标准 I/O 和许多 libc 能力依赖外部 `read/write/close/_exit/fstat/lseek/...`
  - `malloc/free` 依赖 `sbrk`
- `build.md`
  - 可配置 `picocrt`、`malloc`、`single-thread`、TLS、global errno
  - 提供 RISC-V cross file 示例
- `linking.md`
  - 可以使用自定义 linker script
  - 如果用 `-specs=picolibc.specs`，自定义脚本应通过 GCC 的 `-Tcustom.ld`
- `init.md`
  - 若使用自定义 startup code，后续可能需要自己决定是否调用 `__libc_init_array` / `__libc_fini_array`

## 4. 总体迁移策略

推荐路线：

1. 先做一条独立的 `picolibc` 实验链路，只构建 `_picohello`
2. 用 `__xv6_*` raw syscall 包装隔离 libc 符号
3. 提供最小 OS glue，让 `printf/malloc/exit` 跑通
4. 用独立 linker script 控制 `picolibc` 用户程序 ELF
5. 跑通后再逐步扩展到简单用户程序

明确不做的事：

- 第一阶段不迁移 `sh` / `init` / `usertests`
- 第一阶段不修改 native `user/user.h` 作为 `picolibc` 公共头
- 第一阶段不追求 `fopen/fread/fwrite` 全量可用
- 第一阶段不引入完整 Unix 初始栈布局
- 第一阶段不处理 `envp` / `auxv`

## 5. 目录与边界设计

建议新增独立实验目录：

```text
user/pico/
  picohello.c
  usys_pico.pl
  xv6_syscall_raw.h
  picolibc_os.c
  user_pico.ld
```

建议先**不要**新增 `user/pico/crt0_entry.S` / `user/pico/crt0.c`。

原因：

- 仓库已经有可工作的最小 `_start` / `crt0`
- 第一阶段无需维护两套几乎相同的启动代码
- 等到需要引入 `__libc_init_array` / `__libc_fini_array` 时，再考虑为 `picolibc` 分叉专用 startup

第一阶段边界：

- native xv6 用户程序：
  - 继续使用当前 `ULIB`
- `picolibc` 实验程序：
  - 使用单独 `PICO_OBJS + libc.a + libgcc.a`

## 6. 阶段 0：硬化当前 crt0

在开始 `picolibc` 迁移前，先补两个小修：

### 6.1 `crt0_main` 标记为 `noreturn`

目标：

- 明确语义：`crt0_main()` 不应返回
- 让编译器和后续维护者都能看到启动路径约束

建议形态：

```c
__attribute__((noreturn))
void crt0_main(int argc, char **argv) {
    exit(main(argc, argv));
}
```

### 6.2 `_start` 增加 fallback loop

目标：

- 如果 `crt0_main()` 异常返回，不直接继续执行未知地址

建议形态：

```asm
.section .text
.globl _start
_start:
    call crt0_main
1:
    j 1b
```

说明：

- 这是启动代码防御性增强，不改变正常语义

## 7. 阶段 1：建立独立的 picolibc 实验链路

### 7.1 Makefile 设计目标

新增一套独立变量：

```make
PICOLIBC_EXPERIMENT ?=
PICOLIBC_PREFIX ?= $(CURDIR)/opt/picolibc-rv64-xv6
PICO_BUILD = $(UBUILD)/pico
```

并单独组织：

```make
PICO_OBJS = \
  $(UBUILD)/crt0_entry.o \
  $(UBUILD)/crt0.o \
  $(PICO_BUILD)/usys_pico.o \
  $(PICO_BUILD)/picolibc_os.o
```

关键点：

- 先复用现有 `crt0_entry.o` / `crt0.o`
- 不链接 native `ulib.o` / `printf.o` / `umalloc.o`
- 只在 `PICOLIBC_EXPERIMENT=1` 时构建 `_picohello`

### 7.2 为什么不能混链当前 ULIB

如果把 `picolibc` 和 native `ULIB` 混在一起，极易遇到：

- `printf` 重复定义
- `malloc/free` 重复定义
- `memcpy/strlen/memset` 重复定义
- `exit` / `_exit` 语义混杂
- 头文件与函数原型冲突

所以第一阶段必须严格分离：

- native 用户程序：继续 old path
- `picolibc` 实验程序：only `crt0 + raw syscall + os glue + picolibc`

## 8. 阶段 2：引入 raw syscall 层

### 8.1 动机

当前 `user/usys.pl` 生成的是 libc 友好的名字：

- `read`
- `write`
- `close`
- `exit`
- `sbrk`

这和 `picolibc` 期望提供或调用的 POSIX / libc 符号会直接冲突。

所以实验链路需要生成一套带前缀的 raw syscall：

- `__xv6_read`
- `__xv6_write`
- `__xv6_close`
- `__xv6_exit`
- `__xv6_sbrk`
- `__xv6_fstat`

### 8.2 文件设计

新增：

```text
user/pico/usys_pico.pl
user/pico/xv6_syscall_raw.h
```

其中：

- `usys_pico.pl`
  - 参考现有 `user/usys.pl`
  - 但生成 `__xv6_*` 符号
- `xv6_syscall_raw.h`
  - 只声明 `__xv6_*` raw syscall
  - 不引入 `user/user.h`

### 8.3 第一阶段需要的 raw syscall

P0 最小集合：

- `__xv6_exit`
- `__xv6_read`
- `__xv6_write`
- `__xv6_close`
- `__xv6_fstat`
- `__xv6_sbrk`

P1 再补：

- `__xv6_open`
- `__xv6_unlink`
- `__xv6_getpid`
- `__xv6_kill`

## 9. 阶段 3：实现最小 OS glue

新增：

```text
user/pico/picolibc_os.c
```

### 9.1 头文件约束

这一阶段最重要的规则：

- `picolibc` 实验链路的 `.c` 文件**不要 include `user/user.h`**
- `picolibc_os.c` 只包含：
  - 标准 libc / POSIX 头
  - `xv6_syscall_raw.h`

原因：

- native `user/user.h` 里的 `read/write/sbrk` 原型和标准 libc 原型不一致
- 混用会立刻造成类型冲突

### 9.2 第一阶段只实现 P0 能力

P0 必需接口：

- `_exit`
- `read`
- `write`
- `close`
- `fstat`
- `isatty`
- `lseek`
- `sbrk`

目标：

- 跑通 `printf`
- 跑通 `malloc/free`
- 跑通 `exit`

### 9.3 接口语义建议

- `_exit(status)`
  - 直接调用 `__xv6_exit(status)`
  - 不返回
- `write/read/close`
  - 直接转到底层 `__xv6_*`
  - 失败时设置最小 `errno`
- `sbrk`
  - 直接包装 xv6 `sbrk`
  - 失败时返回 `(void *) -1`
- `lseek`
  - xv6 当前无真正 seek 语义时，可先返回 `ESPIPE`
- `isatty`
  - 对 `0/1/2` 返回 true
- `fstat`
  - 第一阶段只满足 stdio 最小需要
  - 至少确保 `stdout/stderr` 不崩

### 9.4 `struct stat` 风险

不要在同一编译单元中直接混用：

- `kernel/stat.h`
- `<sys/stat.h>`

建议：

- 在 `picolibc_os.c` 中维护一个私有 `struct xv6_stat`
- 后续由 xv6 ABI 结构手动转换到 libc `struct stat`

第一阶段如果只需支撑 `printf`，可以先做最小填充，再在第二阶段补准确转换。

## 10. 阶段 4：Picolibc 专用 linker script

新增：

```text
user/pico/user_pico.ld
```

第一阶段目标：

- `ENTRY(_start)`
- 明确 `.text/.rodata/.data/.bss`
- 导出 `end`
- 产出静态 ELF
- 保持当前 xv6 `exec()` 可接受的 LOAD segment 布局

第一阶段**不急着**引入完整 constructor / destructor 支持，只需要为后续保留扩展点即可。

需要重点检查：

- `readelf -h`
- `readelf -l`
- `nm -u`

必须满足：

- entry 指向 `_start`
- 没有 `INTERP`
- 没有动态链接依赖
- LOAD segment 对当前 loader 友好

如果 ELF 被当前 `exec()` 拒绝，优先调整：

- linker script
- 链接参数

而不是第一时间改 `kernel/exec.c`。

## 11. 阶段 5：构建外部 picolibc

### 11.1 构建工具前置

需要确认宿主机具备：

- `riscv64-unknown-elf-gcc`
- `riscv64-unknown-elf-ar`
- `riscv64-unknown-elf-as`
- `riscv64-unknown-elf-ld`
- `meson`
- `ninja`

### 11.2 第一阶段建议配置

结合 `build.md`，第一阶段建议优先尝试：

- `picocrt=false`
- `multilib=false`
- `tests=false`
- `single-thread=true`
- `thread-local-storage=false`
- `newlib-global-errno=true`

原因：

- xv6 已有自己的 `_start`
- 第一阶段不需要 `picolibc` 自带启动对象
- 先避免 TLS / reentrancy 问题
- 先用单一全局 `errno`
- 先禁用复杂锁语义

对 `malloc`：

- 保留 `enable-malloc=true`
- 先让 `picolibc malloc` 通过 `sbrk` 工作

### 11.3 cross file

建议参考 `build.md` 中的 RISC-V cross file 示例，单独维护一份：

```text
toolchain/cross-riscv64-xv6.txt
```

并尽量保持与 xv6 当前用户程序编译参数一致，例如：

- `-nostdlib`
- `-ffreestanding`
- `-fno-common`
- `-mcmodel=medany`
- `-mno-relax`

是否需要：

- `-march=rv64gc`
- `-mabi=lp64`

以本地工具链实际配置为准。

## 12. 阶段 6：跑通第一个程序 `_picohello`

建议新增：

```text
user/pico/picohello.c
```

第一版只验证：

- `printf`
- `argc/argv`
- `malloc/free`
- `return 0`

建议形态：

```c
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
```

预期验收：

- `picohello` 能执行
- `printf` 输出正常
- `malloc/free` 不崩
- 返回 shell

## 13. 阶段 7：测试与排错流程

### 13.1 构建与回归

必须保证：

- `make build`
- `make image`
- `make test-quick`

在 `PICOLIBC_EXPERIMENT` 关闭时保持原行为不变。

### 13.2 Picolibc 实验目标

建议目标：

- `make PICOLIBC_EXPERIMENT=1 build`
- `make PICOLIBC_EXPERIMENT=1 image`

然后进入 xv6 手工运行：

- `picohello`
- `picohello a b c`

### 13.3 ELF 检查

建议固定检查：

- `riscv64-unknown-elf-readelf -h build/user/_picohello`
- `riscv64-unknown-elf-readelf -l build/user/_picohello`
- `riscv64-unknown-elf-nm -u build/user/_picohello`
- `riscv64-unknown-elf-objdump -d build/user/_picohello`

### 13.4 常见失败分类

1. 符号冲突
   - 原因：误把 native `ULIB` 混进 picolibc 目标
   - 处理：只保留 `crt0 + raw syscall + os glue + libc.a + libgcc.a`

2. `printf` 无输出
   - 先检查 `write()` 是否实现
   - 再检查 `stdout/stderr` 路径是否可用

3. `malloc` 崩溃
   - 先检查 `sbrk` 包装
   - 再检查是否误链接 `umalloc.o`

4. ELF 被 `exec()` 拒绝
   - 先查 `readelf -l`
   - 优先调 linker script / 链接参数

5. TLS / errno 相关 undefined symbol
   - 回头检查 `thread-local-storage` / `newlib-global-errno` 配置

## 14. 阶段 8：逐步扩大覆盖面

只有在 `_picohello` 稳定后，才考虑继续扩展。

### 8.1 第二阶段扩展 OS glue

补 P1 接口：

- `open`
- `stat`
- `unlink`
- `getpid`
- `kill`

目标：

- 尝试简单文件 I/O
- 逐步支持 `fopen` / `fread` / `fclose`

### 8.2 第三阶段再考虑 init/fini

当确实需要 constructor / destructor，或 `picolibc` 某些功能依赖初始化路径时，再考虑：

- 为 `picolibc` 分叉专用 `crt0`
- 调用 `__libc_init_array`
- 调用 `__libc_fini_array`
- 在 linker script 中补齐：
  - `__preinit_array_start/end`
  - `__init_array_start/end`
  - `__fini_array_start/end`

### 8.3 程序迁移顺序

建议顺序：

1. `picohello`
2. `echo`
3. `sleep`
4. `cat`
5. `wc`
6. `ls`
7. `grep`

暂不优先：

- `init`
- `sh`
- `usertests`
- `nettests`
- `mmaptest`

原因：

- 这些程序要么是系统启动关键路径
- 要么覆盖面太广
- 要么依赖额外资源与语义，不适合做 libc PoC 首批迁移

## 15. 分阶段提交建议

建议按下面的提交粒度推进：

### Commit 1

`refactor(user): harden crt0 entry`

内容：

- `_start` 增加 fallback loop
- `crt0_main` 标记 `noreturn`

### Commit 2

`build(user): add picolibc experiment scaffolding`

内容：

- 新增 `user/pico/`
- 新增 `picohello.c`
- 新增 `user_pico.ld`
- Makefile 增加 `PICOLIBC_EXPERIMENT` 链路

### Commit 3

`user: add raw syscall stubs for libc integration`

内容：

- 新增 `usys_pico.pl`
- 新增 `xv6_syscall_raw.h`
- 生成 `__xv6_*` raw syscall

### Commit 4

`libc: add picolibc OS glue for xv6`

内容：

- 新增 `picolibc_os.c`
- 跑通 `_exit/read/write/close/fstat/isatty/lseek/sbrk`

### Commit 5

`build(user): link picohello with picolibc`

内容：

- Makefile 接入外部 `picolibc` 产物
- `_picohello` 成功链接

### Commit 6

`docs: record picolibc integration status`

内容：

- 记录当前可用接口
- 记录已知限制
- 记录最小验证方式

## 16. 第一阶段完成标准

认为第一阶段完成，需要同时满足：

1. 默认 native 用户程序和测试入口不受影响
2. `make PICOLIBC_EXPERIMENT=1 build` 能生成 `_picohello`
3. `_picohello` 的 `readelf/nm` 检查通过
4. `_picohello` 可在 xv6 shell 中运行
5. `printf` 正常输出
6. `malloc/free` 正常工作
7. `main` 返回后能正确 `exit`

## 17. 后续方向

在第一阶段完成后，再继续做：

- 更准确的 `fstat/stat` 转换
- 文件 API 支持
- `fopen/fread/fwrite/fclose`
- `__libc_init_array` / `__libc_fini_array`
- 简单 native 用户程序迁移
- 评估是否需要更标准的 Unix 初始栈布局

一句话路线图：

先不替换系统 libc；先做独立的 `picolibc _picohello` 链路；用 `__xv6_*` raw syscall 隔离符号；用最小 OS glue 跑通 `printf/malloc/exit`；确认稳定后，再迁移简单用户程序。

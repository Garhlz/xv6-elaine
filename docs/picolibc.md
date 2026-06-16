# Picolibc 接入记录

本文档跟踪 `dev/all` 分支接入 `picolibc` 的进度和计划。与 `lab-migration-plan.md`（历史记录）和 `TODO.md`（工程路线）不同的是，本文件聚焦一条具体技术链路的分阶段落地。当前已完成阶段 0/1/2（crt0 硬化、scaffold、raw syscall），阶段 3/4/5 待推进，阶段 6（picohello）依赖阶段 3 和阶段 5 完成后才能按 xv6 syscall 路径稳定运行。

## 1. 当前基线

### 1.1 已有基础设施

- `_start` → `crt0_main()` → `main(argc, argv)` → `exit(status)` — 已落地
- `user/crt0_entry.S` + `user/crt0.c` — 已就绪，含兜底死循环和 `noreturn` 标注
- `main` 签名统一为 `int main(int argc, char **argv)` — 已完成
- ulibc 模块化拆分（`ustring.c` / `ufile.c` / `ugetpid.c` / `ulib.h`）— 已完成
- `usys.py` 替代 `usys.pl` — 已完成
- `PICOLIBC_EXPERIMENT=1` 构建门控 — 已就绪
- `user/pico/` 独立实验目录 — 已创建
- `picohello` 链接规则和 linker script 已就绪，依赖阶段 3（OS glue）和阶段 5（构建 picolibc）完成后验证

### 1.2 当前 xv6 用户态 ABI

- `exec()` 将 `argc` 放入 `a0`，`argv` 放入 `a1`
- `exec()` 将 `epc` 设为 ELF entry
- `_start` 直接承接 `argc/argv`

第一阶段不需要重做 native 启动链，只需为 `picolibc` 增加独立链接路径。

### 1.3 关键官方文档

- OS integration: <https://github.com/picolibc/picolibc/blob/main/doc/os.md>
- Build options: <https://github.com/picolibc/picolibc/blob/main/doc/build.md>
- Linking: <https://github.com/picolibc/picolibc/blob/main/doc/linking.md>
- Init / constructors: <https://github.com/picolibc/picolibc/blob/main/doc/init.md>

## 2. 目标与约束

### 2.1 第一阶段目标

- 在不影响现有 xv6 用户程序的前提下，引入一条 `picolibc` PoC 链路
- `build/user/_picohello` 可被打包进 `fs.img` 并在 xv6 shell 中运行
- 先跑通 `printf`、`malloc/free`、`main(argc, argv)` 返回后正常 `exit`
- 保持现有 native 用户程序不变（`cat`、`sh`、`usertests`、`nettests`、`mmaptest`、`make test-quick` 等）

### 2.2 核心约束

- 不直接替换当前 `ULIB`
- 不把 `picolibc` 和 native `ulib.o` / `printf.o` / `umalloc.o` 混链
- 先静态链接，不做动态链接
- 先用 xv6 自己的 `_start` / `crt0` 模型，不直接切到 `picocrt`
- 第一阶段不修改 `kernel/exec.c` 的 ELF 加载语义
- 第一阶段不追求完整 POSIX，只提供 `picolibc` PoC 所需的最小 OS glue

### 2.3 明确不做的事

- 第一阶段不迁移 `sh` / `init` / `usertests`
- 第一阶段不修改 native `user/user.h` 作为 `picolibc` 公共头
- 第一阶段不追求 `fopen/fread/fwrite` 全量可用
- 第一阶段不引入完整 Unix 初始栈布局
- 第一阶段不处理 `envp` / `auxv`

### 2.4 整体路线

先不替换系统 libc，先做独立的 `picolibc _picohello` 链路；用 `__xv6_*` raw syscall 隔离符号；用最小 OS glue 跑通 `printf/malloc/exit`；确认稳定后，再迁移简单用户程序。

```text
crt0 硬化 → scaffold → raw syscall → OS glue → linker script → 构建 picolibc → picohello → 扩展程序 → init/fini
```

## 3. 目录与边界设计

```text
user/pico/
  crt0_entry.S          # picolibc 专用启动入口
  crt0.c                # picolibc 专用 C 启动包装
  picohello.c           # PoC 测试程序
  usys_pico.py          # 生成 __xv6_* raw syscall stub
  xv6_syscall_raw.h     # __xv6_* raw syscall 声明
  picolibc_os.c         # 最小 OS glue（_write/_read/_sbrk/_exit 等）
  user_pico.ld          # picolibc 专用 linker script
```

边界隔离：

- native xv6 用户程序 → 继续使用 `XV6_ULIB`
- `picolibc` 实验程序 → 只用 `PICO_OBJS + libc.a + libgcc.a`
- `picolibc` 启动代码独立于 native `crt0`，方便后续接入 `__libc_init_array` / `__libc_fini_array`

## 4. 阶段 0：硬化 crt0（已完成）

- [x] `crt0_main` 标记 `__attribute__((noreturn))`，明确语义
- [x] `_start` 增加 `1: j 1b` 兜底死循环，防止异常返回时 PC 跑飞

- 涉及模块：`user/crt0.c`、`user/crt0_entry.S`。
- 验证方式：`make build && make image && make test-quick`。

## 5. 阶段 1：建立独立 picolibc 实验链路（已完成）

依赖：阶段 0。

### 5.1 Makefile 设计

```makefile
PICOLIBC_EXPERIMENT ?=
PICOLIBC_PREFIX ?= $(CURDIR)/opt/picolibc-rv64-xv6
PICO_BUILD = $(UBUILD)/pico

PICO_OBJS = \
  $(PICO_BUILD)/crt0_entry.o \
  $(PICO_BUILD)/crt0.o \
  $(PICO_BUILD)/usys_pico.o
```

关键点：

- 不链接 native `ulib.o` / `printf.o` / `umalloc.o`
- 只在 `PICOLIBC_EXPERIMENT=1` 时构建 `_picohello`
- 第一阶段只建立 build/link scaffold

### 5.2 为什么不能混链

混链 native `ULIB` 和 `picolibc` 会导致：

- `printf` / `malloc` / `free` / `memcpy` / `strlen` / `memset` 重复定义
- `exit` / `_exit` 语义混杂
- 头文件与函数原型冲突

所以两条链路从第一天就必须严格分离。

- 涉及模块：`user/pico/`、`Makefile`。
- 验证方式：`make build` 在 `PICOLIBC_EXPERIMENT` 关闭/开启时均正常，`make image && make qemu` 后 native 程序不受影响。

## 6. 阶段 2：raw syscall 层（已完成）

依赖：阶段 1。

### 6.1 动机

native `user/usys.py` 生成的符号名（`read`、`write`、`close` 等）是 libc 友好的，和 `picolibc` 期望提供/调用的 POSIX/libc 符号直接冲突。实验链路需要生成带前缀的 raw syscall：`__xv6_read`、`__xv6_write`、`__xv6_exit`、`__xv6_sbrk` 等。

### 6.2 文件

- `user/pico/usys_pico.py` — 参考 `user/usys.py`，生成 `__xv6_*` 符号
- `user/pico/xv6_syscall_raw.h` — 只声明 `__xv6_*` raw syscall，不引入 `user/user.h`；`__xv6_exit` 返回类型为 `void`（noreturn），其余 stub 返回 `int`

### 6.3 Raw syscall 集合

P0（最小集合）：`__xv6_exit`、`__xv6_read`、`__xv6_write`、`__xv6_close`、`__xv6_fstat`、`__xv6_sbrk`

P1（按需补充）：`__xv6_open`、`__xv6_unlink`、`__xv6_getpid`、`__xv6_kill`

- 涉及模块：`user/pico/usys_pico.py`、`user/pico/xv6_syscall_raw.h`。
- 验证方式：`nm build/user/pico/usys_pico.o | grep __xv6` 确认 `__xv6_*` 符号均生成。

## 7. 阶段 3：最小 OS glue

依赖：阶段 2。与阶段 4（linker script）和阶段 5（构建 picolibc）可并行推进。

### 7.1 设计

新增 `user/pico/picolibc_os.c`，实现 `picolibc` 所需的 POSIX 风格接口包装。

头文件约束：

- **不要 include `user/user.h`**（native 原型与 libc 原型不一致，混用会类型冲突）
- 只 include 标准 libc/POSIX 头 + `xv6_syscall_raw.h`

### 7.2 P0 必需接口

- `_exit(status)` — 直接调 `__xv6_exit(status)`，不返回
- `write(fd, buf, n)` — 直接转 `__xv6_write`，失败时设置最小 `errno`
- `read(fd, buf, n)` — 同上
- `close(fd)` — 直接转 `__xv6_close`
- `sbrk(n)` — 包装 xv6 `sbrk`，失败返回 `(void *)-1`
- `lseek(fd, offset, whence)` — xv6 无真正 seek，先返回 `ESPIPE`
- `isatty(fd)` — 对 `0/1/2` 返回 true
- `fstat(fd, buf)` — 第一阶段满足 stdio 最小需要，至少不崩

目标：跑通 `printf`、`malloc/free`、`exit`。

### 7.3 `struct stat` 风险

不要在同一编译单元中混用 `kernel/stat.h` 和 `<sys/stat.h>`。

策略：在 `picolibc_os.c` 中维护私有 `struct xv6_stat`，由 xv6 ABI 结构手动转换到 libc `struct stat`。第一阶段如只需支撑 `printf`，可先做最小填充。

- 涉及模块：`user/pico/picolibc_os.c`、`user/pico/xv6_syscall_raw.h`。
- 验证方式：编译通过，链接 `_picohello` 时无 `_write` / `_read` / `_sbrk` / `_exit` undefined symbol。

## 8. 阶段 4：Picolibc 专用 linker script

依赖：阶段 1。与阶段 3（OS glue）和阶段 5（构建 picolibc）可并行推进。

新增 `user/pico/user_pico.ld`。

第一阶段目标：

- `ENTRY(_start)`
- 明确 `.text` / `.rodata` / `.data` / `.bss`
- 导出 `end`
- 产出静态 ELF
- 保持当前 xv6 `exec()` 可接受的 LOAD segment 布局

- 涉及模块：`user/pico/user_pico.ld`、`Makefile`。
- 验证方式：
  ```bash
  riscv64-unknown-elf-readelf -h build/user/_picohello   # entry 指向 _start，无 INTERP
  riscv64-unknown-elf-readelf -l build/user/_picohello   # LOAD segment 合理
  riscv64-unknown-elf-nm -u build/user/_picohello        # 无动态链接依赖
  ```

如果 ELF 被 `exec()` 拒绝，优先调整 linker script / 链接参数，不首先修改 `kernel/exec.c`。

## 9. 阶段 5：构建外部 picolibc

依赖：宿主机 riscv64 工具链 + meson/ninja。与阶段 3（OS glue）和阶段 4（linker script）可并行推进。

### 9.1 构建工具前置

需要确认宿主机具备：`riscv64-unknown-elf-gcc` / `ar` / `as` / `ld`，以及 `meson`、`ninja`。

### 9.2 配置选项

结合 `build.md`，第一阶段配置：

- `picocrt=false` — xv6 已有自己的 `_start`
- `multilib=false` — 只需要单一 ABI
- `tests=false` — 不需要 picolibc 自带测试
- `single-thread=true` — xv6 用户态单线程
- `thread-local-storage=false` — 先避免 TLS 问题
- `newlib-global-errno=true` — 先用单一全局 `errno`
- `enable-malloc=true` — 让 picolibc malloc 通过 `sbrk` 工作

### 9.3 Cross file

维护一份 `toolchain/cross-riscv64-xv6.txt`，参考 `build.md` 中 RISC-V cross file 示例。编译选项尽量与 xv6 当前用户程序一致：

- `-nostdlib`、`-ffreestanding`、`-fno-common`
- `-mcmodel=medany`、`-mno-relax`
- `-march=rv64gc`、`-mabi=lp64`（以本地工具链实际配置为准）

- 涉及模块：`toolchain/cross-riscv64-xv6.txt`、`Makefile`。
- 验证方式：`make PICOLIBC_EXPERIMENT=1 build` 可通过 `_picohello` 链接，无 libc symbol undefined 错误。

## 10. 阶段 6：跑通 picohello

依赖：阶段 0/1/2/3/4/5。阶段 3（OS glue）未完成时 picolibc 的 `exit()` 无法通过 `_exit` syscall 返回内核，不宜宣称"已跑通"。

文件：`user/pico/picohello.c`

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

- 涉及模块：`user/pico/picohello.c`。
- 验证方式：
  - `picohello` 可在 xv6 shell 中执行
  - `printf` 输出正常
  - `malloc/free` 不崩
  - `main` 返回后正确 `exit`，回到 shell

## 11. 阶段 7：测试与排错

依赖：阶段 6。

### 11.1 回归保护

`PICOLIBC_EXPERIMENT` 关闭时，以下命令必须保持原行为不变：

```bash
make build && make image && make test-quick
```

### 11.2 Picolibc 构建与运行

```bash
make PICOLIBC_EXPERIMENT=1 build
make PICOLIBC_EXPERIMENT=1 image
make qemu
# xv6 shell: picohello a b c
```

### 11.3 ELF 检查

```bash
riscv64-unknown-elf-readelf -h build/user/_picohello
riscv64-unknown-elf-readelf -l build/user/_picohello
riscv64-unknown-elf-nm -u build/user/_picohello
riscv64-unknown-elf-objdump -d build/user/_picohello
```

- 验证方式：全部命令输出符合预期。

### 11.4 常见失败速查

- **符号冲突（重复定义）** — 原因：误把 native `ULIB` 混进 picolibc 目标。处理：只保留 `crt0 + raw syscall + os glue + libc.a + libgcc.a`
- **`printf` 无输出** — 原因：`write()` 未实现或 stdout 路径不通。处理：检查 OS glue 中 `write`/`isatty`
- **`malloc` 崩溃** — 原因：`sbrk` 包装有问题或误链接 `umalloc.o`。处理：检查 `sbrk` 包装和链接顺序
- **ELF 被 `exec()` 拒绝** — 原因：LOAD segment 不兼容。处理：先查 `readelf -l`，优先调 linker script
- **TLS / errno undefined symbol** — 原因：配置不匹配。处理：检查 `thread-local-storage` / `newlib-global-errno`

## 12. 阶段 8：逐步扩展

依赖：阶段 6 稳定通过后。

### 12.1 第二阶段：扩展 OS glue

补 P1 接口：`open`、`stat`、`unlink`、`getpid`、`kill`。

- 目标：支撑简单文件 I/O（`fopen` / `fread` / `fclose`）
- 验证方式：编译通过，新增接口无 undefined symbol

### 12.2 第三阶段：init/fini

当 `picolibc` 某些功能依赖初始化路径时，再考虑：

- 为 `picolibc` 分叉专用 `crt0`，调用 `__libc_init_array` / `__libc_fini_array`
- 在 linker script 中补齐 `__preinit_array_start/end`、`__init_array_start/end`、`__fini_array_start/end`

### 12.3 程序迁移顺序

优先：`picohello` → `echo` → `sleep` → `cat` → `wc` → `ls` → `grep`

暂不优先：`init`、`sh`、`usertests`、`nettests`、`mmaptest`（系统启动关键路径或覆盖面太广，不适合做 libc PoC 首批迁移）。

## 13. 提交建议

- **Commit 1**: `refactor(user): harden crt0 entry` — `_start` fallback loop + `crt0_main` noreturn
- **Commit 2**: `build(user): add picolibc experiment scaffolding` — 新增 `user/pico/`、`picohello.c`、`user_pico.ld`、Makefile 门控
- **Commit 3**: `user: add raw syscall stubs for libc integration` — 新增 `usys_pico.py`、`xv6_syscall_raw.h`，生成 `__xv6_*`
- **Commit 4**: `libc: add picolibc OS glue for xv6` — 新增 `picolibc_os.c`，跑通 P0 OS glue 接口
- **Commit 5**: `build(user): link picohello with picolibc` — Makefile 接入外部 picolibc 产物，`_picohello` 成功链接
- **Commit 6**: `docs: record picolibc integration status` — 记录当前可用接口、已知限制、最小验证方式

## 14. 第一阶段完成标准

全部满足以下条件视为第一阶段完成：

1. 默认 native 用户程序和测试入口不受影响
2. `make PICOLIBC_EXPERIMENT=1 build` 能生成 `_picohello`
3. `_picohello` 的 `readelf` / `nm` 检查通过
4. `_picohello` 可在 xv6 shell 中运行
5. `printf` 正常输出
6. `malloc/free` 正常工作
7. `main` 返回后能正确 `exit`

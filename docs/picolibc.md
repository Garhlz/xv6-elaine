# Picolibc 接入记录

本文档跟踪 `dev/all` 分支接入 `picolibc` 的进度和计划。与 `lab-migration-plan.md`（历史记录）和 `TODO.md`（工程路线）不同的是，本文件聚焦一条具体技术链路的分阶段落地。当前已完成阶段 0/1/2/3/4/5/6，并推进了阶段 8：`picohello` 已在 xv6 shell 中验证 `printf`、`argc/argv`、`malloc/free` 和 `exit` 路径；`picoio` 已验证 `open/stat/read/write/close/getpid` 路径；`picostdio` 已验证 `fopen/fread/fwrite/fclose` 路径；`picoinit` 已验证 constructor/destructor；`picoecho` 和 `picosleep` 已验证简单程序迁移链路；`picotime` 已验证 time、entropy 和 errno PoC；`_pico_echo` / `_pico_sleep` 已验证真实 native 源码的 Picolibc 变体构建。

## 1. 当前基线

### 1.1 已有基础设施

- `_start` → `crt0_main()` → `main(argc, argv)` → `exit(status)` — 已落地
- `user/crt0_entry.S` + `user/crt0.c` — 已就绪，含兜底死循环和 `noreturn` 标注
- `main` 签名统一为 `int main(int argc, char **argv)` — 已完成
- ulibc 模块化拆分（`ustring.c` / `ufile.c` / `ugetpid.c` / `ulib.h`）— 已完成
- `usys.py` 替代 `usys.pl` — 已完成
- `PICOLIBC_EXPERIMENT=1` 构建门控 — 已就绪
- `user/pico/` 独立实验目录 — 已创建
- 最小 OS glue、linker script、picolibc cross file 和 Makefile 构建目标 — 已就绪
- `picohello` 链接规则已接入，最小运行验证已通过
- `picoio` 链接规则已接入，P1 文件 I/O 验证已通过
- `picostdio` 链接规则已接入，stdio 文件 I/O 验证已通过
- `picoinit` 链接规则已接入，constructor/destructor 验证已通过
- `picoecho` / `picosleep` 链接规则已接入，简单程序迁移 PoC 已通过
- `picotime` 链接规则已接入，time/entropy/errno PoC 已通过
- `_pico_echo` / `_pico_sleep` 链接规则已接入，真实 native 源码双运行时验证已通过
- `make test-picolibc` 已接入 Go runner，单次 QEMU 启动验证全部 Picolibc PoC 程序

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

### 1.4 本地依赖与 clangd 索引

本仓库不提交 `external/picolibc/`、`opt/picolibc-rv64-xv6/`、`build/picolibc-rv64/` 和 `compile_commands.json`。这些都是本地生成物，已由 `.gitignore` 忽略。

首次配置一台机器时，先准备宿主机依赖：

```bash
# macOS / Homebrew 示例
brew install riscv-gnu-toolchain meson ninja

# 确认工具可用
riscv64-unknown-elf-gcc --version
meson --version
ninja --version
```

下载 Picolibc 源码到约定目录：

```bash
mkdir -p external
git clone https://github.com/picolibc/picolibc.git external/picolibc
```

如果已经 clone 过，只需要更新：

```bash
git -C external/picolibc pull --ff-only
```

构建并安装到本仓库本地目录：

```bash
make picolibc-configure
make picolibc-build
make picolibc-install
```

默认路径：

- 源码：`external/picolibc`
- Meson 构建目录：`build/picolibc-rv64`
- 安装目录：`opt/picolibc-rv64-xv6`

如果需要使用其他源码或安装路径，可以覆盖变量：

```bash
make picolibc-install \
  PICOLIBC_SRC=/path/to/picolibc \
  PICOLIBC_PREFIX=/path/to/install
```

构建完成后生成 clangd 编译数据库：

```bash
./tools/gen_compile_commands.sh
```

这一步会做三件事：

- 为 xv6 kernel、native user、`user/pico/` 生成仓库自己的 compile commands
- 为 `user/echo.c` / `user/sleep.c` 额外生成 `PICOLIBC_USER` 变体，方便查看迁移分支
- 如果存在 `build/picolibc-rv64/compile_commands.json`，自动合并 Meson 生成的 Picolibc 编译数据库，让 clangd 能索引 `external/picolibc`

VS Code / clangd 使用建议：

```bash
./tools/gen_compile_commands.sh
clangd --check=external/picolibc/libc/stdio/printf.c
clangd --check=external/picolibc/libc/stdlib/malloc.c
clangd --check=user/pico/picolibc_os.c
```

如果 `external/picolibc` 的跳转仍不完整，优先检查：

- `build/picolibc-rv64/compile_commands.json` 是否存在
- 根目录 `compile_commands.json` 是否已重新生成
- VS Code 是否已重启 clangd server
- `.clangd` 是否仍包含 `external/picolibc/.*` 和 `opt/picolibc-rv64-xv6/.*` 的 fallback flags

## 2. 目标与约束

### 2.1 第一阶段目标

- 在不影响现有 xv6 用户程序的前提下，引入一条 `picolibc` PoC 链路
- `build/user/_picohello`、`build/user/_picoio`、`build/user/_picostdio`、`build/user/_picoinit`、`build/user/_picoecho`、`build/user/_picosleep`、`build/user/_picotime`、`build/user/_pico_echo`、`build/user/_pico_sleep` 可被打包进 `fs.img` 并在 xv6 shell 中运行
- 先跑通 `printf`、`malloc/free`、`main(argc, argv)` 返回后正常 `exit`，再验证基础 POSIX fd I/O 和 stdio 文件 I/O
- 保持现有 native 用户程序不变（`cat`、`sh`、`usertests`、`nettests`、`mmaptest`、`make test-quick` 等）
- 提供独立 `make test-picolibc` 回归入口，不把实验程序混入默认 quick/smoke

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
- 第一阶段只验证 `fopen/fread/fwrite/fclose` 的基础文件读取链路，不追求完整 stdio 语义
- 第一阶段不引入完整 Unix 初始栈布局
- 第一阶段不处理 `envp` / `auxv`

### 2.4 整体路线

先不替换系统 libc，先做独立的 `picolibc` 实验链路；用 `__xv6_*` raw syscall 隔离符号；用最小 OS glue 跑通 `printf/malloc/exit`；再扩展到基础 fd I/O、stdio 文件 I/O、init/fini 和 time/entropy/errno；确认稳定后，再迁移简单用户程序。

```text
crt0 硬化 → scaffold → raw syscall → OS glue → linker script → 构建 picolibc → picohello → picoio/picostdio → init/fini → picoecho/picosleep → picotime → _pico_echo/_pico_sleep → 迁移更多简单程序
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
  picoio.c              # 文件 I/O PoC 测试程序
  picostdio.c           # stdio 文件 I/O PoC 测试程序
  picoinit.c            # constructor/destructor PoC 测试程序
  picoecho.c            # echo 迁移前置 PoC
  picosleep.c           # sleep 迁移前置 PoC
  picotime.c            # time/entropy/errno PoC 测试程序
  xv6_pico.h            # Picolibc 迁移版程序使用的少量 xv6 扩展声明
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
  $(PICO_BUILD)/usys_pico.o \
  $(PICO_BUILD)/picolibc_os.o
```

关键点：

- 不链接 native `ulib.o` / `printf.o` / `umalloc.o`
- 只在 `PICOLIBC_EXPERIMENT=1` 时构建 `_picohello`、`_picoio`、`_picostdio`、`_picoinit`、`_picoecho`、`_picosleep`、`_picotime`、`_pico_echo`、`_pico_sleep`
- native `UPROGS` 与实验 `PICO_UPROGS` 保持分离

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

P1/P2（已补充）：`__xv6_open`、`__xv6_unlink`、`__xv6_getpid`、`__xv6_kill`、`__xv6_sleep`、`__xv6_uptime`

- 涉及模块：`user/pico/usys_pico.py`、`user/pico/xv6_syscall_raw.h`。
- 验证方式：`nm build/user/pico/usys_pico.o | grep __xv6` 确认 `__xv6_*` 符号均生成。

## 7. 阶段 3：最小 OS glue（已完成）

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
- `_write/_read/_close/_fstat/_lseek/_sbrk` — 转发到对应非下划线接口，兼容常见 libc backend 符号
- `open/stat/unlink/getpid/kill/sleep` — P1 OS glue，用于验证文件 I/O、基础进程接口和简单程序迁移链路
- `gettimeofday/times/getentropy/abort` — P2 OS glue，用于补齐常见 libc fallback 符号；`getentropy` 当前是 deterministic PoC，不提供安全随机性

目标：跑通 `printf`、`malloc/free`、`exit`。

### 7.3 `struct stat` 风险

不要在同一编译单元中混用 `kernel/stat.h` 和 `<sys/stat.h>`。

策略：在 `picolibc_os.c` 中维护私有 `struct xv6_stat`，由 xv6 ABI 结构手动转换到 libc `struct stat`。第一阶段如只需支撑 `printf`，可先做最小填充。

- 涉及模块：`user/pico/picolibc_os.c`、`user/pico/xv6_syscall_raw.h`。
- 验证方式：安装 `picolibc` 后，链接 `_picohello` 时无 `_write` / `_read` / `_sbrk` / `_exit` undefined symbol。

## 8. 阶段 4：Picolibc 专用 linker script（已完成）

依赖：阶段 1。与阶段 3（OS glue）和阶段 5（构建 picolibc）可并行推进。

新增 `user/pico/user_pico.ld`。

第一阶段目标：

- `ENTRY(_start)`
- 明确 `.text` / `.rodata` / `.data` / `.bss`
- 预留 `.preinit_array` / `.init_array` / `.fini_array` 和对应 start/end 符号
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

## 9. 阶段 5：构建外部 picolibc（仓库侧已完成）

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
- `posix-console=true` — 编入 `stdin/stdout/stderr`，让 `printf` 可通过 `write` 路径输出
- `enable-malloc=true` — 让 picolibc malloc 通过 `sbrk` 工作

### 9.3 Cross file

维护一份 `toolchain/cross-riscv64-xv6.txt`，参考 `build.md` 中 RISC-V cross file 示例。编译选项尽量与 xv6 当前用户程序一致：

- `-nostdlib`、`-ffreestanding`、`-fno-common`
- `-mcmodel=medany`、`-mno-relax`
- `-march=rv64gc`、`-mabi=lp64`（以本地工具链实际配置为准）

- 涉及模块：`toolchain/cross-riscv64-xv6.txt`、`Makefile`。
- 验证方式：`make PICOLIBC_EXPERIMENT=1 build` 可通过 `_picohello` 链接，无 libc symbol undefined 错误。

### 9.4 Makefile 目标

仓库提供以下目标，但不自动下载外部源码：

- `make picolibc-configure PICOLIBC_SRC=/path/to/picolibc`
- `make picolibc-build PICOLIBC_SRC=/path/to/picolibc`
- `make picolibc-install PICOLIBC_SRC=/path/to/picolibc`

默认安装路径为 `opt/picolibc-rv64-xv6`，也可通过 `PICOLIBC_PREFIX=/path/to/install` 覆盖。

从零配置和 clangd 索引流程见 [1.4 本地依赖与 clangd 索引](#14-本地依赖与-clangd-索引)。

## 10. 阶段 6：跑通 picohello（已完成）

依赖：阶段 0/1/2/3/4/5。

- 涉及模块：`user/pico/picohello.c`、`user/pico/picoio.c`、`user/pico/picostdio.c`、`user/pico/picoinit.c`、`user/pico/picoecho.c`、`user/pico/picosleep.c`、`user/pico/picotime.c`。

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

### 10.1 已验证输出

使用脚本化 QEMU 等待 shell prompt 后执行：

```sh
picohello a b c
echo done
```

关键输出：

```text
hello from picolibc
argc = 4
argv[0] = picohello
argv[1] = a
argv[2] = b
argv[3] = c
malloc(32) = 0x5010
done
```

结论：

- `printf` 可通过 `write` syscall 输出
- `argc/argv` 与当前 xv6 `exec()` ABI 匹配
- `malloc/free` 可通过 `sbrk` 路径工作
- `main` 返回后经 `exit` / `_exit` 回到 shell

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

- 回归入口：`make test-picolibc`
- 覆盖范围：`picohello`、`picoio`、`picostdio`、`picoinit`、`picoecho`、`picosleep`、`picotime`、`pico_echo`、`pico_sleep`

### 11.3 ELF 检查

```bash
riscv64-unknown-elf-readelf -h build/user/_picohello
riscv64-unknown-elf-readelf -l build/user/_picohello
riscv64-unknown-elf-nm -u build/user/_picohello
riscv64-unknown-elf-objdump -d build/user/_picohello
```

- 验证方式：全部命令输出符合预期。

当前 `_picohello` 已确认：

- `riscv64-unknown-elf-nm -u build/user/_picohello` 无未解析符号
- ELF entry 为 `0x0`
- ELF flags 为 `RVC, soft-float ABI`

### 11.4 常见失败速查

- **符号冲突（重复定义）** — 原因：误把 native `ULIB` 混进 picolibc 目标。处理：只保留 `crt0 + raw syscall + os glue + libc.a + libgcc.a`
- **`printf` 无输出** — 原因：`write()` 未实现或 stdout 路径不通。处理：检查 OS glue 中 `write`/`isatty`
- **`malloc` 崩溃** — 原因：`sbrk` 包装有问题或误链接 `umalloc.o`。处理：检查 `sbrk` 包装和链接顺序
- **ELF 被 `exec()` 拒绝** — 原因：LOAD segment 不兼容。处理：先查 `readelf -l`，优先调 linker script
- **TLS / errno undefined symbol** — 原因：配置不匹配。处理：检查 `thread-local-storage` / `newlib-global-errno`

## 12. 阶段 8：逐步扩展

依赖：阶段 6 稳定通过后。

### 12.1 第二阶段：扩展 OS glue

P1 接口已补：`open`、`stat`、`unlink`、`getpid`、`kill`、`sleep`。

- 目标：支撑简单文件 I/O（POSIX fd API 与 `fopen` / `fread` / `fwrite` / `fclose`）
- 已验证：`picoio README` 可通过 `open/stat/read/write/close/getpid` 读取并输出 README 前 64 字节
- 已验证：`picostdio README` 可通过 `fopen/fread/fwrite/fclose` 读取并输出 README 前 64 字节
- 已验证：`picoecho` / `picosleep` 可作为简单 native 程序迁移前置 PoC
- 已验证：`picotime` 可通过 `gettimeofday/times/getentropy` 和缺失文件 `errno` 路径跑通 P2 PoC
- 已验证：`make test-picolibc` 可在 Go runner 中自动回归全部 Picolibc PoC
- 已验证：`_pico_echo` / `_pico_sleep` 可由 `user/echo.c` / `user/sleep.c` 真实源码构建，原生 `_echo` / `_sleep` 保持不变
- 下一步：收紧完整 errno 语义，或开始迁移 `cat` / `wc` / `ls`

### 12.1.1 已验证输出

使用脚本化 QEMU 等待 shell prompt 后执行：

```sh
picoio README
echo done
```

关键输出：

```text
picoio pid = 3
stat README size = 2226
read 64 bytes from README
xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson'
done
```

使用脚本化 QEMU 等待 shell prompt 后执行：

```sh
picostdio README
echo done
```

关键输出：

```text
fread 64 bytes from README
xv6 is a re-implementation of Dennis Ritchie's and Ken Thompson'
done
```

使用脚本化 QEMU 等待 shell prompt 后执行：

```sh
picoinit
picoecho hello from pico
picosleep 0
echo done
```

关键输出：

```text
constructor ran
main sees constructor_ran = 1
main sees destructor_ran = 0
destructor ran
hello from pico
```

使用脚本化 QEMU 等待 shell prompt 后执行：

```sh
picotime
echo done
```

关键输出：

```text
gettimeofday sec=0 usec=200000
times ticks=2 utime=0 stime=0
entropy: a2 6b d8 9e 42 b7 70 8f
missing open errno=2
```

### 12.2 第三阶段：init/fini（已完成）

当前实现：

- `user/pico/crt0.c` 在进入 `main()` 前遍历 `__preinit_array_start/end` 和 `__init_array_start/end`
- `user/pico/user_pico.ld` 显式导出 `__preinit_array_*`、`__init_array_*`、`__fini_array_*`
- fini array 交给 `exit()` 路径处理，避免手动调用导致 destructor 重复执行
- `picoinit` 验证 constructor 在 `main()` 前执行，destructor 在 `main()` 返回后执行一次

### 12.3 程序迁移顺序

优先：`picohello` → `picoecho/picosleep` → `echo` → `sleep` → `cat` → `wc` → `ls` → `grep`

暂不优先：`init`、`sh`、`usertests`、`nettests`、`mmaptest`（系统启动关键路径或覆盖面太广，不适合做 libc PoC 首批迁移）。

## 13. 当前提交边界

已提交部分：

- `build(user): add picolibc experiment scaffold` — 建立 `user/pico/`、native `usys.py`、实验构建门控
- `libc: wire picolibc hello path` — 接入 P0 OS glue、linker script、cross file，并验证 `_picohello`

当前待提交部分：

- 接入 Picolibc init array，验证 constructor/destructor
- 扩展 raw `sleep` syscall 和 Picolibc `sleep()` OS glue
- 扩展 raw `uptime` syscall 和 P2 time/entropy/abort OS glue
- 新增 `_picoinit`、`_picoecho`、`_picosleep`
- 新增 `_picotime`
- 新增 `picolibc` Go test suite 和 `make test-picolibc`
- 新增 `_pico_echo` / `_pico_sleep`，验证真实 native 源码的 Picolibc 变体
- 同步本文档中的 init/fini、简单程序和 P2 PoC 状态

## 14. 第一阶段完成标准

全部满足以下条件视为第一阶段完成：

1. 默认 native 用户程序和测试入口不受影响
2. `make PICOLIBC_EXPERIMENT=1 build` 能生成 `_picohello`、`_picoio`、`_picostdio`、`_picoinit`、`_picoecho`、`_picosleep`、`_picotime`、`_pico_echo`、`_pico_sleep`
3. `_picohello` 的 `readelf` / `nm` 检查通过
4. `_picohello`、`_picoio`、`_picostdio`、`_picoinit`、`_picoecho`、`_picosleep`、`_picotime`、`_pico_echo`、`_pico_sleep` 可在 xv6 shell 中运行
5. `printf` 正常输出
6. `malloc/free` 正常工作
7. `main` 返回后能正确 `exit`
8. `picoio README` 可通过 POSIX fd API 读取并输出文件内容
9. `picostdio README` 可通过 stdio API 读取并输出文件内容
10. `picoinit` 可验证 constructor/destructor 顺序
11. `picoecho` / `picosleep` 可验证简单程序迁移链路
12. `picotime` 可验证 time、entropy 和 errno PoC
13. `make test-picolibc` 可自动回归全部 Picolibc PoC
14. `_pico_echo` / `_pico_sleep` 可验证真实 native 源码迁移链路

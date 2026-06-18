# Picolibc 接入记录

本文档记录 `dev/all` 分支上的 Picolibc 接入状态、构建方式和后续路线。当前策略是保留 xv6 native ulibc，同时建立一条独立的 Picolibc 实验链路，用小工具逐步验证 libc、syscall 和用户程序迁移能力。

## 1. 当前状态

### 已完成

- native 用户程序仍使用 `XV6_ULIB`，Picolibc 程序只在 `PICOLIBC_EXPERIMENT=1` 时构建。
- 用户程序入口已统一为 `_start -> crt0_main(argc, argv) -> main(argc, argv) -> exit(status)`。
- native `ulib.c` 已拆分为 `ustring.c`、`ufile.c`、`ugetpid.c`，syscall stub 生成器已从 Perl 切到 Python。
- `user/pico/` 已建立独立启动代码、raw syscall stub、OS glue、linker script 和测试程序。
- Picolibc ELF 已拆成 `R E` / `RW` 两个 `LOAD` segment，避免 linker 生成 `RWE` segment。
- `make test-picolibc` 已接入 Go runner，用一次 QEMU 启动验证 Picolibc PoC 程序。

### 已验证程序

- `picohello`：`printf`、`argc/argv`、`malloc/free`、`exit`
- `picoio`：`open/stat/read/write/close/getpid`
- `picostdio`：`fopen/fread/fwrite/fclose`
- `picoseek`：`fseek/ftell/rewind/fgetc`
- `picodup2`：`dup2` stdout 重定向
- `picosys`：`dup/pipe/chdir/mkdir/link/symlink`
- `picocat`：cat 风格顺序读写
- `picoregex` / `picoregextest`：预编译 regex、锚点、转义、`+`、`?`、字符类和非法 pattern 校验
- `picogrep`：复用预编译 regex 过滤文件行
- `picowc`：wc 风格行数、词数、字节数统计
- `getdentstest`：native `getdents` syscall、目录项类型和 offset 推进
- `picogetdents`：Picolibc raw `__xv6_getdents` 目录遍历
- `picoinit`：constructor/destructor
- `picoecho` / `picosleep`：简单程序迁移 PoC
- `picotime`：`gettimeofday/times/getentropy` 和 errno smoke
- `_pico_echo` / `_pico_sleep`：真实 native 源码的 Picolibc 变体

当前 `getdents` 已完成 raw syscall 验证；`opendir/readdir/closedir` 还没有实现，它们属于 libc shim 层，不应做成 syscall。

## 2. 官方资料

- OS integration: <https://github.com/picolibc/picolibc/blob/main/doc/os.md>
- Build options: <https://github.com/picolibc/picolibc/blob/main/doc/build.md>
- Linking: <https://github.com/picolibc/picolibc/blob/main/doc/linking.md>
- Init / constructors: <https://github.com/picolibc/picolibc/blob/main/doc/init.md>

## 3. 本地构建

本仓库不提交 `external/picolibc/`、`opt/picolibc-rv64-xv6/`、`build/picolibc-rv64/` 和 `compile_commands.json`。这些都是本地生成物，已由 `.gitignore` 忽略。

准备依赖：

```bash
brew install riscv-gnu-toolchain meson ninja
riscv64-unknown-elf-gcc --version
meson --version
ninja --version
```

下载源码：

```bash
mkdir -p external
git clone https://github.com/picolibc/picolibc.git external/picolibc
```

构建并安装：

```bash
make picolibc-configure
make picolibc-build
make picolibc-install
```

默认路径：

- 源码：`external/picolibc`
- Meson 构建目录：`build/picolibc-rv64`
- 安装目录：`opt/picolibc-rv64-xv6`

可覆盖路径：

```bash
make picolibc-install \
  PICOLIBC_SRC=/path/to/picolibc \
  PICOLIBC_PREFIX=/path/to/install
```

生成 clangd 编译数据库：

```bash
./tools/gen_compile_commands.sh
```

这会合并 xv6、`user/pico/` 和 `build/picolibc-rv64/compile_commands.json`。如果 `external/picolibc` 跳转不完整，先确认 Meson compile database 存在，然后重启 VS Code clangd server。

## 4. 架构边界

### Native 链路

native xv6 用户程序继续使用：

```text
user/crt0_entry.S
user/crt0.c
user/ustring.c
user/ufile.c
user/ugetpid.c
user/printf.c
user/umalloc.c
user/usys.py
user/usyscall.h
user/ulib.h
user/user.h
```

### Picolibc 链路

Picolibc 程序只链接：

```text
PICO_OBJS + libc.a + libgcc.a
```

不要把 native `printf.o`、`umalloc.o`、`ustring.o` 或 `user/user.h` 混入 Picolibc 链路，否则会出现符号重复和原型冲突。

`user/pico/` 主要文件：

```text
crt0_entry.S          # Picolibc 专用 _start
crt0.c                # preinit/init array + main + exit
usys_pico.py          # 生成 __xv6_* raw syscall stub
xv6_syscall_raw.h     # raw syscall 声明
picolibc_os.c         # POSIX 风格 OS glue
user_pico.ld          # Picolibc 用户程序 linker script
xv6_pico.h            # Picolibc 测试程序使用的 xv6 扩展声明
```

## 5. OS Glue

`picolibc_os.c` 负责把 Picolibc 期望的 POSIX 风格接口映射到 xv6 syscall。核心原则：

- 不 include `user/user.h`。
- 不在同一编译单元混用 `kernel/stat.h` 和 `<sys/stat.h>`。
- raw syscall 使用 `__xv6_*` 前缀，避免和 libc 符号冲突。
- xv6 只返回 `-1`，errno 只能在用户态根据可判断条件做近似映射。

当前已接入的主要接口：

- 进程与启动：`_exit`、`abort`
- 基础 I/O：`read`、`write`、`close`、`isatty`
- 文件：`open`、`stat`、`fstat`、`unlink`
- fd：`dup`、`dup2`、`pipe`
- 定位：`lseek`
- 内存：`sbrk`
- 路径：`chdir`、`mkdir`、`link`、`symlink`
- 时间和随机：`gettimeofday`、`times`、`getentropy`
- xv6 扩展：`sleep`、`getpid`、`kill`

## 6. 测试

常用验证：

```bash
make build
make build PICOLIBC_EXPERIMENT=1
make test-quick
make test-picolibc
```

ELF 检查：

```bash
riscv64-unknown-elf-readelf -h build/user/_picohello
riscv64-unknown-elf-readelf -l build/user/_picohello
riscv64-unknown-elf-nm -u build/user/_picohello
```

要求：

- entry 指向 `_start`
- 无 `INTERP` / `DYNAMIC`
- 无 undefined symbol
- `LOAD` segment 为 `R E` / `RW`
- `LOAD` virtual address 页对齐

当前关键回归：

- `make test-quick` 覆盖 native `lseektest`、`dup2test`、`getdentstest`
- `make test-picolibc` 覆盖 Picolibc PoC 程序，包括 `picogetdents`

## 7. 目录读取后续路线

目录能力是 `ls`、`find`、shell glob、归档工具和很多第三方 C 程序的前置条件。当前 raw `getdents` 已完成，下一步按三层推进。

### 7.1 Syscall 层

已完成：

- `getdents(fd, buf, nbytes)`：从目录 fd 批量读取 `struct xv6_dent`，并推进 fd offset。

短期建议：

- `lstat(path, st)`：查看 symlink 本身，而不是跟随 symlink。
- `readlink(path, buf, size)`：读取 symlink target。
- `rename(old, new)`：路径重命名，先做 xv6 可支持的最小子集。
- `getcwd(buf, size)`：支撑 `pwd` 和更好的路径诊断。

不建议：

- 不要新增 `readdir` syscall。`readdir` 应该是 libc 层封装，底层继续使用 `getdents`。

### 7.2 Libc / Picolibc Shim 层

建议下一阶段补：

- `opendir(path)`：内部 `open(path, O_RDONLY)`，分配目录状态对象。
- `readdir(DIR *)`：缓存 `getdents` 返回的一批 `xv6_dent`，逐个转换成 Picolibc `struct dirent`。
- `closedir(DIR *)`：关闭 fd 并释放状态对象。
- `rewinddir(DIR *)`：回到目录开头。可以要求目录 fd 支持 `lseek(fd, 0, SEEK_SET)`，也可以在 shim 层单独处理。
- `dirfd(DIR *)`：返回底层 fd，很多程序会用。

可以晚点做：

- `scandir`
- `alphasort`
- `telldir`
- `seekdir`

### 7.3 系统工具层

推荐顺序：

1. `picols` 简版：直接基于 `__xv6_getdents` 列出目录项。
2. `picols` 文件类型显示：依赖更可靠的 `stat/fstat` 类型映射。
3. `picols -l`：依赖 `lstat/readlink` 和更完整的 `struct stat` 转换。
4. `picofind`：验证目录递归和路径拼接。
5. `picopwd`：验证 `getcwd`。
6. `pbox` multi-call binary：等 `picocat/picowc/picogrep/picols` 稳定后，再统一入口。

`pbox` 应该晚于目录和路径语义。它解决的是工具组织方式，不解决底层 OS 能力缺口。

## 8. 后续 Roadmap

### 阶段 9：目录工具

- 已完成 raw `getdents`
- 已完成 native `getdentstest`
- 已完成 Picolibc `picogetdents`
- 下一步实现简版 `picols`
- 之后再决定是否抽出 `opendir/readdir/closedir`

验收标准：

- `picols .` 至少能列出 `README`、`sh`、`cat` 等 fs image 中的文件。
- `make test-quick` 和 `make test-picolibc` 通过。

### 阶段 10：stat 语义补强

- 补强 xv6 `stat` 到 Picolibc `struct stat` 的转换。
- 明确 `S_IFREG`、`S_IFDIR`、`S_IFCHR`、symlink 的映射。
- 为 `st_mode` 添加合理默认权限位，例如 regular file `0644`、directory `0755`。
- 补齐 `st_ino`、`st_nlink`、`st_size` 等 xv6 已有语义。

不要伪造过多 POSIX 字段。xv6 没有 uid/gid、真实时间戳和完整权限模型时，保持字段最小且可解释。

### 阶段 11：symlink 语义

- 增加 `lstat`。
- 增加 `readlink`。
- 在 Picolibc OS glue 中接入 `lstat()` 和 `readlink()`。
- 扩展 `picols`，至少显示 symlink 类型；如果有 `-l`，显示 `name -> target`。

### 阶段 12：路径和 fd 控制

- `rename`
- `getcwd` / `pwd`
- `fcntl` 小子集：优先 `F_DUPFD`、`F_GETFL`，再考虑 `F_SETFL`
- `pbox` multi-call binary

每个新增 syscall 都应同时有 native 测试和 Picolibc smoke。

## 9. 常见失败速查

- 重复定义 `printf/malloc/free/memcpy`：误混 native `XV6_ULIB` 和 Picolibc 链路。
- `printf` 无输出：检查 `write()`、`isatty()` 和 stdout fd。
- `malloc` 崩溃：检查 `sbrk()` 返回旧 break、失败返回 `(void *)-1`。
- ELF 被 `exec()` 拒绝：先查 `readelf -l`，优先调 linker script。
- errno 异常：xv6 内核不返回具体 errno，用户态只能做近似映射。
- `readdir` undefined：当前尚未实现 `opendir/readdir/closedir` shim，先使用 raw `getdents` 或 `picogetdents`。

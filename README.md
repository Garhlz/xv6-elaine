# xv6-elaine

这是一个基于 MIT 6.S081 / 6.828 2021 xv6-riscv 的教学操作系统项目。`dev/all` 分支整合了 `util`、`syscall`、`pgtbl`、`traps`、`cow`、`thread`、`net`、`lock`、`fs`、`mmap` 全部 10 个 lab，并在此基础上继续推进内核机制整理、用户态运行时、Picolibc 移植和自动化测试。

## 项目重点

- **内核机制**：集成 COW fork、lazy mmap、符号链接、double-indirect block、per-CPU allocator、bucketed buffer cache、alarm、页表访问统计和 E1000/UDP 网络路径。
- **内核结构**：将 syscall 分发、参数解析以及 fd、mmap、网络 handler 拆分为独立模块，降低原始单文件职责密度。
- **用户态运行时**：建立统一的 `_start -> crt0_main -> main -> exit` 启动链，并拆分 native ulibc 的字符串、文件和 USYSCALL helper。
- **Picolibc 移植**：建立独立 ELF、启动代码、raw syscall stub 和 OS glue，支持文件 I/O、stdio、目录遍历、时间、随机数、regex，并运行 `cat`、`grep`、`wc` 等验证工具。
- **自动化测试**：实现 Go host-side runner，通过 QEMU 隔离运行 quick、smoke、per-subsystem 和 heavy suite，保留 Python grader 作为课程参考。

当前最重要的开发方向不是继续扩充 POSIX 接口，而是统一 VM page-fault 路径，并补强 mmap/COW 的生命周期与边界语义。详见 [docs/TODO.md](docs/TODO.md)。

## 目录结构

| 目录 | 内容 |
| --- | --- |
| `kernel/` | 进程、虚拟内存、文件系统、驱动、网络和 syscall |
| `user/` | native 用户程序、测试程序和最小 ulibc |
| `user/pico/` | Picolibc 启动链、OS glue 和验证工具 |
| `tests/host/` | Go 测试运行器 `xv6test` |
| `graders/` | MIT 课程 Python grader，作为 legacy 对照 |
| `docs/` | 当前路线、迁移记录和专题文档 |
| `notxv6/` | pthread 并发练习 `ph`、`barrier` |
| `mkfs/` | xv6 文件系统镜像构建工具 |

当前活动配置仍为 `conf/lab.mk` 中的 `LAB=net`，用于保留网络编译宏和 QEMU 参数；其他 lab 功能已在 `dev/all` 中无条件集成。

## 环境要求

- RISC-V GCC/binutils 工具链，例如 `riscv64-unknown-elf-` 或 `riscv64-linux-gnu-` 前缀（Makefile 自动检测，也可通过 `TOOLPREFIX` 指定）
- `qemu-system-riscv64`
- `python3`
- `go`

Picolibc 实验额外需要 `riscv64-unknown-elf-gcc`、Meson 和 Ninja，安装过程见 [docs/picolibc.md](docs/picolibc.md)。

## 构建与运行

```bash
make build
make image
make qemu
```

需要宿主机 UDP 端口转发时使用：

```bash
make qemu-net
```

调试入口：

```bash
make qemu-gdb
make qemu-gdb-net
```

所有生成物统一位于 `build/`，包括 kernel、用户程序、`fs.img`、测试日志和宿主机工具。

## 测试

推荐按改动范围选择最小覆盖层级：

| 命令 | 用途 |
| --- | --- |
| `make test-quick` | 11 个快速 case，适合日常迭代 |
| `make test-smoke` | 40 个轻量 case，适合提交前检查 |
| `make test-<subsystem>` | COW、mmap、fs、lock、net 等定向回归 |
| `make test-heavy` | `bigfile`、`sbrkmuch`、完整 `usertests` |
| `make test-all` | 串联八个子系统 suite，不包含 smoke、heavy 和 Picolibc |
| `make test-picolibc` | 构建并验证 19 个 Picolibc 用户程序 |

`test-quick` 当前覆盖 util、syscall、pgtbl、COW、fs 和 mmap，包括 `lseektest`、`dup2test`、`getdentstest`。`test-smoke` 使用 per-case QEMU 隔离，默认排除 DNS 和 heavy case，测试日志保存在 `build/test-logs/`。

课程原始入口 `make grade-*`、`make grade-all` 和 Python `make smoke-py` 继续保留，但新开发优先使用 Go runner 的 `test-*` 入口。

## Picolibc 实验

Picolibc 默认不参与普通构建，通过 `PICOLIBC_EXPERIMENT=1` 启用：

```bash
make picolibc-configure
make picolibc-build
make picolibc-install
make build PICOLIBC_EXPERIMENT=1
make test-picolibc
```

当前已验证启动、`malloc`、fd I/O、stdio、seek、dup/pipe、目录读取、stat 类型转换、time/entropy/errno、constructor/destructor、regex，以及 `cat`、`grep`、`wc` 风格工具。完整设计边界和后续路线见 [docs/picolibc.md](docs/picolibc.md)。

## 文档索引

- [docs/TODO.md](docs/TODO.md)：当前开发优先级和验收标准。
- [docs/picolibc.md](docs/picolibc.md)：Picolibc 构建、架构和实现状态。
- [docs/test-migrate.md](docs/test-migrate.md)：Go runner 的设计与迁移历史。
- [docs/lab-migration-plan.md](docs/lab-migration-plan.md)：10 个 lab 的整合记录。
- [docs/code-refactor.md](docs/code-refactor.md)：代码结构整理记录。
- [2021 年实验指导英文快照](docs/labs-2021/README.md)：十个实验的官方要求及来源。
- [2021 年实验指导中文译文](docs/labs-2021-zh/README.md)：十个实验的译文、通用指导和课程日程整理。

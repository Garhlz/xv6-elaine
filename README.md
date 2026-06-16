# xv6-labs-2021

这是一个基于 MIT 6.S081 / 6.828 2021 课程的 xv6-riscv 实验仓库。当前活动实验配置位于 `conf/lab.mk`，现在是 `LAB=net`；`dev/all` 分支上已经整合了 `util`、`syscall`、`pgtbl`、`traps`、`cow`、`thread`、`lock`、`fs` 和 `mmap` 实验。

## 目录说明

- `kernel/`：内核代码，包含进程、页表、文件系统、驱动和网络栈。
- `user/`：用户态程序与实验测试程序，例如 `nettests`。
- `mkfs/`：构建 `build/fs.img` 的宿主机工具源码。
- `conf/`：实验配置。
- `docs/`：整合记录、TODO 和后续工程化计划。
- `graders/`：课程 grader 脚本：
  - `grade-lab-util`
  - `grade-lab-syscall`
  - `grade-lab-net`
  - `grade-lab-pgtbl`
  - `grade-lab-traps`
  - `grade-lab-cow`
  - `grade-lab-thread`
  - `grade-lab-lock`
  - `grade-lab-fs`
  - `grade-lab-mmap`
- 根目录辅助脚本：
  - `gradelib.py`
  - `server.py`
  - `ping.py`

## 环境要求

需要以下工具在 `PATH` 中可用：

- `riscv64-linux-gnu-gcc` / `riscv64-linux-gnu-ld`
- `qemu-system-riscv64`
- `python3`
- `go`（用于当前并行迁移中的 Go host-side runner）

如果使用仓库里的 VS Code 配置，建议安装：

- `clangd`
- `clang-format`
- `ms-vscode.makefile-tools`

## 常用运行方式

### 启动 xv6

```bash
make qemu
```

### 启动并等待 GDB

```bash
make qemu-gdb
```

### 启动带宿主机 UDP 转发的 xv6

```bash
make qemu-net
```

如果需要在 GDB 模式下调试带转发的网络环境，使用：

```bash
make qemu-gdb-net
```

### 运行当前实验评分

```bash
make grade
```

### 运行各实验及汇总评分

```bash
make build
make image
make test-quick
make test-smoke
make smoke
make smoke-py
make test-smoke-go
make regression
make grade-util
make grade-syscall
make grade-net
make grade-pgtbl
make grade-traps
make grade-cow
make grade-thread
make grade-lock
make grade-fs
make grade-mmap
make test-thread
make test-cow
make test-traps
make test-mmap
make test-net
make test-lock
make test-fs
make test-usertests
make test-heavy
make test-all
make grade-all
make grade-all-heavy
```

`make build` 构建 kernel、user programs 和 host tools；`make image` 构建 `build/fs.img`。`make test-quick` 是日常快速验证入口，只跑一小组稳定、低耗时的 Go case。`make test-smoke` 是默认轻量提交前检查，使用 Go host-side runner；`make smoke` 是兼容别名；`make smoke-py` 保留旧 Python smoke 对照入口；`make test-smoke-go` 是 `make test-smoke` 的兼容别名。`make test-thread`、`make test-cow`、`make test-traps`、`make test-mmap`、`make test-net`、`make test-lock`、`make test-fs` 是各子系统 Go runner 测试入口；`make test-usertests` 覆盖 19 个轻量 usertests subtest；`make test-heavy` 仅跑 heavy 标签 case（bigfile、完整 usertests 等）。`make test-all` 串联全部 suite。`make regression` 是中等回归，`make grade-all-heavy` 是完整重型回归。`make grade-all` 继续保留课程完整回归语义，会先统一清理并构建一次系统产物，然后依次运行 `util` → `syscall` → `net` → `pgtbl` → `traps` → `cow` → `thread` → `lock` → `fs` → `mmap` 的 grader。

### net lab 手工测试

先在一个终端启动宿主机 UDP 回显服务：

```bash
make server
```

再在另一个终端启动 xv6：

```bash
make qemu
```

进入 xv6 shell 后运行：

```text
nettests
```

如果要从宿主机主动向 xv6 转发端口发送测试包，需要使用带 UDP 转发的 QEMU：

```bash
make qemu-net
make ping
```

## 说明

- `make grade` 会调用当前 `LAB` 对应的 grader；现在默认是 `graders/grade-lab-net`。
- `make grade-util`、`make grade-syscall`、`make grade-net`、`make grade-pgtbl`、`make grade-traps`、`make grade-cow`、`make grade-thread`、`make grade-lock`、`make grade-fs`、`make grade-mmap`、`make grade-all` 适合 `dev/all` 分支上的阶段性回归验证。
- 当前 `mmap` 集成已用 `make grade-mmap`、`make grade-cow`、`make grade-traps` 和 `make smoke` 验证通过；`grade-mmap` 额外覆盖了 `MAP_SHARED` 进程退出写回和 syscall buffer 指向 fresh mmap 页的 lazy fault-in。完整 `make grade-all` / `make grade-all-heavy` 属于重型回归，适合阶段性提交后单独运行。
- 默认的 `make qemu` / `make qemu-gdb` 不启用宿主机到 xv6 的 UDP 端口转发；只有 `make qemu-net` / `make qemu-gdb-net` 会显式传入 `NETFWD=1` 并打开 `hostfwd=udp::$(FWDPORT)-:2000`，避免非网络实验的评分过程额外依赖端口绑定。
- 生成文件统一放在 `build/` 下，例如 `build/fs.img`、`build/kernel/kernel`、`build/user/_sh`、`build/notxv6/ph`。旧路径生成物、`packets.pcap`、`xv6.out*` 不应当作为源码提交。
- 当前仓库已经补充了 `clangd` / `clang-format` / `compile_commands.json` 相关配置，适合继续做代码阅读和实验回顾。

## 当前状态

- 计划内 lab 已全部整合到 `dev/all`：`util`、`syscall`、`pgtbl`、`traps`、`cow`、`thread`、`net`、`lock`、`fs`、`mmap`。
- `conf/lab.mk` 仍保持 `LAB=net`，用于保留 net 相关编译宏和 QEMU 网络配置；`dev/all` 的其他 lab 功能是无条件集成。
- syscall 相关代码已按职责初步拆分：`kernel/syscall.c` 只保留分发与 trace，参数提取位于 `kernel/sysarg.c`，注册表位于 `kernel/syscall_table.c`，fd/mmap/net syscall 已分别拆到 `kernel/sysfd.c`、`kernel/sysmmap.c`、`kernel/sysnetcall.c`。
- `docs/lab-migration-plan.md` 记录 lab 迁移历史和关键取舍。
- `docs/TODO.md` 记录迁移完成后的工程化路线图。
- `docs/test-migrate.md` 记录 Go host-side runner 迁移计划、当前 smoke 覆盖和 Python grader 对照策略。

## 后续开发重点

建议先做工程地基，再做内核语义增强：

1. 整理 VM fault path，把 COW 和 mmap 缺页处理从 `trap.c` 中拆出清晰边界。
2. 审计 mmap、fork、exec、exit 的资源生命周期和错误路径，尤其是完整 Unix mmap fork 语义和 exec 清理失败的两阶段处理。
3. 在 syscall 机制层已经初步拆清后，继续审计 mmap 边界、推进 VM fault path 重构，并视需要整理用户态运行时入口（`crt0`）和 `ulibc` 边界。

## 测试建议

建议按改动范围分层运行测试，避免默认触发超长回归。

### 快速检查

适合小改动或先看是否能编译：

```bash
make build
make image
```

如果想跑一套默认的轻量 smoke 回归，使用：

```bash
make test-smoke
```

如果只是日常频繁验证，优先跑：

```bash
make test-quick
```

`test-quick` 目前包含 `pingpong`、`primes`、`trace-nothing`、`sysinfotest`、`pgtbltest`、`cowtest`、`symlinktest`、`mmaptest`，保持 per-case QEMU 隔离，不依赖外网 DNS，也不跑重型 `usertests` / `bigfile`。

`make smoke` 仍可用，但现在只是 `make test-smoke` 的兼容别名。默认 Go smoke 会统一构建一次 `build/fs.img`，然后运行 40 个轻量 case，覆盖 util、syscall、pgtbl、traps、thread、cow、net、fs、mmap 和 19 个 usertests subtest；net smoke 默认只跑本地 UDP echo 路径，不依赖外网 DNS。`make test-usertests` 单独跑 usertests 轻量 suite；`make test-heavy` 显式跑 heavy 标签 case。

如果要和旧实现逐项对照，使用：

```bash
make smoke-py
```

`./quick.sh` 仍可用，但现在只是委托 `make smoke`。smoke 故意不包含 `bigfile`、`grade-lab-lock`、`grade-lab-fs` 和完整 `grade-all`。

如果想显式调用 Go smoke runner 的兼容入口，使用：

```bash
make test-smoke-go
```

当前 Go smoke runner 使用 per-case QEMU，并在每条非末尾命令完成后等待 xv6 shell prompt 回到结尾位置再推进下一条命令；最后一条命令在期望输出稳定后即可完成，避免 `usertests <case>` 这类不稳定回到 prompt 的单项测试误超时。日志保存在 `build/test-logs/smoke/`。默认 smoke 只运行带 `smoke` 标签的 case；`nettests` 的 DNS 检查已拆为非默认 case，可通过 `xv6test run --suite smoke --tags dns` 单独运行。

### 定向检查

适合改动某个 lab 或某个子系统后跑对应 grader：

```bash
make grade-syscall
make grade-net
make grade-lock
make grade-fs
make grade-mmap
make regression
```

如果只想手工验证 `fs` 关键路径，优先跑：

```bash
make qemu
```

进入 xv6 后运行：

```text
symlinktest
bigfile
```

### 完整回归

只在阶段性集成完成或提交前运行：

```bash
make grade-all
make grade-all-heavy
```

说明：

- `symlinktest` 成本低，适合频繁跑。
- `mmaptest` 和 `grade-mmap` 聚焦 mmap 缺页、写回和课程级 fork 路径，不重复运行完整 `usertests`。
- `bigfile` 会顺序写满并回读校验 doubly-indirect 文件，耗时明显更长。
- `usertests` 覆盖面大，在 `FSSIZE=200000` 的集成分支上会比单 lab 分支慢很多。

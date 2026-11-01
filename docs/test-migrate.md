# Host-Side Test Runner

本文档记录 Go host-side runner 的当前架构、使用方式和已经完成的迁移决策。迁移已进入维护阶段；新的开发任务以 `TODO.md` 为准。

## 当前状态

- Go runner 位于 `tests/host/cmd/xv6test` 和 `tests/host/internal/testrunner`。
- `make test-smoke` 已是默认提交前入口，`make smoke` 和 `make test-smoke-go` 是兼容别名。
- Python `gradelib.py` 与 `graders/grade-lab-*` 仅保留为课程参考和回归对照。
- 默认采用 per-case QEMU，case 之间使用 snapshot 隔离文件系统状态。
- 支持普通 QEMU、UDP host forwarding 和 HostOnly 三种运行模式。
- heavy case 默认排除，DNS case 不进入默认 smoke。

当前 suite 基线：

| Suite | 当前用途 |
| --- | --- |
| `quick` | 11 个日常快速 case |
| `smoke` | 40 个默认轻量 case |
| `picolibc` | 1 个连续场景，执行 21 条 Picolibc 验证命令 |
| per-subsystem | thread、cow、traps、mmap、net、lock、fs、usertests 等定向回归 |
| `heavy` | `bigfile`、`sbrkmuch`、完整 `usertests` 等重型 case |

`make test-all` 只串联 thread、cow、traps、mmap、net、lock、fs、usertests 八个子系统入口，不会遍历所有 suite；smoke、heavy 和 Picolibc 需分别运行。

实际 case 列表以 runner 为唯一事实来源：

```bash
go run ./tests/host/cmd/xv6test list --suite quick
go run ./tests/host/cmd/xv6test list --suite smoke
go run ./tests/host/cmd/xv6test list --suite picolibc
```

## 使用方式

Makefile 日常入口：

```bash
make test-quick
make test-smoke
make test-cow
make test-mmap
make test-fs
make test-lock
make test-net
make test-usertests
make test-heavy
make test-all
make test-picolibc
```

CLI 支持列出和定向运行 suite/case：

```bash
xv6test list [--suite NAME] [--tags TAG,...]
xv6test run --suite NAME [--case NAME] [--tags TAG,...]
```

可选参数包括 `--log-dir` 和 `--timeout`。日志默认保存到：

```text
build/test-logs/<suite>/<case>.log
```

这里的 `xv6test` 表示 runner 命令。未安装独立二进制时，可在仓库根目录运行：

```bash
make build image
GOCACHE="$PWD/build/go-cache" go run ./tests/host/cmd/xv6test run --suite smoke --case bigargtest
```

CLI 定向运行前应先构建最新的内核和镜像；Makefile 的普通 `test-*` 入口已声明相关构建依赖。Picolibc 场景请使用 `make test-picolibc`，确保镜像包含实验程序。

## Runner 模型

### Case 描述

suite 和 case 当前直接注册在 Go 代码中。每个 case 可以声明：

- 要发送到 xv6 shell 的命令序列。
- 必须匹配的正则表达式、次数和 distinct 约束。
- 禁止出现的错误输出。
- tags、timeout 和 QEMU 模式。
- HostOnly 场景的宿主机命令。

暂不引入 YAML/JSON suite 描述。当前规模下，Go 类型检查和集中注册比增加配置解析层更直接。

### 命令完成判定

runner 等待 xv6 shell 初始 prompt，然后逐条发送命令。非末尾命令必须等待 prompt 重新出现在输出末尾；最后一条命令在期望输出稳定后完成。

这一策略避免依赖额外 sentinel 命令，也能兼容部分测试打印成功后不能稳定回到 shell 的情况。

### 隔离与清理

- 默认每个 case 单独启动 QEMU，避免目录、进程和文件状态互相污染。
- QEMU 使用 snapshot 保护基础 `fs.img`。
- runner 在成功、失败和 timeout 路径都负责清理进程组。
- stdout/stderr 会完整 drain 后再做最终断言和落盘。

per-suite QEMU 复用目前不作为优先事项；只有在状态重置协议明确后才值得实现。

### 网络和 HostOnly

- `QemuModeNormal`：普通 guest-side case。
- `QemuModeNetForward`：显式传入 `NETFWD=1`，用于宿主机 UDP server。
- `HostOnly`：不启动 QEMU，用于 `notxv6/ph` 和 `notxv6/barrier`。

默认 smoke 只运行本地 UDP 路径；外部 DNS 通过 `--tags dns` 显式运行。

## 测试分层

推荐顺序：

1. 小改动运行 `make test-quick`。
2. 子系统改动增加对应 `make test-<subsystem>`。
3. 提交前运行 `make test-smoke`。
4. 阶段性内核改动运行 `make test-heavy` 或 legacy grader 对照。

heavy、完整 FS/lock 压力和公网 DNS 不进入默认反馈路径，以保证日常测试时间可控。

## 失败排查

先查看 `build/test-logs/<suite>/<case>.log`，区分 guest 中的失败与宿主环境错误：

- 出现 `panic:` 时，先检查 panic 之前运行的命令。例如 `bigargtest` 检查超长参数导致 `exec()` 失败时的清理路径，不能只把超时归因于运行较慢。
- QEMU 报 `Could not set up host forwarding rule` 时，表示 UDP 转发端口绑定失败，应检查端口占用或执行环境对本机端口的限制。此时 guest 尚未运行，不能据此判断网络驱动失败。
- 单个 case 失败时，可定向复跑定位问题。全套运行后补跑成功的单项应分别记录，不能把有失败的原始运行记为全套一次通过。

## 已完成的迁移决策

- Go runner 已替代 Python smoke 成为默认入口。
- 原 util、syscall、pgtbl、traps、net、fs、mmap smoke 已迁移。
- thread、cow 和 19 个轻量 usertests case 已加入默认覆盖。
- 网络本地测试与外部 DNS 已拆分。
- `bigfile`、`sbrkmuch`、完整 `usertests` 已标记为 heavy。
- 已实现 timeout、失败日志、Reject 检查、QEMU 非零退出传播和输出 drain。

仍保留的已知覆盖差异：

- `sleep, makes syscall` 依赖 gdb breakpoint，Go runner 尚无通用 gdbstub 断言。
- `pte-printout` 只验证关键输出格式，未结构化验证 PTE 与 PA 对应关系。
- `bttest` 只验证可运行且不 panic，尚未集成 `addr2line` 源码位置检查。

## 后续工作

- [ ] 为 runner 增加单元测试，覆盖匹配、Reject、timeout 和 tag 过滤。
- [ ] 建立 CI：先跑 `go test ./...` 和构建，再加入 quick QEMU suite。
- [ ] 可选输出 JSON/JUnit，便于 CI 展示 case 级结果。
- [ ] 在 Go suite 覆盖足够稳定后，将 Python grader 明确归档为 legacy。

不要为了追求 case 数量而扩大默认 smoke。新增 case 应优先覆盖真实的机制边界和历史回归点。

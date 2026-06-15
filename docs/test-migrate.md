# Host-Side Test Runner Migration Plan

本文档用于敲定测试系统迁移计划：用 Go 重写 host-side test runner，逐步替换当前 Python `gradelib.py` 与 `graders/grade-lab-*` 体系，并移除“grade / 评分”语义。迁移目标不是改变 xv6 guest 内部测试程序，而是重构宿主机侧的构建、启动 QEMU、执行命令、匹配输出、汇总结果和记录日志的流程。

## 1. 目标与边界

### 1.1 目标

- 实现一个 Go 编写的 host-side test runner，作为后续统一测试入口。
- 将“grade”命名逐步替换为“test / check / suite / runner”等测试语义。
- 保留现有 xv6 guest-side C 测试程序，例如 `user/usertests.c`、`user/mmaptest.c`、`user/nettests.c`、`user/cowtest.c`、`user/bigfile.c`。
- 保留 Makefile 作为用户入口层，但将底层执行逻辑逐步切换到 Go runner。
- 支持分层测试：smoke、regression、heavy、stress、per-lab、per-subsystem。
- 支持结构化结果输出，便于后续接入 CI 或生成测试报告。

### 1.2 非目标

- 不重写 xv6 guest 内部测试程序为 Go。
- 不改变 lab 功能语义或为了测试迁移修改内核行为。
- 不追求完整 POSIX 测试套件。
- 不立即删除 Python runner；先通过并行验证建立等价性，再逐步退场。
- 不把课程原始 grader 语义继续作为长期接口设计核心。

## 2. 当前测试系统现状

### 2.1 Host-side 组成

- `gradelib.py`
  - 负责解析参数、启动 QEMU、连接 gdbstub、注入 xv6 shell 命令、匹配输出、保存日志。
- `graders/grade-lab-*`
  - 每个 lab 一个 Python 脚本，描述测试 case、期望输出和分值。
  - 目前虽然已经移动到 `graders/`，但命名和输出仍保留课程评分语义。
- Go runner (`tests/host/cmd/xv6test`) 已实现 `list` / `run` 命令，支持 `--suite`、`--case`、`--tags`、`--log-dir`、`--timeout`。
- `Makefile`
  - 暴露 `make grade-*`、`make smoke`、`make regression`、`make grade-all`、`make grade-all-heavy`。
  - `make smoke` 已经是轻量测试入口，但内部仍调用 Python grader。
- `quick.sh`
  - 当前只是 `make smoke` 的兼容包装。
- `tests/host/cmd/xv6test`
  - Go host-side runner 的并行迁移入口。
  - 当前已支持通过 `make qemu` 启动 xv6、向 shell 注入命令、用 regex 匹配输出、保存日志。
  - 当前 Makefile 并行入口是 `make test-smoke-go`，暂不替换原有 `make smoke`。
  - 当前 smoke suite 已接入 util、syscall、pgtbl、traps、net、fs、mmap 中可用 regex 表达的首批用例。

### 2.2 Guest-side 组成

- `user/usertests.c`
  - 综合用户态测试，覆盖面广但耗时重。
- `user/mmaptest.c`
  - mmap 相关测试。
- `user/nettests.c`
  - net lab 相关测试。
- `user/cowtest.c`
  - COW 相关测试。
- `user/pgtbltest.c`
  - pgtbl 相关测试。
- `user/alarmtest.c`
  - traps / alarm 相关测试。
- `user/bigfile.c`、`user/symlinktest.c`
  - fs lab 相关测试。
- `user/kalloctest.c`、`user/bcachetest.c`、`user/stats.c`
  - lock / allocator / bcache 相关测试。

### 2.3 当前 Go Runner 基线

当前 Go runner 仍作为并行入口存在，不替换 `make smoke`：

- 入口：`make test-smoke-go`
- CLI：`xv6test list`、`xv6test run`
- 过滤能力：支持 `--suite`、`--case`、`--tags`
- 日志目录：`build/test-logs/<suite>/<case>.log`
- 当前执行模型：每个 case 独立启动一个 QEMU，并使用 `QEMUEXTRA+=-snapshot` 避免污染 `build/fs.img`
- 最近一次验证结果：`make test-smoke-go` 已覆盖 18 个 smoke case，并验证通过

正式切换 `make smoke` 前，仍需要补齐 Python smoke 与 Go smoke 的覆盖对照、耗时记录和不等价项说明。

## 3. 命名迁移原则

后续命名应从“评分”转为“测试”。旧命名只在兼容层保留，不再作为新接口扩展方向。

| 当前名称 | 目标名称 | 说明 |
| --- | --- | --- |
| `gradelib.py` | `testrunner` 或 `xv6test` | Go runner 二进制名称待定，建议 `xv6test` |
| `graders/` | `tests/host/` 或 `tests/suites/` | 保存 host-side 测试 suite 描述 |
| `grade-lab-*` | `suite-*.json` / `*.yaml` / Go 注册表 | 测试套件不再包含分值概念 |
| `make grade-*` | `make test-*` | `grade-*` 可作为过渡别名 |
| `make grade-all` | `make test-all` | 完整测试，不表示评分 |
| `make grade-all-heavy` | `make test-heavy` | 重型测试入口 |

建议长期入口：

- `make test-smoke`
- `make test-regression`
- `make test-heavy`
- `make test-all`
- `make test-mmap`
- `make test-fs`
- `make test-lock`
- `make test-net`

过渡期可保留：

- `make smoke`
- `make regression`
- `make grade-*`
- `make grade-all`

## 4. Go Runner 设计

### 4.1 目录建议

建议新增：

```text
tests/host/cmd/xv6test/
  main.go
tests/host/internal/testrunner/
  runner.go
  qemu.go
  matcher.go
  suite.go
  report.go
tests/suites/
  smoke.yaml
  mmap.yaml
  fs.yaml
  lock.yaml
  net.yaml
```

如果希望先减少格式设计成本，也可以第一阶段不用 YAML/JSON，直接在 Go 中注册 suite；等 runner 稳定后再把 case 描述数据化。

### 4.2 核心能力

- 启动 QEMU：
  - 调用 `make qemu-gdb` 或直接调用 `qemu-system-riscv64`。
  - 推荐第一阶段继续通过 Makefile 启动，减少参数漂移。
- 连接 gdbstub：
  - 复用当前 `qemu-gdb` 模式下的 gdbstub 行为。
  - 先实现与 `gradelib.py` 等价的等待和超时逻辑。
- 注入 xv6 shell 命令：
  - 支持单条命令。
  - 支持一组命令在同一个 QEMU 实例中顺序执行。
  - 支持命令执行后自动 shutdown。
- 匹配输出：
  - 支持 regex 匹配。
  - 支持 forbidden regex。
  - 支持多段有序匹配。
  - 支持保存完整输出供失败排查。
- 超时控制：
  - 每个 case 有独立 timeout。
  - 每个 suite 有整体 timeout。
  - 超时后可靠终止 QEMU。
- 结构化报告：
  - 默认人类可读输出。
  - 可选 JSON 输出。
  - 后续可选 JUnit XML 输出。
- 测试分层：
  - 按 suite、tag、subsystem、heavy 标记筛选。

### 4.3 测试 case 数据模型

建议每个 case 至少包含：

```text
name
suite
tags
buildTargets
qemuMode
commands
expect
reject
timeout
heavy
artifacts
```

字段含义：

- `name`：测试名。
- `suite`：所属套件，例如 `mmap`、`fs`、`lock`。
- `tags`：例如 `smoke`、`regression`、`heavy`、`net`。
- `buildTargets`：运行前需要的构建目标，例如 `image`、`build`。
- `qemuMode`：普通 QEMU、带网络转发 QEMU、或 host-only。
- `commands`：进入 xv6 shell 后执行的命令列表。
- `expect`：必须匹配的输出 regex。
- `reject`：不能出现的输出 regex。
- `timeout`：case 级超时。
- `heavy`：是否重型测试。
- `artifacts`：失败时保存的日志、pcap 或 QEMU 输出。

### 4.4 命令注入与完成判定

当前 Go runner 只通过 xv6 shell prompt `$ ` 判断 shell 初始就绪。后续命令推进使用 sentinel 驱动，避免测试程序自身输出 `$ ` 时误触发下一条命令。

每条命令发送后，runner 会自动追加一条唯一 sentinel：

```text
<command>
echo __XV6TEST_DONE_<case>_<step>__
```

runner 等待对应 sentinel 出现在独立输出行后再发送下一条命令；由于 xv6 shell prompt 可能与输出拼在同一行，识别时允许前导 `$ ` prompt。完成后在完整输出上做 `expect`、`count` 和 `reject` 检查。这样可以减少 prompt 误触发，也便于定位是哪一条命令超时。

### 4.5 QEMU 隔离策略

当前默认策略是 per-case QEMU：

- 优点：测试隔离强，失败后状态污染小，日志和失败 case 一一对应。
- 缺点：启动成本较高，完整 smoke 耗时比复用 QEMU 更长。

未来可评估 per-suite QEMU 复用模式：

- 只用于明确无状态污染或可主动清理状态的轻量测试。
- 必须处理文件系统状态污染、命令顺序依赖和失败恢复。
- 不应影响默认 per-case 隔离模式。

### 4.6 Runner 稳定性要求

- timeout 后必须杀掉整个 QEMU 进程组。
- 后台进程，例如 `make server`，必须在 case 结束后可靠清理。
- 每个失败 case 必须保存完整日志。
- `reject` regex 必须在最终完整输出上检查，不能只依赖提前通过时的局部输出。
- case 名称必须稳定，并支持通过 `--case` 单独运行。
- tag 过滤必须只影响选择 case，不改变 case 自身语义。
- runner 失败时必须返回非零退出码，便于 Makefile 和 CI 判断。

## 5. 迁移阶段

### Phase 0：建立迁移基线（切换 `make smoke` 前必须完成）

- [ ] 记录旧 Python `make smoke` 的覆盖范围、输出样例和平均耗时。
- [ ] 记录 Go `make test-smoke-go` 的覆盖范围、输出样例和平均耗时。
- [ ] 建立 Python smoke 与 Go smoke 的 case 对照表，明确完全等价、部分等价和暂未迁移项。
- [ ] 标出所有不等价项、原因和后续处理方式。
- [ ] 记录 `make regression`、`make grade-*` 的覆盖范围，作为 per-subsystem 迁移的后续基线。
- [x] 为失败日志路径建立统一目录：`build/test-logs/`。

验证方式：

- 运行 `make smoke`。
- 运行 `make test-smoke-go`。
- 运行当前重点定向测试：`make grade-mmap`、`make grade-cow`、`make grade-traps`。
- 保存输出样例，作为 Go runner 迁移对照。

Phase 0 不阻塞 Go runner 的并行开发，但阻塞 `make smoke` 的正式切换。

### Phase 1：实现最小 Go Runner

- [x] 新增 `tests/host/cmd/xv6test`。
- [x] 实现 `xv6test run --suite smoke`，能启动 QEMU、执行 xv6 shell 命令并匹配输出。
- [x] 支持 timeout 和 QEMU 进程组清理。
- [x] 支持保存 stdout 日志到 `build/test-logs/`。
- [x] 保持 Makefile 启动 QEMU 的参数来源，避免 QEMU 配置重复维护。
- [x] 使用 per-command sentinel 判定命令完成，prompt 只用于初始 shell 就绪。
- [x] 支持按单个 case 执行的 CLI 语义：`xv6test run --suite smoke --case <case>`。
- [x] 继续扩展 suite/case 数据模型，补充 tag、heavy、artifacts、host-only 等字段。
  - `Tags []string` — 已实现，所有 smoke case 打上子系统 + "smoke" 标签。
  - `Heavy bool` — 已预留字段，待 Phase 4 使用。
  - `Artifacts []string` — 已预留字段，暂未在运行器中消费。
  - Host-only — 待后续 Phase 补充，当前所有 case 均需 QEMU。

首批迁移 case：

- `sleep-returns`（已接入 `make test-smoke-go`）
- `pingpong`（已接入 `make test-smoke-go`）
- `primes`（已接入 `make test-smoke-go`）
- `find-current-directory`（已接入 `make test-smoke-go`）
- `find-recursive`（已接入 `make test-smoke-go`）
- `xargs`（已接入 `make test-smoke-go`，使用次数匹配验证 `hello` 输出）
- `trace-32-grep`（已接入 `make test-smoke-go`）
- `trace-all-grep`（已接入 `make test-smoke-go`）
- `trace-nothing`（已接入 `make test-smoke-go`）
- `trace-children`（已接入 `make test-smoke-go`，当前验证 fork trace 数量下限）
- `sysinfotest`（已接入 `make test-smoke-go`）
- `pgtbltest`（已接入 `make test-smoke-go`）
- `pte-printout`（已接入 `make test-smoke-go`，当前验证启动页表打印的关键格式）
- `bttest`（已接入 `make test-smoke-go`，当前验证命令可运行且不 panic）
- `alarmtest`（已接入 `make test-smoke-go`）
- `nettests`（已接入 `make test-smoke-go`，依赖 host-side `make server`）
- `symlinktest`（已接入 `make test-smoke-go`）
- `mmaptest`（已接入 `make test-smoke-go`）

Python 到 Go 的 smoke 迁移对照：

| 原 Python case | Go case | 状态 | 差异 | 后续处理 |
| --- | --- | --- | --- | --- |
| `sleep, no arguments` | 暂未迁移 | 待迁移 | 旧 case 验证无参数行为，当前 Go smoke 只验证 `sleep` 可返回 | 补充无参数错误输出或行为匹配 |
| `sleep, returns` | `sleep-returns` | 已迁移 | 无已知差异 | 保持在 smoke |
| `sleep, makes syscall` | 暂未迁移 | 部分缺口 | 依赖 `sys_sleep` gdb breakpoint，Go runner 暂无 gdbstub / breakpoint 能力 | 在运行器支持 gdbstub 后迁移 |
| `pingpong` | `pingpong` | 已迁移 | 无已知差异 | 保持在 smoke |
| `primes` | `primes` | 已迁移 | 无已知差异 | 保持在 smoke |
| `find, in current directory` | `find-current-directory` | 已迁移 | Go case 使用固定短文件名，避免超过 xv6 `DIRSIZ` | 保持固定测试数据，必要时补充随机化 |
| `find, recursive` | `find-recursive` | 已迁移 | Go case 使用固定短文件名和两层目录 | 后续可补充更多目录分支 |
| `xargs` | `xargs` | 已迁移 | 使用次数断言验证 `hello` 出现 3 次 | 保持在 smoke |
| `trace 32 grep` | `trace-32-grep` | 已迁移 | 无已知差异 | 保持在 smoke |
| `trace all grep` | `trace-all-grep` | 已迁移 | 无已知差异 | 保持在 smoke |
| `trace nothing` | `trace-nothing` | 已迁移 | 无已知差异 | 保持在 smoke |
| `trace children` | `trace-children` | 部分迁移 | 当前只验证 fork trace 数量下限，未验证多 PID 继承集合 | 增加唯一 PID 数量断言 |
| `sysinfotest` | `sysinfotest` | 已迁移 | 无已知差异 | 保持在 smoke |
| `pgtbltest: ugetpid` / `pgaccess` | `pgtbltest` | 已迁移 | Go case 合并为一个 guest 命令并验证关键输出 | 保持在 smoke |
| `pte printout` | `pte-printout` | 部分迁移 | 当前只验证启动页表打印关键格式，未校验 pte 与 pa 对应关系 | 增加结构化断言 |
| `backtrace smoke test` | `bttest` | 部分迁移 | 当前只验证命令可运行且不 panic，未做 `addr2line` 源码位置校验 | 增加 `addr2line` 集成 |
| `alarmtest: test0/test1/test2` | `alarmtest` | 已迁移 | Go case 合并为一个 guest 命令并验证三个通过输出 | 保持在 smoke |
| `nettest: ping/single/multi/DNS` | `nettests` | 已迁移 | Go case 通过 `Background` 启动 `make server`；DNS 阶段依赖访问 `8.8.8.8:53` | 后续由 `qemuMode` 明确网络模式，并评估是否拆分本地 net smoke 与外部 DNS 测试 |
| Python one-liner `symlinktest` | `symlinktest` | 已迁移 | 无已知差异 | 保持在 smoke |
| Python one-liner `mmaptest` | `mmaptest` | 已迁移 | 无已知差异 | 保持在 smoke |

验证方式：

- Go runner 跑出的结果与当前 `make smoke` 中对应 Python one-liner / grader case 等价。
- 当前已验证：`make test-smoke-go` 能通过已接入的首批 smoke case，并将日志保存到 `build/test-logs/smoke/`。

### Phase 2：迁移 smoke suite

- [ ] 用 Go runner 实现 `test-smoke`。
- [ ] 将当前 `make smoke` 的测试范围迁到 Go runner：
  - util
  - syscall
  - net
  - pgtbl
  - traps
  - symlinktest
  - mmaptest
- [ ] 给 `make smoke` 增加过渡实现：内部调用 `make test-smoke`。
- [ ] 保留 Python smoke 对照入口一段时间，例如 `make smoke-py`。

切换 `make smoke` 的准入标准：

- [ ] Go smoke 覆盖当前 Python smoke 的所有非特殊断言 case。
- [ ] 已知不等价项全部列入 Python 到 Go 的 smoke 迁移对照表。
- [ ] `make test-smoke-go` 连续多次通过，且没有残留 QEMU / `make server` 进程。
- [ ] 失败日志足以定位 case、命令、超时和缺失匹配。
- [ ] 失败退出码、timeout 行为、QEMU 清理行为稳定。
- [ ] 至少保留一个 Python smoke 对照入口，例如 `make smoke-py`。

验证方式：

- `make smoke` 与旧 Python smoke 结果一致。
- 记录耗时，确认没有明显变慢。

### Phase 2.5：补齐运行模式

- [ ] 实现 `QemuModeNormal`，用于默认 guest-side xv6 case。
- [ ] 实现 `QemuModeNetForward`，用于需要网络转发或 host-side server 的 case。
- [ ] 实现 `HostOnly`，用于不启动 QEMU 的宿主机侧测试，例如 `notxv6/ph`、`notxv6/barrier`。
- [ ] 让 `nettests` 通过运行模式声明自动启动 `make server`，并明确使用带 host forwarding 的 QEMU 配置。
- [ ] 让 `ph` / `barrier` 直接运行 host binary，不进入 xv6 shell。

验证方式：

- `xv6test list --suite smoke --tags net` 能清楚标出网络相关 case。
- `make test-net` 不依赖隐式的 runner 特判。
- `make test-thread` 能同时覆盖 host-only 和 guest-side `uthread`。

### Phase 3：迁移 per-subsystem suite

- [ ] 实现 `make test-mmap`，替代 `make grade-mmap` 的测试语义。
- [ ] 实现 `make test-cow`。
- [ ] 实现 `make test-traps`。
- [ ] 实现 `make test-net`。
- [ ] 实现 `make test-thread`，覆盖 host-only `notxv6/ph`、`notxv6/barrier` 和 guest `uthread`。
- [ ] 实现 `make test-lock`。
- [ ] 实现 `make test-fs`。

迁移规则：

- 每迁移一个 suite，保留旧 Python 入口作为对照。
- 新入口不输出分值，只输出 pass / fail、耗时和失败日志位置。
- 如果旧 grader 的分值只是课程历史信息，不迁入新 runner。

验证方式：

- 每个 `make test-*` 与对应旧 `make grade-*` 在通过/失败上保持一致。

### Phase 4：拆分 usertests 与重型测试

- [ ] 梳理 `user/usertests.c` 中适合轻量化的 case。
- [ ] 建立 `test-usertests-smoke` 或等价入口，只跑低成本 case。
- [ ] 保留完整 `usertests` 在 heavy suite。
- [ ] 将 `bigfile` 固定放入 heavy 或 fs-heavy suite。
- [ ] 将 lock 全量压力项放入 heavy 或 lock-heavy suite。

建议分层：

- `test-smoke`：提交前快速反馈。
- `test-regression`：日常中等回归。
- `test-heavy`：完整大文件、完整 usertests、lock/fs 压力。
- `test-stress`：长时间压力或循环测试，默认不跑。

验证方式：

- `test-smoke` 不包含 `bigfile`、完整 `usertests`、完整 lock 压力。
- `test-heavy` 覆盖这些重型项，并在 README 中说明耗时预期。

### Phase 5：Python grader 归档与退场

- [ ] 将 `gradelib.py` 和 `graders/grade-lab-*` 作为 legacy 对照优先归档，例如移入 `tests/legacy/` 或 `legacy/graders/`。
- [ ] 默认入口不再调用 Python runner。
- [ ] README 不再推荐 `grade-*` 作为日常测试入口。
- [ ] 保留 Python grader 一段时间，用于对照与回归定位。
- [ ] 移除 `grade-*` 作为主入口。
- [ ] 如需兼容，保留短期 alias：
  - `make grade-mmap` 打印迁移提示并调用 `make test-mmap`。
  - `make grade-all` 打印迁移提示并调用 `make test-all`。
- [ ] 更新 `README.md`、`AGENTS.md`、`docs/TODO.md`、`docs/lab-migration-plan.md`。

验证方式：

- 干净仓库中执行 `make test-smoke`、`make test-regression`、`make test-heavy`。
- 确认默认测试入口没有 Python runner 依赖。

## 6. Makefile 迁移计划

### 6.1 新入口

新增：

```text
test-smoke
test-regression
test-heavy
test-all
test-util
test-syscall
test-net
test-pgtbl
test-traps
test-cow
test-thread
test-lock
test-fs
test-mmap
```

### 6.2 过渡入口

保留但不继续扩展：

```text
smoke
regression
grade-*
grade-all
grade-all-heavy
```

过渡期行为：

- `smoke` 调用 `test-smoke`。
- `regression` 调用 `test-regression`。
- `grade-*` 调用对应 `test-*`，并可输出一行迁移提示。
- `grade-all-heavy` 调用 `test-heavy`。

### 6.3 最终入口

长期文档只推荐：

```text
make test-smoke
make test-regression
make test-heavy
make test-all
make test-<subsystem>
```

## 7. 报告与日志

Go runner 应统一管理测试输出：

- 成功时输出：
  - suite 名称；
  - case 数；
  - 总耗时；
  - 每个失败 case 的摘要。
- 失败时保存：
  - QEMU stdout；
  - host-side runner 日志；
  - case 配置；
  - 可选 `packets.pcap`。
- 可选输出：
  - `--json build/test-logs/result.json`
  - `--junit build/test-logs/junit.xml`

建议目录：

```text
build/test-logs/
  smoke/
  regression/
  heavy/
```

## 8. CI 接入计划

CI 迁移应分阶段推进，不要一开始就把重型 QEMU 测试放进默认检查。

第一阶段：

- [ ] 运行 `go test ./...`，验证 host-side runner 编译和基础逻辑。
- [ ] 运行 `make build` 或等价构建目标，验证内核和用户程序可构建。
- [ ] 不启动 QEMU，降低 CI 环境依赖。

第二阶段：

- [ ] 运行 `make test-smoke`，覆盖 Go smoke suite。
- [ ] 保存 `build/test-logs/` 作为失败 artifact。
- [ ] 明确 QEMU、RISC-V toolchain 和网络转发依赖。

第三阶段：

- [ ] 将 `test-heavy`、完整 `usertests`、`bigfile` 和 lock/fs 压力测试设为手动触发或定时任务。
- [ ] 可选输出 JSON 或 JUnit XML，便于 CI 展示 case 级结果。

## 9. 风险与约束

- QEMU / gdbstub 控制逻辑迁移风险较高，必须保留旧 Python runner 做一段时间对照。
- Go runner 仍依赖 xv6 shell prompt `$ ` 判断初始 shell 就绪；后续命令完成已使用 sentinel 判定。
- 当前默认 per-case QEMU 隔离性强但耗时更高；per-suite QEMU 复用只能作为后续优化，并且必须处理状态污染。
- `qemuMode` / host-only 运行模式尚未落地，迁移 `thread`、`net` 等 suite 前应先补齐运行模式。
- `nettests` 的 DNS 阶段依赖外部网络，CI 或受限网络环境中可能不稳定；后续应拆分本地网络 smoke 与外部 DNS 检查。
- 过早删除 `grade-*` 会破坏已有使用习惯，建议先 alias 再退场。
- 不要在迁移 runner 的同时大规模修改 guest-side C 测试，否则很难定位回归来源。
- 不要把分值概念带入新 runner；新 runner 只关心 pass / fail / skip / timeout。
- 不要默认运行重型测试，避免 `bigfile` 和完整 `usertests` 拖慢日常反馈。

## 10. 第一批可执行任务

1. 新增 `tests/host/cmd/xv6test` skeleton。
2. 实现最小命令：`xv6test run --suite smoke --case mmaptest`。
3. 实现 QEMU 启动、shell 命令注入、regex 匹配、timeout、日志保存。
4. 用 Go runner 复刻当前 `make smoke` 中可用通用 regex / 次数匹配表达的 util、syscall、pgtbl、traps、net、fs、mmap case。
5. 新增 `make test-smoke-go` 作为并行入口，不替换现有 `make smoke`。（已完成）
6. 比较 Go runner 与当前 Python runner 的输出、耗时和失败日志质量。
7. 稳定后将 `make smoke` 切换到 Go runner，并保留 `make smoke-py` 作为短期回退。

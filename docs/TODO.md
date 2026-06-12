# TODO

## 近期任务

- 完成 `fs` lab 在 `dev/all` 上的迁移、联调和回归验证。
- 完成 `mmap` lab 在 `dev/all` 上的迁移、联调和回归验证。
- 在 `fs` 和 `mmap` 迁移完成后，再统一收口测试框架和工程化改进，避免中途同时改功能代码和基础设施。

## 测试框架迁移

- 将当前 Python grader/测试编排脚本逐步迁移到 Go。
- 保留“宿主机外部驱动 QEMU + xv6 内部测试程序自检”的总体测试模型，不引入内核内大型测试框架。
- 测试框架迁移后的主目标不是复刻课程 grader 的分数模型，而是建立更适合持续开发的功能测试体系。
- 迁移时优先抽象公共能力：
  - QEMU 启动与关闭
  - 超时控制
  - 串口输出采集与匹配
  - 测试失败日志落盘
  - 端口/环境准备
- 初期保留每个 lab 的独立 grader 入口，避免一开始就把测试定义过度集中化。

## 测试入口重构方向

- 保留 `grade-*`，但降级为兼容回归入口。
- `grade-*` 不再作为日常开发主入口，只在里程碑、发布前或对照课程 grader 时运行。
- 可以新增 `make test-legacy` 统一承接课程 grader 兼容回归。
- 新建按功能分类的测试入口：
  - `make test-smoke`
  - `make test-proc`
  - `make test-vm`
  - `make test-fs`
  - `make test-syscall`
  - `make test-net`
  - `make test-lock`
  - `make test-user`
  - `make test-all`

## 测试组织优化

- 继续把 grader 共性逻辑下沉到公共库层，suite 级脚本只保留测试定义和少量特例。
- 明确区分“构建一次”和“重复运行测试用例”的职责，减少重复准备逻辑。
- 给长耗时测试补充更清楚的阶段输出，帮助区分“正常慢”与“异常卡住”。
- 已提供临时开发回归脚本 `./quick.sh`；后续再把 `quick` / `full` 两档执行模式正式并入测试系统。
- 保持测试输出稳定，避免写过于脆弱的精确文本匹配。
- `make grade-all` 的长期方向不是继续按 `util -> syscall -> ... -> lock` 一条龙串行执行，而是映射为功能测试集合。
- 支持单次启动 QEMU 执行一组 xv6 命令，减少“每个 case 启一次 QEMU”的整机重启开销。
- 把 `usertests` 拆成更小的功能回归集，例如 `proc`、`fs`、`vm` 子集，而不是长期依赖整包大测试。
- 支持增量测试建议：
  - 修改 `kernel/bio.c` 时，优先建议跑 `test-lock` 和 `test-fs`
  - 修改 `kernel/vm.c` 时，优先建议跑 `test-vm` 和 `test-user`
  - 修改 `kernel/proc.c` 时，优先建议跑 `test-proc`
- 区分默认开发回归和扩展压力测试，不把 `kalloctest`、`ph_fast`、完整 `usertests` 这类重型项放进每次默认开发循环。

## 测试分级

- `smoke`：目标 30 秒内，提交前必跑。
- `core`：目标 2 到 5 分钟，作为日常回归主力。
- `full`：完整系统回归，在阶段性验证、发布前或 CI 中运行。
- `stress`：长时压力测试，不默认运行。

## 最小测试集设计

- 每个功能分类都提供最小可用测试集，避免默认执行重型全量回归。
- 示例：
  - `test-vm-smoke`：`cowtest simple`、`pgaccess`、`ugetpid`
  - `test-fs-smoke`：`bigfile`、`dirfile`、`bigdir`
  - `test-lock-smoke`：`kalloctest test1/test2`、`bcachetest`

## 功能测试拆分建议

- `proc`
  - `forktest`
  - `exitwait`
  - `killstatus`
  - `preempt`
- `vm`
  - `cowtest`
  - `pgaccess`
  - `ugetpid`
  - `sbrkmuch`
- `fs`
  - `bigfile`
  - `dirfile`
  - `bigdir`
  - `iref`
  - `openiput`
- `syscall`
  - `trace`
  - `sysinfotest`
  - `alarmtest`
- `net`
  - `nettests`
- `lock`
  - `kalloctest`
  - `bcachetest`
  - `stats`
- `user`
  - 保留拆分后的 `usertests` 子集，而不是继续依赖整个大包

## 测试清单与目录形态

- 迁移后的长期目录目标可以类似：
  - `tests/smoke/`
  - `tests/proc/`
  - `tests/vm/`
  - `tests/fs/`
  - `tests/syscall/`
  - `tests/net/`
  - `tests/lock/`
  - `tests/user/`
  - `scripts/test_runner.go`
  - `scripts/qemu_expect.go`
- 每个测试建议定义为 manifest 或结构化数据，至少包含：
  - 测试名
  - 所属 suite
  - 超时时间
  - xv6 内执行命令
  - 期望输出
- 长期目标是摆脱课程 grader 的“分数模型”，转向自己的测试清单模型。

## 输出格式目标

- 测试输出以 `PASS/FAIL`、用例名称、运行时长、失败日志路径、子系统标签为主。
- 不再把 `Score: 69/69` 作为主要反馈形式。
- 目标示例：
  - `[PASS] vm:cow_simple 3.2s`
  - `[PASS] fs:bigdir 5.8s`
  - `[FAIL] lock:bcache_stress 22.1s`

## 迁移顺序建议

- 先保留现有 `grade-*`，同时新增 `make test-*`。
- 先把 `grade-all` 中的内容映射到功能分类测试集合。
- 优先把 `usertests` 中高价值项拆出来独立运行。
- 再实现统一的 Go `test_runner`，逐步替代按 lab 划分的课程 grader。
- 最后再把 `grade-*` 挪到 `legacy` 层，或只保留兼容入口。

## 文档与维护

- 在测试框架迁移到 Go 后，补充一份新的测试架构说明文档，说明包结构、执行流程和扩展方式。
- 在 `README.md` 中补充常见长耗时测试说明，标明哪些测试慢属于正常现象。

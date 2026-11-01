# Development Roadmap

本文档只记录 `dev/all` 当前仍值得推进的工作。10 个 lab 的整合历史见 `lab-migration-plan.md`，Go runner 的迁移过程见 `test-migrate.md`，Picolibc 的详细状态见 `picolibc.md`。

## 当前基线

- 10 个 6.S081 lab 已整合到 `dev/all`。
- syscall 分发、参数解析以及 fd、mmap、网络 handler 已按职责拆分。
- native 用户态已建立统一 crt0，ulibc 已按字符串、文件和 USYSCALL helper 拆分。
- Go runner 已成为默认测试入口：quick 11 case、smoke 40 case，并支持 per-subsystem、heavy、HostOnly 和网络转发模式。
- Picolibc 独立链路已支持启动、内存、文件 I/O、stdio、seek、dup/pipe、目录遍历、stat、时间、随机数、regex 和多个实际工具。
- raw `getdents`、Picolibc `opendir/readdir/closedir/rewinddir/dirfd` 以及 stat 类型映射已经完成。

## P0：统一 VM Fault 路径

这是当前最高优先级，也是下一阶段最适合作为简历核心成果的内核专题。

- [ ] 为现有行为补齐基线测试。
  - 覆盖 load/store page fault、COW、mmap lazy fault、非法地址和权限错误。
  - 补充 COW 与 `copyout()`、mmap 与 `copyin/copyout/copyinstr` 的交互测试。
  - 验收：新增测试在重构前通过，并能在故意破坏对应路径时失败。
- [ ] 提取统一的 user page-fault dispatcher。
  - 从 `usertrap()` 中抽出 `handle_user_page_fault(scause, stval)`。
  - 明确 load fault、store fault、COW 和 mmap lazy fault 的判定顺序。
  - 验收：`trap.c` 只负责 trap 分类和进程终止决策。
- [ ] 拆分 COW fault helper。
  - 集中处理单引用页提权、复制页、PTE flags、引用计数和失败回滚。
  - 统一 trap fault 与 `copyout()` 触发 COW 时的核心逻辑。
  - 验收：`make test-cow`、`make test-mmap`、`make test-traps`、`make test-smoke` 通过。
- [ ] 明确 mmap fault 模块边界。
  - 将 `find_vma()`、`mmap_fault()`、拆映射和清理逻辑从 `proc.c` 移到独立 `vma.c` 或 `mmap.c`。
  - `proc.c` 只保留 fork、exit 等生命周期调用点。
  - 验收：模块职责、锁和 file reference 所有权在文档中可说明。

## P1：补强 mmap 语义

P1 在 fault-path 重构稳定后推进，不与 P0 同时大改。

- [ ] 完善 mmap 参数校验。
  - 检查非页对齐 offset、非法 `prot`/`flags`、非零 `addr` 和地址溢出。
- [ ] 支持中间区间 `munmap()`。
  - 实现 VMA split，正确更新 file reference、offset、length 和 `MAP_SHARED` 写回。
- [ ] 审计 mmap fork 语义。
  - 明确 `MAP_PRIVATE` 快照/COW 和 `MAP_SHARED` 父子可见性。
- [ ] 修正 exec 与 mmap 清理的两阶段语义。
  - 失败的 `exec()` 不得提前破坏旧地址空间或丢失映射状态。
- [ ] 支持 VMA hole reuse。
  - 反复 mmap/munmap 不应只增加 `mmap_top` 并最终耗尽地址区间。

P1 完成标准：新增边界测试通过，且 `make test-mmap`、`make test-cow`、`make test-fs`、`make test-smoke` 无回归。

## P2：内核可观测性与文件系统

这一阶段只选择一个有量化结果的专题，不同时铺开。

- [ ] 建立 `docs/kernel-lifetime-notes.md`。
  - 记录 file/inode/VMA/COW page/buffer cache 的引用和所有权转移。
- [ ] 为 WAL 增加统计。
  - 统计 commit、logged/absorbed/installed blocks 和等待原因，为优化提供基线。
- [ ] 优化 WAL 正常提交路径。
  - 区分 normal commit 与 recovery replay，减少不必要的日志区读回和 block copy。
- [ ] 评估 buffer cache 淘汰策略。
  - 在现有 bucketed cache 基础上比较全桶近似 LRU 与分代/冷热策略。

涉及性能优化时必须先记录基线 workload、统计口径和正确性测试，再实现优化。

## P3：项目交付质量

- [ ] 建立 CI。
  - 第一阶段运行 `go test ./...`、`make build` 和格式检查。
  - 第二阶段加入 `make test-quick`，保存失败日志。
  - heavy suite 只做手动或定时运行。
- [ ] 为 VM/mmap 专题增加设计文档。
  - 记录问题、旧路径、设计不变量、失败回滚和测试矩阵。
- [ ] 准备可复现演示。
  - 展示 QEMU 启动、核心测试、Picolibc 工具和失败日志。
- [ ] 阶段性运行完整回归并记录结果。
  - 至少包括 `make test-smoke`、相关 subsystem suite 和一次 heavy/legacy 对照。

## Picolibc 收束线

Picolibc 当前作为用户态兼容性和内核 syscall 的验证平台，不再优先追求完整 POSIX。

- [ ] 实现简版 `picols`：支持无参数、目录参数和普通文件参数。
- [ ] 在 VM 主线完成后再评估 `lstat/readlink` 和 `picols -l`。
- [ ] 暂缓 `rename`、`getcwd`、完整 `fcntl`、`scandir` 和 `pbox`。

## 每次提交的验证

- [ ] `git diff --check`
- [ ] 对修改过的 C/头文件运行 `clang-format -i`
- [ ] 对修改过的头文件运行 `clangd --check=<file>`
- [ ] 运行覆盖改动范围的最小 suite
- [ ] 内核机制改动至少运行 `make test-quick` 和对应 subsystem suite
- [ ] 阶段性内核提交运行 `make test-smoke`

## 推荐执行顺序

1. VM fault 基线测试。
2. page-fault dispatcher 与 COW helper。
3. VMA 模块边界整理。
4. mmap fork、munmap、exec 生命周期语义。
5. CI、设计文档和演示。
6. 从 WAL 或 buffer cache 中选择一个可量化专题。

完成第 4 步后，项目将形成一条完整的内核成果链：问题审计、机制设计、实现重构、边界测试和回归验证，而不只是 lab 功能集合。

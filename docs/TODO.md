# TODO

当前 `dev/all` 已完成 `util`、`syscall`、`pgtbl`、`traps`、`cow`、`thread`、`net`、`lock`、`fs`、`mmap` 的功能整合。后续重点从“迁移 lab”转为“整理集成后的教学 OS 工程质量”。

## P0：稳定基线与工程地基

- 推送当前 `dev/all` 稳定点，作为“所有 lab 已集成”的远端基线。
- 重构 `Makefile`，把构建产物移出源码目录：
  - 目标目录建议为 `build/kernel/`、`build/user/`、`build/mkfs/`、`build/fs.img`。
  - 移出 `*.o`、`*.d`、`*.asm`、`*.sym`、`user/_*`、`kernel/kernel`、`mkfs/mkfs`、`fs.img`。
  - 保持现有 `make qemu`、`make grade-*`、`make clean` 入口兼容。
  - 避免多个 grader 并行或连续运行时互相清理构建产物。
- 整理 `.gitignore`，确保生成物、QEMU 输出、pcap、临时日志不会污染工作区。
- 给 `Makefile` 增加清晰分层目标：
  - `make build`
  - `make image`
  - `make smoke`
  - `make grade-<lab>`
  - `make regression`
  - `make grade-all-heavy`

## P1：VM 与缺页路径整理

- 把 `trap.c` 中的用户态 page fault 处理抽成独立函数，例如 `handle_user_page_fault(scause, stval)`。
- 明确 fault 分派优先级：
  - COW store fault
  - mmap load/store fault
  - 非法访问并 kill
- 把 COW 处理逻辑从 `usertrap()` 中拆出，例如 `cow_fault(pagetable, va)`。
- 把 mmap fault 处理逻辑和 VMA 查询逻辑集中到更明确的模块边界，避免 `proc.c` 继续膨胀。
- 增加注释说明 `scause=13`、`scause=15`、`PTE_COW`、VMA lazy page 的交互关系。
- 保持 `alarm` 和 device interrupt 路径不被 page fault 重构影响。

## P2：mmap 语义补强

- 支持 VMA hole reuse，避免 `mmap_top` 只增不减导致长运行进程耗尽 mmap 区间。
- 支持中间 `munmap` 的 VMA split，而不仅是 whole / prefix / suffix。
- 严格校验 mmap 参数：
  - `offset` 页对齐
  - `prot` 只包含合法位
  - `flags` 必须且只能包含 `MAP_SHARED` 或 `MAP_PRIVATE`
  - `addr` 当前不支持时应明确忽略或拒绝
- 审计 `PROT_NONE`、`PROT_EXEC`、只写映射等边界语义。
- 评估 `copyin`、`copyout`、`copyinstr` 遇到 lazy mmap 页时是否应主动 fault-in，避免只有用户态 load/store 才能触发映射。
- 为 `MAP_SHARED` 写回增加更精确的策略：
  - 当前保守写回已 fault-in 页。
  - 后续可评估 dirty bit 或软件 dirty 标记。

## P3：资源生命周期与错误路径审计

- 审计 `fork()` 失败路径：
  - VMA `filedup()` 后如果 `uvmcopy()` 失败，必须释放子进程 VMA file refs。
  - open file、cwd、VMA 的引用释放顺序要一致。
- 审计 `exec()` 与 mmap 的关系：
  - 当前进程地址空间被替换前是否需要清理所有 VMA。
  - `MAP_SHARED` 页面是否应在 `exec()` 前写回。
- 审计 `exit()` 中 mmap 写回失败策略：
  - xv6 风格下可以忽略错误，但应在代码注释中说明。
  - 避免失败路径泄露 file refs 或物理页。
- 审计 `mappages()`、`kalloc()`、`readi()`、`writei()` 失败后的资源回收。
- 审计 COW 引用计数与 `uvmunmap()`、`uvmunmap_mmap()`、`copyout()` 的交互。

## P4：锁与引用关系文档化

- 新增一份短文档，例如 `docs/kernel-lifetime-notes.md`，记录关键所有权关系：
  - `struct file` refcount：`open`、`dup`、`fork`、`mmap`、`close`、`exit`
  - `struct inode` lock/ref：`ilock`、`iput`、`iunlockput`
  - COW physical page refcount：`uvmcopy`、page fault、`kfree`
  - VMA file refs：`mmap`、`munmap`、`fork`、`exit`
- 记录常见锁顺序，尤其是 `wait_lock`、`p->lock`、inode sleeplock、bcache bucket lock。
- 给复杂路径补充最少量注释，优先解释“不变量”和“为什么这么做”，不要解释显而易见的赋值。

## P5：测试系统重构

- 保留 `grade-*` 作为课程 grader 兼容入口，但不要把它作为日常开发唯一入口。
- 将 `quick.sh` 正式并入 `Makefile`，形成 `make smoke`。
- 新增分层测试入口：
  - `make smoke`：提交前快速检查，目标 30 到 90 秒。
  - `make regression`：日常中等回归，目标 2 到 5 分钟。
  - `make grade-all-heavy`：完整重型回归，包含 `bigfile`、完整 `usertests`、lock/fs 压力项。
  - `make stress`：长时压力测试，默认不跑。
- 拆分 `usertests`：
  - `proc`：`forktest`、`exitwait`、`killstatus`、`preempt`
  - `vm`：`cowtest`、`pgaccess`、`ugetpid`、`sbrkmuch`
  - `fs`：`bigfile`、`dirfile`、`bigdir`、`iref`、`openiput`
  - `syscall`：`trace`、`sysinfotest`、`alarmtest`
  - `net`：`nettests`
  - `lock`：`kalloctest`、`bcachetest`、`stats`
- 明确默认测试不包含以下重型项：
  - `bigfile`
  - `usertests writebig`
  - 完整 `usertests`
  - `grade-lab-lock` 全量压力项
- 支持单次启动 QEMU 执行一组 xv6 命令，减少每个 case 重启 QEMU 的成本。
- 长期可把 Python grader 公共逻辑迁移到 Go，但应在 Makefile 和测试分层稳定后再做。

## P6：文件系统大文件测试成本优化

- 明确区分“基础 FS 正确性”和“double-indirect 最大文件”测试。
- 避免让所有完整 `usertests` 默认继承放大后的 `MAXFILE` 写盘成本。
- 可以考虑为 `usertests writebig` 使用单独较小上限，保留 `bigfile` 专门测试 65,803 blocks。
- 在 README 中持续维护长耗时测试说明，标明哪些慢属于正常现象。

## P7：CI 与发布流程

- 默认 CI 只跑轻量或中等回归：
  - `make smoke`
  - `make grade-mmap`
  - `make grade-cow`
  - `make grade-traps`
- 重型回归用手动触发或夜间任务：
  - `make grade-all-heavy`
  - `make stress`
- 每次提交前至少跑：
  - `git diff --check`
  - `make kernel/kernel fs.img`
  - 与改动子系统对应的定向测试
- 每次阶段性合并前跑完整重型回归，并记录耗时和失败日志路径。

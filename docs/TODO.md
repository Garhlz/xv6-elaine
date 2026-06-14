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
- 持续审计 `copyin`、`copyout`、`copyinstr` 对 lazy mmap 页的主动 fault-in 行为；当前已覆盖 `read(fd, fresh_mmap_addr, n)`，后续继续补充跨页、字符串和错误权限路径。
- 为 `MAP_SHARED` 写回增加更精确的策略：
  - 当前保守写回已 fault-in 页。
  - 后续可评估 dirty bit 或软件 dirty 标记。

## P3：资源生命周期与错误路径审计

- 审计 `fork()` 失败路径：
  - VMA `filedup()` 后如果 `uvmcopy()` 失败，必须释放子进程 VMA file refs。
  - open file、cwd、VMA 的引用释放顺序要一致。
- 补齐完整 Unix mmap fork 语义：
  - `MAP_PRIVATE` 已 fault-in 且已修改页面应在 fork 时形成快照。
  - `MAP_SHARED` 已 fault-in 页面应在父子之间保持共享或至少保持可见的一致写回语义。
- 审计 `exec()` 与 mmap 的关系：
  - 当前实现会在提交新地址空间前清理所有 VMA，但失败路径可能已经部分拆掉旧映射。
  - 后续应拆成 `flush all` 和 `discard all` 两阶段：先完成所有可失败写回，再执行不可失败拆映射。
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

## P7：缓冲缓存 LRU 策略优化

当前 `bio.c` 使用近似 LRU：`brelse()` 打 `timestamp`（ticks），`bget()` miss 时全桶扫描选最老 victim。这个方案有两个明显不足：

- **全桶扫描成本高**：每次 miss 都要遍历所有桶的所有 buf 去比大小，随 NBUF 增大线性恶化。
- **单次访问即可"翻新"**：只要 refcnt 短暂 > 0（哪怕只是日志层 bpin/bunpin 的一瞬间），`brelse()` 就会更新 timestamp，导致只被用过一两次的冷块和反复访问的热块在 LRU 序上无差别。

计划方向：引入接近 InnoDB-style 的 **old / young 双链表**（或称 old / young 分区）：

- 将每个桶的链表拆为 **old 区**（靠近哨兵头，候选淘汰）和 **young 区**（远离哨兵头，受保护）。
- 新块首次插入时进入 old 区的中间位置（而非链表头），给它一个"考察期"。
- 只有当块在 old 区期间被再次访问（refcnt 从 0 升到 > 0 再回到 0），才晋升到 young 区。
- young 区满时，最久未访问的 young 块降级回 old 区。
- 淘汰永远从 old 区头部选取，不再需要全桶扫描 timestamp。
- 保留 `evict_lock` 的串行化角色；old/young 迁移逻辑在持对应桶锁下完成。
- `bpin/bunpin` 仅影响 refcnt，不作为晋升依据（避免日志层的短暂 pin 触发晋升）。

这样做的好处：

- **O(1) 淘汰**：old 区头部就是 victim，无需扫描。
- **抵抗扫描污染**：一次性的大文件顺序读（扫描大量块但每个只用一次）不会把热块挤出缓存——扫描来的块只停留在 old 区，用完就被淘汰。
- **语义清晰**：old = "可能只被用一次的冷数据"，young = "多次被访问的热数据"。

实现时的注意事项：

- old/young 的比例需要可调（例如通过 `#define OLD_PCT 37`，按百分比划分）。
- 晋升和降级的时机要精确文档化——特别是和 `bpin/bunpin`、`brelse` 的交互。
- 必须保持 `bread/bwrite/brelse/bpin/bunpin/binit` 的外部行为不变。
- 重构后需要全量回归：`grade-fs`（bigfile + symlinktest + usertests）、`grade-lock`（bcache 竞争压力）、`grade-mmap`（mmap 写回路径走 bcache）。

## P8：日志系统 WAL 提交路径优化

当前 `log.c` 的 `commit()` 在 `write_head()` 之后通过 `install_trans(0)`
将日志区块的内容反向拷贝回 home block。正常提交路径下这是冗余的——home buffer
已被 `log_write()` 调用 `bpin()` 钉在缓存中，其 `data[]` 就是事务的最终内容。

### 核心优化：正常提交直接安装 pinned home buffer（推荐优先级最高）

**现状**：

```
write_log()     → home → log 区 (bread + memmove + bwrite)
write_head()    → commit point
install_trans() → log 区 → home (bread + memmove + bwrite + bunpin)  ← 冗余
clear head
```

**优化后**：

```
write_log()              → home → log 区
write_head()             → commit point
install_trans_normal()   → 直接 bwrite(home buffer) + bunpin        ← 跳过 log 反向读+拷贝
clear head
```

做法：将 `install_trans()` 拆为两个版本：

- `install_trans_recover()` — 恢复路径，从日志区 `bread()` 并重放到数据区。
- `install_trans_normal()` — 正常提交路径，直接 `bwrite(home buffer)` + `bunpin()`，
  利用 `log_write()` 已 pin 的 home buffer 绕过日志区反向拷贝。

收益：正常提交每块省去一次 `bread(log block)` + 一次 `BSIZE memmove`（1024B 拷贝）+
一次 buffer cache 查找和锁开销。恢复路径保持不变，WAL 崩溃一致性不变。

**关键正确性条件**：

1. `write_log()` 必须先于 `write_head()`（WAL 顺序）
2. `write_head()` 必须先于 `install_trans_normal()`（提交点先于安装）
3. `install_trans_normal()` 只能用于正常 commit，不能用于 recovery
4. recovery 仍然必须从日志区 `bread()` 并 replay
5. `log_write()` 必须 `bpin()` home buffer；`install_trans_normal()` 写完必须 `bunpin()`
6. commit 期间 `log.committing == 1`，阻止新 FS 修改混入

### 进阶优化：内存日志中缓存 pinned buffer 指针（方案 1 稳定后考虑）

在 `struct log` 中增加 `struct buf *bufs[LOGSIZE]`（纯内存字段，不写盘），
`log_write()` 时记录 pin 的 buf 指针，后续 `write_log()` 和 `install_trans_normal()`
直接用指针访问而非按块号重新 `bread()`。进一步减少 buffer cache 查找开销。

注意：通过 `bufs[]` 直接访问 `data[]` 前应持有 `b->lock`（即便 `outstanding==0`
时理论上无并发修改者，持锁语义更完整）。

### 防御性加固

- `read_head()` 增加日志头边界检查：`lh->n < 0 || lh->n > LOGSIZE || lh->n > log.size - 1`。
  防止磁盘日志头损坏导致恢复路径越界。
- `bunpin()` 和 `brelse()` 增加 `refcnt < 1` 的 `panic` 检查，及早暴露 pin/unpin 不匹配。

### 可观测性：日志统计

在 `struct log` 中增加 `logstats`（commits、logged_blocks、absorbed_blocks、
begin_sleep_commit、begin_sleep_space、install_blocks），在相应路径更新，
通过 `sysinfo` 或新 syscall 暴露，用于验证优化效果。

### 不建议做的

- **metadata-only journaling** — 改变语义，非小优化。
- **允许 commit 期间新事务进入** — 需要双缓冲或 multiple generation，复杂度高。
- **异步 commit** — xv6 无 fsync 语义，会导致返回和持久化不一致。
- **checksum / sequence number** — 会改磁盘格式，不适合作为第一步。

### 验证要求

实现后需要全量回归：`grade-fs`（bigfile + symlinktest + usertests）、`grade-mmap`、
`grade-lock`，确认真正常提交路径和崩溃恢复路径均行为正确。

## P9：CI 与发布流程

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

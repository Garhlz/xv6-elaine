# TODO / Roadmap

本文档记录 `dev/all` 分支在完成 6.S081 / 6.828 2021 xv6 labs 功能整合之后的维护计划。它不是课程 hand-in 清单，而是后续工程化、代码审计、测试分层和文档补强的路线图。

## 1. 当前状态概览

- 当前 `dev/all` 已整合 `util`、`syscall`、`pgtbl`、`traps`、`cow`、`thread`、`net`、`lock`、`fs`、`mmap`。
- 当前活动 lab 配置仍是 `conf/lab.mk` 中的 `LAB=net`，用于保留 net 相关编译宏和 QEMU 网络配置；其他 lab 功能在 `dev/all` 中无条件集成。
- 构建产物已统一输出到 `build/`，课程 grader 脚本已统一移动到 `graders/`，日常入口以 `make build`、`make image`、`make smoke`、`make regression`、`make grade-*` 为主。
- 迁移历史与各 lab 取舍记录在 `docs/lab-migration-plan.md`；本文件只跟踪迁移完成后的后续工作。
- 最近一次工程基线验证包含 `make smoke`；完整重型回归仍应在阶段性合并前单独运行。

## 2. 已完成内容

### 2.1 Lab 功能整合

- [x] 完成 `util` lab 集成。
  - 涉及模块：`user/sleep.c`、`user/pingpong.c`、`user/primes.c`、`user/find.c`、`user/xargs.c`、`user/xargstest.sh`、`Makefile`。
  - 完成内容：加入用户态工具和对应 `UPROGS` / `UEXTRA` 条目。
  - 验证方式：`make grade-util`、`make smoke`、`make grade-all`。
- [x] 完成 `syscall` lab 集成。
  - 涉及模块：`kernel/syscall.c`、`kernel/sysproc.c`、`kernel/proc.c`、`kernel/kalloc.c`、`kernel/sysinfo.h`、`user/trace.c`、`user/sysinfotest.c`。
  - 完成内容：实现 `trace`、`sysinfo`，并保留 net lab 需要的 syscall 条目。
  - 验证方式：`make grade-syscall`、`make smoke`。
- [x] 完成 `pgtbl` lab 集成。
  - 涉及模块：`kernel/proc.c`、`kernel/vm.c`、`kernel/riscv.h`、`user/pgtbltest.c`。
  - 完成内容：集成 `ugetpid`、`vmprint`、`pgaccess`。
  - 验证方式：`make grade-pgtbl`、`make smoke`。
- [x] 完成 `traps` lab 集成。
  - 涉及模块：`kernel/trap.c`、`kernel/proc.c`、`user/alarmtest.c`。
  - 完成内容：集成 backtrace 与 alarm 相关路径。
  - 验证方式：`make grade-traps`、`make smoke`。
- [x] 完成 `cow` lab 集成。
  - 涉及模块：`kernel/vm.c`、`kernel/kalloc.c`、`kernel/trap.c`、`user/cowtest.c`。
  - 完成内容：集成 copy-on-write fork、页引用计数和 COW fault 处理。
  - 验证方式：`make grade-cow`、`make regression`。
- [x] 完成 `thread` lab 集成。
  - 涉及模块：`user/uthread.c`、`user/uthread_switch.S`、`notxv6/ph.c`、`notxv6/barrier.c`、`graders/grade-lab-thread`。
  - 完成内容：集成用户态线程切换、pthread 哈希表练习和 barrier 练习。
  - 验证方式：`make grade-thread`。
- [x] 完成 `net` lab 集成。
  - 涉及模块：`kernel/e1000.c`、`kernel/net.c`、`kernel/sysnet.c`、`user/nettests.c`、`server.py`、`ping.py`。
  - 完成内容：集成 E1000 收发路径和 nettests 所需 UDP 通路。
  - 验证方式：`make grade-net`、`make smoke`。
- [x] 完成 `lock` lab 集成。
  - 涉及模块：`kernel/kalloc.c`、`kernel/bio.c`、`kernel/stats.c`、`user/kalloctest.c`、`user/bcachetest.c`、`user/stats.c`。
  - 完成内容：集成 per-CPU allocator、bucketed buffer cache 和统计接口。
  - 验证方式：`make grade-lock`、`make regression`。
- [x] 完成 `fs` lab 集成。
  - 涉及模块：`kernel/fs.c`、`kernel/file.c`、`kernel/sysfile.c`、`kernel/param.h`、`user/bigfile.c`、`user/symlinktest.c`。
  - 完成内容：集成 double-indirect block、symbolic link 和相关用户态测试。
  - 验证方式：`make grade-fs`、`make grade-all`。
- [x] 完成 `mmap` lab 集成。
  - 涉及模块：`kernel/proc.c`、`kernel/proc.h`、`kernel/sysfile.c`、`kernel/vm.c`、`kernel/trap.c`、`user/mmaptest.c`。
  - 完成内容：实现 file-backed lazy mmap、`munmap`、基本 `MAP_SHARED` 写回和 syscall buffer 指向 fresh mmap 页的 lazy fault-in。
  - 验证方式：`make grade-mmap`、`make smoke`。

### 2.2 构建与目录基线

- [x] 整理 `Makefile` 输出路径。
  - 涉及模块：`Makefile`、`mkfs/mkfs.c`。
  - 完成内容：将 kernel、user programs、host tools、文件系统镜像和调试产物输出到 `build/`；`mkfs` 使用 basename 写入 xv6 image，支持 `build/user/_*` 输入路径。
  - 验证方式：`make clean && make build image`，并确认不生成 `kernel/kernel`、`user/_sh`、`fs.img`、`ph`、`barrier` 等旧路径产物。
- [x] 整理 grader 脚本目录。
  - 涉及模块：`graders/grade-lab-*`、`Makefile`、`gradelib.py`。
  - 完成内容：将 `grade-lab-*` 移入 `graders/`，保留 `make grade-*`、`make smoke`、`make grade-all` 作为稳定入口；每个 grader 可直接执行并找到根目录的 `gradelib.py`。
  - 验证方式：`make smoke`、`graders/grade-lab-util --help`、全体 `graders/grade-lab-* --help`。
- [x] 建立分层测试入口。
  - 涉及模块：`Makefile`、`quick.sh`。
  - 完成内容：新增 `make build`、`make image`、`make smoke`、`make regression`、`make grade-all-heavy`；`quick.sh` 保留为 `make smoke` 兼容包装。
  - 验证方式：`./quick.sh -n`、`make smoke`。
- [x] 清理生成物规则。
  - 涉及模块：`.gitignore`、`Makefile`。
  - 完成内容：忽略 `build/`、QEMU 输出、pcap 和旧路径残留生成物；删除已跟踪的 `packets.pcap`。
  - 验证方式：`git status --short` 不显示构建产物。

### 2.3 文档基线

- [x] 更新仓库入口文档。
  - 涉及模块：`README.md`、`AGENTS.md`。
  - 完成内容：说明当前 lab 集成状态、常用命令、`build/` 输出目录、`graders/` 目录和测试分层。
  - 验证方式：检查 README 中命令与 `Makefile` 目标一致。
- [x] 保留 lab 迁移记录。
  - 涉及模块：`docs/lab-migration-plan.md`。
  - 完成内容：记录各 lab 迁移文件、验证方式和关键注意事项。
  - 验证方式：后续迁移事实变更时同步更新该文档。

## 3. 进行中 / 部分完成内容

### 3.1 VM fault path 整理

- [ ] 重构 `usertrap()` 中的 page fault 分派。
  - 当前状态：`kernel/trap.c` 中 `scause=13` 直接调用 `mmap_fault()`；`scause=15` 同时处理 COW、未映射 mmap write fault 和非法访问，逻辑仍集中在 `usertrap()`。
  - 剩余工作：抽出 `handle_user_page_fault(scause, stval)`，并在其中明确 load fault、store fault、COW、mmap lazy fault 的分派顺序。
  - 涉及模块：`kernel/trap.c`、`kernel/vm.c`、`kernel/proc.c`、`kernel/defs.h`。
  - 验证方式：`make grade-cow`、`make grade-mmap`、`make grade-traps`、`make smoke`。
- [ ] 拆分 COW fault 处理逻辑。
  - 当前状态：COW 复制、单引用页提权、`PTE_COW` 清理和 `sfence_vma()` 仍直接写在 `usertrap()` 中。
  - 剩余工作：实现类似 `cow_fault(pagetable_t pagetable, uint64 va)` 的 helper，集中处理引用计数、PTE flags 和失败回滚。
  - 涉及模块：`kernel/trap.c`、`kernel/vm.c`、`kernel/kalloc.c`、`kernel/riscv.h`。
  - 验证方式：`make grade-cow`，并补充 COW 与 `copyout()` 交互的定向用例。
- [ ] 明确 mmap fault 模块边界。
  - 当前状态：`find_vma()`、`mmap_fault()`、`mmap_unmap()`、`mmap_cleanup()` 仍在 `kernel/proc.c`，VMA 结构在 `kernel/proc.h`。
  - 剩余工作：评估是否拆出 `kernel/mmap.c` 或 `kernel/vma.c`，让 `proc.c` 只保留进程生命周期调用点。
  - 涉及模块：`kernel/proc.c`、`kernel/proc.h`、`kernel/sysfile.c`、`kernel/vm.c`。
  - 验证方式：`make grade-mmap`、`make smoke`。

### 3.2 mmap 语义补强

- [ ] 完善 mmap 参数校验。
  - 当前状态：已检查 `length`、`fd`、文件类型、`MAP_SHARED` / `MAP_PRIVATE` 互斥、读写权限和 `mmap_top` 溢出；尚未看到对 `offset` 页对齐、非法 `prot` 位、非法 `flags` 位、非空 `addr` 策略的完整校验。
  - 剩余工作：明确并实现 `offset % PGSIZE == 0`、`prot` 仅允许 `PROT_READ | PROT_WRITE | PROT_EXEC`、`flags` 仅允许当前支持的 mmap flags、`addr` 为非 0 时拒绝或文档化忽略。
  - 涉及模块：`kernel/sysfile.c`、`kernel/fcntl.h`、`user/mmaptest.c`。
  - 验证方式：新增 mmap 参数错误测试，运行 `make grade-mmap`。
- [ ] 支持 VMA hole reuse。
  - 当前状态：`sys_mmap()` 使用 `proc->mmap_top` 顺序递增分配，`munmap()` 后不会复用低地址空洞。
  - 剩余工作：实现空洞查找策略，避免长期运行进程反复 mmap/munmap 后耗尽 `MMAPBASE` 到 `TRAPFRAME` 的区间。
  - 涉及模块：`kernel/proc.c`、`kernel/proc.h`、`kernel/sysfile.c`。
  - 验证方式：新增循环 mmap/munmap 用例，确认地址复用且不覆盖现有 VMA。
- [ ] 支持中间区间 `munmap()`。
  - 当前状态：`mmap_unmap()` 支持 whole / prefix / suffix，遇到 VMA 中间区间会返回错误。
  - 剩余工作：实现 VMA split，并正确处理 file refs、offset、length 和已 fault-in 页的写回。
  - 涉及模块：`kernel/proc.c`、`kernel/vm.c`。
  - 验证方式：新增 `munmap(p + PGSIZE, PGSIZE)` 类型用例，覆盖 `MAP_PRIVATE` 和 `MAP_SHARED`。
- [ ] 审计 mmap fork 语义。
  - 当前状态：需要检查并确认 `fork()` 中 VMA 元数据、file refs 和已 fault-in mmap 页的行为是否符合预期；原 TODO 已标注完整 Unix mmap fork 语义仍需补齐。
  - 剩余工作：明确 `MAP_PRIVATE` 已修改页在 fork 后是否形成快照，明确 `MAP_SHARED` 父子可见性和写回语义。
  - 涉及模块：`kernel/proc.c`、`kernel/vm.c`、`user/mmaptest.c`。
  - 验证方式：补充父子进程分别写同一映射的可见性测试，运行 `make grade-mmap`。
- [ ] 审计 `exec()` 与 mmap 清理的两阶段语义。
  - 当前状态：`kernel/exec.c` 在提交新地址空间前调用 `mmap_cleanup(p, 0)`；原 TODO 指出失败路径可能已经部分拆掉旧映射。
  - 剩余工作：拆成可失败的写回阶段和不可失败的拆映射阶段，避免 `exec()` 失败后旧地址空间处于部分清理状态。
  - 涉及模块：`kernel/exec.c`、`kernel/proc.c`。
  - 验证方式：构造 `MAP_SHARED` 脏页后执行失败 `exec()` 的测试，确认进程仍可继续使用旧映射。

### 3.3 测试分层继续完善

- [ ] 细化 `make regression` 的覆盖范围。
  - 当前状态：`make regression` 已存在，当前依赖 `smoke grade-mmap grade-cow grade-traps`。
  - 剩余工作：根据实际耗时决定是否加入 `grade-thread` 或定向 fs/lock 轻量项，同时避免默认触发 `bigfile` 和完整 `usertests`。
  - 涉及模块：`Makefile`、`graders/`、`README.md`。
  - 验证方式：记录 `make regression` 耗时，确认在日常开发可接受范围内。
- [ ] 批量执行 xv6 命令以减少 QEMU 重启。
  - 当前状态：多数 grader case 仍会通过 `Runner.run_qemu()` 启动独立 QEMU 实例；`make smoke` 中只有 `symlinktest`、`mmaptest` 用 Python one-liner 定向执行。
  - 剩余工作：为轻量测试组增加单次 QEMU 启动执行多条 xv6 命令的 helper 或 Makefile 入口。
  - 涉及模块：`gradelib.py`、`Makefile`、`graders/`。
  - 验证方式：比较改造前后 `make smoke` 的总耗时和输出稳定性。

## 4. 待完成内容

### 4.1 短期任务

- [ ] 补充 mmap 边界测试。
  - 要做什么：为参数校验、跨页 `copyin` / `copyout`、`copyinstr`、权限错误、非页对齐 offset、非法 flags 增加用户态测试。
  - 涉及模块：`user/mmaptest.c`、`kernel/sysfile.c`、`kernel/vm.c`。
  - 为什么要做：当前已有 `read(fd, fresh_mmap_addr, n)` 覆盖，但跨页、字符串和错误权限路径仍容易回归。
  - 如何验证：`make grade-mmap`，并确认新增 case 在失败时输出明确原因。
- [ ] 补充 `copyin` / `copyout` lazy fault-in 审计说明。
  - 要做什么：记录 `kernel/vm.c` 中用户地址访问触发 mmap lazy fault-in 的入口和限制。
  - 涉及模块：`kernel/vm.c`、`docs/kernel-lifetime-notes.md` 或新增 VM 文档。
  - 为什么要做：mmap 页可能在 syscall buffer 中首次触发 fault，调用路径不如普通 usertrap 直观。
  - 如何验证：文档列出的入口能在代码中逐一定位，并有对应测试或待测项。
- [ ] 加强 buffer cache pin/unpin 防御检查。
  - 要做什么：在 `bunpin()` 和 `brelse()` 中考虑增加 `refcnt < 1` 的 panic 检查。
  - 涉及模块：`kernel/bio.c`、`kernel/buf.h`、`kernel/log.c`。
  - 为什么要做：提前暴露日志层 pin/unpin 不匹配或重复释放问题。
  - 如何验证：`make grade-fs`、`make grade-lock`、`make grade-mmap`。
- [ ] 增加日志头边界检查。
  - 要做什么：在 `read_head()` 读取磁盘日志头后检查 `lh->n < 0 || lh->n > LOGSIZE || lh->n > log.size - 1`。
  - 涉及模块：`kernel/log.c`、`kernel/param.h`。
  - 为什么要做：避免损坏日志头导致恢复路径越界访问。
  - 如何验证：正常路径运行 `make grade-fs`；损坏镜像测试需要单独设计，可先通过代码审查确认边界。
- [ ] 建立 `docs/kernel-lifetime-notes.md`。
  - 要做什么：记录 `struct file` refcount、inode lock/ref、COW page refcount、VMA file refs、bcache buf refcnt/pin 的所有权关系。
  - 涉及模块：`kernel/file.c`、`kernel/fs.c`、`kernel/proc.c`、`kernel/vm.c`、`kernel/bio.c`、`kernel/log.c`。
  - 为什么要做：当前多个 lab 的生命周期规则交叉，后续重构前需要明确不变量。
  - 如何验证：文档中的每条所有权转移能对应到具体函数和释放路径。

### 4.2 中期任务

- [ ] 重构 VMA 管理模块。
  - 要做什么：将 VMA 查找、分配、拆分、写回、清理逻辑从 `kernel/proc.c` 中拆出，形成更清晰的模块边界。
  - 涉及模块：`kernel/proc.c`、`kernel/proc.h`、可能新增 `kernel/mmap.c` 或 `kernel/vma.c`、`Makefile`。
  - 为什么要做：降低 `proc.c` 的职责密度，方便单独审计 mmap 生命周期。
  - 如何验证：`make grade-mmap`、`make grade-cow`、`make smoke`。
- [ ] 拆分 `usertests` 覆盖层级。
  - 要做什么：将 `user/usertests.c` 中耗时不同的测试按 proc、vm、fs、syscall、lock 分组，或者提供更明确的 Makefile/脚本过滤入口。
  - 涉及模块：`user/usertests.c`、`graders/`、`Makefile`、`README.md`。
  - 为什么要做：`FSSIZE=200000` 和 `MAXFILE` 放大后，完整 `usertests` 不适合作为默认日常检查。
  - 如何验证：新增入口能分别运行轻量组和完整组，输出稳定且 README 说明清楚。
- [ ] 优化文件系统大文件测试成本。
  - 要做什么：区分基础 FS 正确性和 double-indirect 最大文件测试；避免所有默认回归都隐式继承 `bigfile` 级别写盘成本。
  - 涉及模块：`user/bigfile.c`、`user/usertests.c`、`graders/grade-lab-fs`、`README.md`。
  - 为什么要做：保留 `bigfile` 的课程覆盖，同时降低日常回归耗时。
  - 如何验证：`make smoke` 不跑重型项，`make grade-fs` / `make grade-all-heavy` 仍覆盖 `bigfile`。
- [ ] 增加日志系统可观测性。
  - 要做什么：为 `kernel/log.c` 增加 commit 次数、logged blocks、absorbed blocks、等待原因、install blocks 等统计，并通过现有 `sysinfo` 或新接口暴露。
  - 涉及模块：`kernel/log.c`、`kernel/sysproc.c`、`kernel/sysinfo.h`、`user/sysinfotest.c` 或新增用户态工具。
  - 为什么要做：为后续 WAL 提交路径优化提供可量化指标。
  - 如何验证：新增用户态测试读取统计并确认事务路径会更新计数。

### 4.3 长期优化

- [ ] 优化 buffer cache 淘汰策略。
  - 要做什么：评估并实现 old / young 双分区链表，替代当前 `timestamp` + 全桶扫描的近似 LRU。
  - 涉及模块：`kernel/bio.c`、`kernel/buf.h`、`kernel/stats.c`、`user/bcachetest.c`。
  - 为什么要做：降低 miss 时全桶扫描成本，并减轻一次性顺序扫描对热块的污染。
  - 如何验证：`make grade-lock`、`make grade-fs`、`make grade-mmap`，并比较 `bcachetest` 竞争统计。
- [ ] 优化 WAL 正常提交路径。
  - 要做什么：将 `install_trans()` 拆为 recovery 路径和 normal commit 路径；正常提交直接写回 pinned home buffer 并 `bunpin()`，恢复路径继续从日志区 replay。
  - 涉及模块：`kernel/log.c`、`kernel/bio.c`。
  - 为什么要做：当前正常提交在 `write_head()` 后仍从日志区读回并拷贝到 home block，存在可避免的 buffer cache 查找和 1024B 拷贝。
  - 如何验证：`make grade-fs`、`make grade-mmap`、`make grade-lock`，并通过日志统计确认 install blocks 或反向读次数下降。
- [ ] 评估内存日志缓存 pinned buffer 指针。
  - 要做什么：在 `struct log` 中加入纯内存 `struct buf *bufs[LOGSIZE]`，减少 `write_log()` 和 normal install 中按块号重新 `bread()` 的查找。
  - 涉及模块：`kernel/log.c`、`kernel/bio.c`。
  - 为什么要做：在 WAL 正常提交路径优化稳定后进一步降低 cache 查找开销。
  - 如何验证：先完成日志统计，再对比优化前后 commit 路径的统计变化。
- [ ] 建立 CI 或本地自动化回归约定。
  - 要做什么：将 `git diff --check`、`make build image`、`make smoke` 和若干定向 grader 组织成可复用脚本或 CI job。
  - 涉及模块：`Makefile`、可能新增 `.github/workflows/` 或本地脚本、`README.md`。
  - 为什么要做：降低阶段性集成时漏跑关键验证的概率。
  - 如何验证：在干净 checkout 上执行自动化入口并记录耗时。
- [ ] 评估是否迁移 grader 公共逻辑。
  - 要做什么：在测试分层稳定后，再决定是否将 Python grader 公共逻辑重构或迁移为其他实现。
  - 涉及模块：`gradelib.py`、`graders/`。
  - 为什么要做：当前优先级应放在内核语义和测试覆盖；过早迁移 grader 语言会增加无关风险。
  - 如何验证：只有在现有 Python grader 的维护成本成为瓶颈时再启动。

## 5. 测试与验证计划

### 5.1 提交前基础检查

- [ ] 运行 `git diff --check`。
- [ ] 运行 `make build image`。
- [ ] 如果修改 `.c` 或 `.h` 文件，运行 `clang-format -i` 处理相关文件。
- [ ] 如果修改头文件，运行 `clangd --check=<file>`，确认头文件自包含和类型依赖正确。

### 5.2 日常轻量回归

- [ ] 运行 `make smoke`。
  - 覆盖内容：`util`、`syscall`、`net`、`pgtbl`、`traps`、`symlinktest`、`mmaptest`。
  - 适用场景：文档、Makefile、用户态工具、小范围内核改动。
- [ ] 运行与改动模块对应的定向 grader。
  - VM / COW：`make grade-cow`。
  - mmap：`make grade-mmap`。
  - traps / alarm：`make grade-traps`。
  - FS：`make grade-fs`。
  - lock / bcache：`make grade-lock`。
  - thread host 工具：`make grade-thread`。

### 5.3 中等与重型回归

- [ ] 运行 `make regression`。
  - 适用场景：涉及 VM、COW、mmap 或 trap 分派的改动。
- [ ] 运行 `make grade-all-heavy` 或 `make grade-all`。
  - 适用场景：阶段性合并、重构完成、缓存或日志路径变更。
  - 注意事项：包含 `bigfile`、完整 fs/lock 压力项和多次 QEMU 启动，耗时明显高于 `make smoke`。

## 6. 可能的后续方向

- 保持 `dev/all` 作为集成分支，避免在课程参考分支上直接开发。
- 保持文档和 Makefile 入口同步：新增测试入口或移动脚本目录后，同步更新 `README.md`、`AGENTS.md`、`docs/lab-migration-plan.md` 和本文件。
- 优先处理能降低后续重构风险的任务：VM fault path 拆分、mmap 生命周期审计、kernel lifetime 文档、测试分层。
- 暂不追求完整 POSIX 兼容、生产级内核能力或磁盘格式大改；后续目标应保持在 xv6 教学 OS 和已整合 lab 的合理范围内。

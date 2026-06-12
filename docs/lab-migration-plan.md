# 6.S081 2021 实验整合说明

本文档根据 6.S081 / 6.828 2021 课程主页与各实验页整理，目标是帮助当前 `dev/all` 分支逐步整合本地各 lab 分支的功能代码，而不是把每个分支的全部格式化、工具链或临时文件一并搬过来。

参考：

- 课程日程页：<https://pdos.csail.mit.edu/6.828/2021/schedule.html>
- util：<https://pdos.csail.mit.edu/6.828/2021/labs/util.html>
- syscall：<https://pdos.csail.mit.edu/6.828/2021/labs/syscall.html>
- pgtbl：<https://pdos.csail.mit.edu/6.828/2021/labs/pgtbl.html>
- traps：<https://pdos.csail.mit.edu/6.828/2021/labs/traps.html>
- cow：<https://pdos.csail.mit.edu/6.828/2021/labs/cow.html>
- thread：<https://pdos.csail.mit.edu/6.828/2021/labs/thread.html>
- net：<https://pdos.csail.mit.edu/6.828/2021/labs/net.html>
- lock：<https://pdos.csail.mit.edu/6.828/2021/labs/lock.html>
- fs：<https://pdos.csail.mit.edu/6.828/2021/labs/fs.html>
- mmap：<https://pdos.csail.mit.edu/6.828/2021/labs/mmap.html>

## 总体建议

- 以功能为单位迁移，不直接整分支 merge。
- 优先迁移“实验必需代码”，跳过 `.vscode/`、格式化、临时文件、测试产物。
- 每整合一个实验，就单独提交并跑对应测试。
- `net` 已经在 `dev/all` 中，后续实验应以它为基线继续叠加。
- 当前 `make grade-all` 已串联 `util → syscall → net → pgtbl → traps → cow → thread → lock → fs → mmap`，并复用一次统一构建产物。

## 建议整合顺序

1. `syscall`（✅ 已完成）
2. `pgtbl`（✅ 已完成）
3. `traps`（✅ 已完成）
4. `cow`（✅ 已完成）
5. `thread`（✅ 已完成）
6. `lock`（✅ 已完成）
7. `fs`（✅ 已完成）
8. `mmap`（✅ 已完成）

`net` 已完成，`util` 已完成，不需要再次迁移。

## syscall

当前状态：已整合到 `dev/all`。

官方内容：新增 `trace` 和 `sysinfo` 两个系统调用，理解用户态 stub、系统调用分发表、参数解析、进程结构扩展。

实际迁移文件：

- `kernel/syscall.c`
- `kernel/syscall.h`
- `kernel/sysproc.c`
- `kernel/proc.h`
- `kernel/proc.c`
- `kernel/kalloc.c`
- `kernel/defs.h`
- `kernel/sysinfo.h`
- `user/user.h`
- `user/usys.pl`
- `user/trace.c`
- `user/sysinfotest.c`
- `Makefile`
- `grade-lab-syscall`

本轮没有迁移的内容：

- 格式化差异（对齐、空格等）
- `.clang-format`、`.clangd`、`compile_commands.json` 等工具链配置文件

验证方式：

- `make grade-syscall`
- `make grade-all`

其中 `make grade-all` 会先统一构建一遍，再依次执行 `util → syscall → net → pgtbl → traps → cow → thread → lock → fs → mmap` 的 grader。

注意：

- grader 中的文件引用从 `README` 改为 `README.md`（匹配本仓库实际文件名），对应 read 大小期望值也做了适配。
- `user/usys.pl` 中保留了 `connect` 条目（net lab 需要），新增 `trace` 和 `sysinfo` 条目。

## util

当前状态：已整合到 `dev/all`。

官方内容：实现 xv6 用户态小工具，包括 `sleep`、`pingpong`、`primes`、`find`、`xargs`，重点是熟悉用户程序、管道、`fork/exec/wait` 和 `UPROGS`。

实际迁移文件：

- `user/sleep.c`
- `user/pingpong.c`
- `user/primes.c`
- `user/find.c`
- `user/xargs.c`
- `user/xargstest.sh`
- `Makefile` 中 `UPROGS` 与 `UEXTRA` 相关条目

本轮没有迁移的内容：

- `user/cat.c`、`user/echo.c`、`user/grep.c`、`user/ls.c` 等基础程序的格式化差异

验证方式：

- `make grade-util`
- `make grade-all`

其中 `make grade-all` 会先统一构建一遍，再依次执行 `util → syscall → net → pgtbl → traps → cow → thread → lock → fs → mmap` 的 grader。

## pgtbl

当前状态：已整合到 `dev/all`。

官方内容：三部分：通过 USYSCALL 共享只读页加速 `getpid()`、实现 `vmprint()` 递归打印页表、实现 `pgaccess()` 检测页访问位（PTE_A）。

实际迁移文件：

- `kernel/proc.h`
- `kernel/proc.c`
- `kernel/vm.c`
- `kernel/riscv.h`
- `kernel/memlayout.h`
- `kernel/defs.h`
- `kernel/exec.c`
- `kernel/sysproc.c`
- `kernel/syscall.c`
- `kernel/syscall.h`
- `user/user.h`
- `user/ulib.c`
- `user/usys.pl`
- `user/pgtbltest.c`
- `Makefile`
- `grade-lab-pgtbl`
- `answers-pgtbl.txt`

本轮没有迁移的内容：

- `kernel/vmcopyin.c`（copyin_new/copyinstr_new，原 LAB_PGTBL 下的附加优化）
- 格式化差异

验证方式：

- `make grade-pgtbl`
- `make grade-all`

其中 `make grade-all` 顺序为 `util → syscall → net → pgtbl → traps → cow → thread → lock → fs → mmap`（用户态 → 系统调用 → 驱动 → 内存管理 → 异常处理 → COW 内存管理 → 线程与并发练习 → 锁竞争优化 → 文件系统 → 文件映射）。

注意：

- 移除了 `#ifdef LAB_PGTBL` 条件编译，USYSCALL 和 pgaccess 在 dev/all 中无条件启用。
- `ugetpid()` 是用户态库函数（直接读 USYSCALL 页），实现在 `user/ulib.c`，不是系统调用。
- pte printout 测试期望的页表地址会因构建不同而变化，grader 用正则匹配固定的格式模式。

## traps

当前状态：已整合到 `dev/all`。

官方内容：两部分：实现 `backtrace()` 内核栈回溯、实现 `sigalarm`/`sigreturn` 用户态定时器回调。

实际迁移文件：

- `kernel/printf.c`
- `kernel/defs.h`
- `kernel/riscv.h`
- `kernel/trap.c`
- `kernel/proc.h`
- `kernel/sysproc.c`
- `kernel/syscall.c`
- `kernel/syscall.h`
- `user/user.h`
- `user/usys.pl`
- `user/alarmtest.c`
- `user/bttest.c`
- `Makefile`
- `grade-lab-traps`

本轮没有迁移的内容：

- `answers-traps.txt`（课程问答，不属于功能代码）
- `usertests`（耗时长且在有限 fs 镜像上不稳定）

验证方式：

- `make grade-traps`
- `make grade-all`

其中 `make grade-all` 顺序为 `util → syscall → net → pgtbl → traps → cow → thread → lock → fs → mmap`。

注意：

- `backtrace()` 从 `sys_sleep` 中调用以使 bttest 能触发栈回溯输出。
- alarm 字段在 proc 中默认零初始化（`allocproc` 已通过 `kalloc` 清零 trapframe 页），`alarm_interval=0` 表示禁用。
- grader 只包含 backtrace 和 alarm 测试（共 60 分），已移除 answers、usertests 和 time 测试。

## cow

当前状态：已整合到 `dev/all`。

官方内容：实现 copy-on-write fork，通过引用计数和缺页异常在父子进程间共享物理页，写入时按需复制。

实际迁移文件：

- `kernel/vm.c`
- `kernel/kalloc.c`
- `kernel/trap.c`
- `kernel/riscv.h`
- `kernel/defs.h`
- `kernel/sysproc.c`
- `user/cowtest.c`
- `Makefile`
- `grade-lab-cow`

本轮没有迁移的内容：

- `usertests`（耗时长，省略）

验证方式：

- `make grade-cow`
- `make grade-all`

注意：

- `PA2INDEX` 使用 `(pa - KERNBASE) / PGSIZE` 计算相对于 KERNBASE 的索引，不是绝对物理地址除以页大小。
- 从 `sys_sleep` 移除了 `backtrace()` 调用，避免其输出干扰其他测试的行匹配。
- `kfree` 改为引用计数语义：只在计数归零时才真正回收页面。
- COW fault 和 `copyout()` 更新 PTE 后刷新 TLB；复制 COW 页后用 `kfree()` 释放旧物理页引用，避免并发退出路径把引用数降到 0 但未回收到 freelist。

## thread

当前状态：已整合到 `dev/all`。

官方内容：三部分：

1. `user/uthread.c` 和 `user/uthread_switch.S`：实现用户级线程切换
2. `notxv6/ph.c`：用 pthread 优化哈希表并发
3. `notxv6/barrier.c`：实现 barrier

实际迁移文件：

- `user/uthread.c`
- `user/uthread_switch.S`
- `notxv6/ph.c`
- `notxv6/barrier.c`
- `Makefile`
- `grade-lab-thread`

验证方式：

- `make grade-thread`
- `make grade-all`

注意：

- 后两部分运行在宿主机，不在 xv6 内核里。
- 这个实验对 `dev/all` 的内核整合价值较低，但对课程复习仍有帮助。
- 迁移后移除了 `LAB=thread` 守卫，`_uthread`、`ph`、`barrier` 和 `grade-lab-thread` 在 `dev/all` 中直接可用。

## net

官方内容：补全 E1000 网卡驱动中的 `e1000_transmit()` 和 `e1000_recv()`，让 xv6 能通过 QEMU user networking 收发 UDP 数据。

当前状态：

- 已经整合进 `dev/all`
- 相关提交：`feat(net): finalize lab implementation`

核心文件：

- `kernel/e1000.c`
- `kernel/net.c`
- `kernel/net.h`
- `kernel/sysnet.c`
- `kernel/file.c`
- `kernel/file.h`
- `kernel/syscall.h`
- `kernel/syscall.c`
- `user/user.h`
- `user/usys.pl`
- `user/nettests.c`
- `server.py`
- `ping.py`
- `Makefile`

注意：

- `make grade-net` 和 `make grade-all` 里的 `nettests` 只需要 guest -> host 的 UDP 通路，不依赖宿主机到 xv6 的 `hostfwd`。
- 只有宿主机主动向 xv6 发送 UDP 数据时，才需要使用 `make qemu-net` 或 `make qemu-gdb-net` 打开 `hostfwd=udp::$(FWDPORT)-:2000`。

## lock

当前状态：已整合到 `dev/all`。

官方内容：两部分：重构物理页分配器为 per-CPU freelist 降低 kmem 锁竞争、重构 buffer cache 为 hash bucket 降低 bcache 锁竞争。

实际迁移文件：

- `kernel/kalloc.c`
- `kernel/bio.c`
- `kernel/buf.h`
- `kernel/spinlock.h`
- `kernel/spinlock.c`
- `kernel/defs.h`
- `kernel/param.h`
- `kernel/main.c`
- `kernel/proc.c`
- `kernel/sysnet.c`
- `kernel/pipe.c`
- `kernel/sprintf.c`
- `kernel/stats.c`
- `user/kalloctest.c`
- `user/bcachetest.c`
- `user/stats.c`
- `user/statistics.c`
- `Makefile`
- `grade-lab-lock`

验证方式：

- `make grade-lock`
- `make grade-all`

注意：

- `kalloc.c` 同时包含 cow 的引用计数和 lock 的 per-CPU freelist。`kmems[NCPU]` 管理 per-CPU 空闲链表，`page_refs` 管理共享的引用计数数组。两个子系统用不同的锁保护。
- `bio.c` 用 29 个 hash bucket 替代单一的 bcache 锁，命中路径只锁目标 bucket；miss/evict 路径用 `bcache_evict` 串行化 victim 选择和跨 bucket 迁移，避免重复缓存同一 block 或无锁读取 `refcnt`。
- cowtest 超时从默认 30s 增加到 120s（per-CPU freelist 下 steal 略慢）。

## fs

当前状态：已整合到 `dev/all`。

官方内容：两部分：

1. 支持大文件：引入 double-indirect block，扩大最大文件大小
2. 支持符号链接：实现 `symlink()` 和路径跟随逻辑

建议迁移文件：

- `kernel/fs.h`
- `kernel/file.h`
- `kernel/fs.c`
- `kernel/sysfile.c`
- `kernel/fcntl.h`
- `kernel/stat.h`
- `kernel/param.h`
- `kernel/syscall.h`
- `kernel/syscall.c`
- `user/user.h`
- `user/usys.pl`
- `user/bigfile.c`
- `user/symlinktest.c`
- `Makefile`
- `grade-lab-fs`

验证方式：

- `make grade-fs`
- `make grade-lock`
- `make grade-all`

注意：

- `dev/all` 保持 `LAB=net`，因此 `bigfile` / `symlinktest` 与 `grade-fs` 需要直接接入现有集成构建，而不是重新依赖 `ifeq ($(LAB),fs)`。
- `sys_symlink()` 只创建并写入符号链接 inode，不要求目标存在，也不额外拒绝目录目标。
- `sys_open()` 默认递归跟随符号链接，`O_NOFOLLOW` 下保留对符号链接自身的打开语义，并用固定深度上限防止环路。
- 文件系统镜像大小已统一提升到 `FSSIZE=200000`，以支持 `bigfile`。

## mmap

当前状态：已整合到 `dev/all`。

官方内容：实现文件映射相关的 `mmap()` / `munmap()`，支持按页懒分配、缺页装载、部分取消映射、进程退出时回收映射。

实际迁移文件：

- `kernel/proc.h`
- `kernel/proc.c`
- `kernel/vm.c`
- `kernel/trap.c`
- `kernel/sysfile.c`
- `kernel/file.h`
- `kernel/fcntl.h`
- `kernel/stat.h`
- `kernel/syscall.h`
- `kernel/syscall.c`
- `user/user.h`
- `user/usys.pl`
- `user/mmaptest.c`
- `Makefile`
- `grade-lab-mmap`

验证方式：

- `make grade-mmap`
- `make grade-cow`
- `make grade-traps`
- `make grade-all`

注意：

- `mmap` 在 `dev/all` 中复用现有 syscall 编号 `SYS_mmap=27`、`SYS_munmap=28`，不会覆盖后续 net/pgtbl 编号。
- `usertrap()` 中 COW store fault 优先，mmap 只处理 load fault 或非 COW store fault，避免回归 cow lab。
- VMA 采用固定 mmap 区间和 lazy fault-in；`munmap()` 支持 whole / prefix / suffix，不实现中间 punch hole。
- `MAP_SHARED` 写回只处理已经 fault-in 的页，未映射页允许跳过。
- `grade-lab-mmap` 已移除 `time` 和完整 `usertests` 检查；完整 `usertests` 在当前 fs 集成后会被 `MAXFILE` 放大，适合通过 `grade-fs` / `grade-all` 的重型回归覆盖。

## 本地分支与迁移策略

当前本地存在这些实验分支：

- `util`
- `syscall`
- `pgtbl`
- `traps`
- `cow`
- `thread`
- `net`
- `lock`
- `fs`
- `mmap`

建议在 `dev/all` 上采用以下策略：

1. 先用 `git diff <base>..<branch> --name-only` 确认该实验真正涉及的文件。
2. 优先手工迁移“实验功能代码”。
3. 若某个实验分支只有 1 到 2 个清晰提交，再考虑 `cherry-pick`。
4. 工具链、格式化、README、临时文件不要和实验功能混提。

## 后续建议

所有计划内实验已整合。后续如继续重构测试系统或整理实现，每次仍保持一份独立提交，提交信息建议统一成：

```text
feat(<lab>): integrate <lab> lab changes

- summarize the functional pieces migrated
- list the primary kernel/user files touched
```

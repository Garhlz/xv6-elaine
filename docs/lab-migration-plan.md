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

## 建议整合顺序

1. `syscall`（✅ 已完成）
2. `pgtbl`
3. `traps`
4. `cow`
5. `thread`
6. `lock`
7. `fs`
8. `mmap`

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

其中 `make grade-all` 会先统一构建一遍，再依次执行 `util`、`net` 与 `syscall` 的 grader。

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

其中 `make grade-all` 会先统一构建一遍，再依次执行 `util` 与 `net` 的 grader。

## pgtbl

官方内容：三部分，分别是：

1. 在 `USYSCALL` 共享只读页中暴露 PID，加速 `getpid()`
2. 实现 `vmprint()` 打印页表
3. 实现 `pgaccess()` 检测页访问位

建议迁移文件：

- `kernel/proc.h`
- `kernel/proc.c`
- `kernel/vm.c`
- `kernel/riscv.h`
- `kernel/memlayout.h`
- `kernel/defs.h`
- `kernel/exec.c`
- `kernel/sysproc.c`
- `kernel/syscall.h`
- `kernel/syscall.c`
- `user/user.h`
- `user/usys.pl`
- `user/pgtbltest.c`
- `Makefile`

## traps

官方内容：两部分：

1. 在 `kernel/printf.c` 中实现 `backtrace()`
2. 实现 `sigalarm` / `sigreturn`，让用户态按时钟周期触发 handler

建议迁移文件：

- `kernel/printf.c`
- `kernel/defs.h`
- `kernel/riscv.h`
- `kernel/trap.c`
- `kernel/proc.h`
- `kernel/proc.c`
- `kernel/sysproc.c`
- `kernel/syscall.h`
- `kernel/syscall.c`
- `user/user.h`
- `user/usys.pl`
- `user/alarmtest.c`
- `user/bttest.c`
- `Makefile`

## cow

官方内容：实现 copy-on-write `fork()`，通过共享只读页和缺页异常按需复制，解决大地址空间 `fork` 的内存开销问题。

建议迁移文件：

- `kernel/vm.c`
- `kernel/kalloc.c`
- `kernel/trap.c`
- `kernel/proc.h`
- `kernel/defs.h`
- `kernel/riscv.h`
- `user/cowtest.c`
- `Makefile`

注意：

- 这是内存管理实验，和 `pgtbl` / `traps` 强相关，必须在它们之后整合。

## thread

官方内容：三部分：

1. `user/uthread.c` 和 `user/uthread_switch.S`：实现用户级线程切换
2. `notxv6/ph.c`：用 pthread 优化哈希表并发
3. `notxv6/barrier.c`：实现 barrier

建议迁移文件：

- `user/uthread.c`
- `user/uthread_switch.S`
- `notxv6/ph.c`
- `notxv6/barrier.c`
- `Makefile`

注意：

- 后两部分运行在宿主机，不在 xv6 内核里。
- 这个实验对 `dev/all` 的内核整合价值较低，但对课程复习仍有帮助。

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

## lock

官方内容：两部分：

1. 重构物理页分配器，降低 `kmem` 锁竞争
2. 重构 buffer cache，降低 `bcache` 锁竞争

建议迁移文件：

- `kernel/kalloc.c`
- `kernel/bio.c`
- `kernel/buf.h`
- `kernel/spinlock.h`
- `kernel/spinlock.c`
- `user/kalloctest.c`
- `user/bcachetest.c`
- `Makefile`

本地分支额外文件：

- `kernel/stats.c`
- `kernel/sprintf.c`
- `user/stats.c`
- `user/statistics.c`

这些更像你自己的扩展或调试辅助，建议在第一轮整合时先不迁移。

## fs

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

注意：

- 这是后期改动面最大的实验之一，建议放在 `lock` 之后整合。

## mmap

官方内容：实现文件映射相关的 `mmap()` / `munmap()`，支持按页懒分配、缺页装载、部分取消映射、进程退出时回收映射。

建议迁移文件：

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

注意：

- `mmap` 会再次修改进程元数据、缺页异常和文件回写路径，应当在 `fs` 之后整合。

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

## 下一步建议

从 `syscall` 开始逐个整合，每个实验保持一份独立提交，提交信息建议统一成：

```text
feat(<lab>): integrate <lab> lab changes

- summarize the functional pieces migrated
- list the primary kernel/user files touched
```

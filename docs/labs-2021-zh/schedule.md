# 2021 年 6.S081 课程日程

本页根据[官方课程日程](https://pdos.csail.mit.edu/6.828/2021/schedule.html)整理翻译。课程网页说明：未来日期的讲义、视频等链接当时沿用了 2020 年 6.S081 的资料，课程进行时会逐步更新。讲义有助于回忆课堂内容，但不能替代听课。每讲的幻灯片、旧版笔记、板书和视频链接请参阅官方日程。

## 9 月

- **9 月 6 日**：劳动节。**9 月 7 日**：注册日。
- **9 月 8 日，第 1 讲（ab）：** 课程介绍和示例。讲义包括 xv6 教材；预习教材第 1 章，另有 Unix 视频供欣赏。作业 1 截止；布置 [util：Unix 实用工具](util.md)。
- **9 月 13 日，第 2 讲（助教）：** C 和 GDB。预习 Kernighan 与 Ritchie（K&R）的 2.9 节“位运算符”、5.1 节“指针与地址”至 5.6 节“指针数组”，以及 6.4 节“指向结构体的指针”。
- **9 月 15 日，第 3 讲（ab）：** 操作系统组织与系统调用。预习 xv6 教材第 2 章和代码 `kernel/proc.h`、`kernel/defs.h`、`kernel/entry.S`、`kernel/main.c`、`user/initcode.S`、`user/init.c`，并浏览 `kernel/proc.c`、`kernel/exec.c`。作业 2 截止；布置 [syscall：系统调用](syscall.md)。**9 月 16 日：** util 截止。
- **9 月 20 日，第 4 讲（ab）：** 页表。预习教材第 3 章及 `kernel/memlayout.h`、`kernel/vm.c`、`kernel/kalloc.c`、`kernel/riscv.h`、`kernel/exec.c`。作业 3 截止。
- **9 月 22 日，第 5 讲（助教）：** GDB、调用约定和 RISC-V 栈帧。预习调用约定材料；布置 [pgtbl：页表](pgtbl.md)。**9 月 23 日：** syscall 截止。
- **9 月 27 日，第 6 讲（ab）：** 隔离以及系统调用的进入／退出。预习教材第 4 章（4.6 节除外）及 `kernel/riscv.h`、`kernel/trampoline.S`、`kernel/trap.c`。作业 4 截止。
- **9 月 29 日，第 7 讲（ab）：** 缺页异常。预习教材第 4.6 节。作业 5 截止；布置 [traps：陷阱](traps.md)。**9 月 30 日：** pgtbl 截止。

## 10 月

- **10 月 4 日，第 8 讲（ab）：** 实验答疑。作业 6 截止。
- **10 月 6 日，第 9 讲（ab）：** 中断。预习教材第 5 章及 `kernel/kernelvec.S`、`kernel/plic.c`、`kernel/console.c`、`kernel/uart.c`、`kernel/printf.c`。作业 7 截止；布置 [cow：写时复制 fork](cow.md)。**10 月 7 日：** traps 截止。**10 月 8 日：** 加课截止日。
- **10 月 11 日：** 原住民日。
- **10 月 13 日，第 10 讲（ab）：** 多处理器与锁。预习教材“锁”一章及 `kernel/spinlock.h`、`kernel/spinlock.c`。作业 8 截止。
- **10 月 18 日，第 11 讲（ab）：** 调度 1。预习“调度”一章至第 7.4 节及 `kernel/proc.c`、`kernel/swtch.S`。作业 9 截止。
- **10 月 20 日，第 12 讲（ab）：** 调度 2。预习“调度”一章的其余内容以及 `kernel/proc.c`、`kernel/sleeplock.c` 中相应部分。作业 10 截止；布置 [thread：多线程](thread.md)。**10 月 21 日：** cow 截止。
- **10 月 25 日，第 13 讲（ab）：** 实验答疑。作业 11 截止。
- **10 月 27 日，第 14 讲（ab）：** 文件系统。预习 `kernel/bio.c`、`kernel/fs.c`、`kernel/sysfile.c`、`kernel/file.c` 及教材“文件系统”一章（日志部分除外）。作业 12 截止；布置 [net：网络驱动](net.md)。**10 月 28 日：** thread 截止。

## 11 月

- **11 月 1 日，第 15 讲（ab）：** 崩溃恢复。预习 `kernel/log.c` 和教材“文件系统”一章的日志部分。作业 13 截止。
- **11 月 3 日，第 16 讲（ab）：** 文件系统性能与快速崩溃恢复。预习《Journaling the Linux ext2fs Filesystem》（1998）。作业 14 截止。
- **11 月 8 日，第 17 讲（ab）：** 应用程序的虚拟内存。预习《Virtual Memory Primitives for User Programs》（1991）。作业 15 截止。**11 月 9 日：** net 截止。
- **11 月 10 日，第 18 讲（ab）：** 操作系统组织。预习《The Performance of micro-Kernel-Based Systems》（1997）。作业 16 截止；布置 [lock：并行性与锁](lock.md)。**11 月 11 日：** 退伍军人节。
- **11 月 15 日，第 19 讲（ab）：** 虚拟机。预习《Dune: Safe User-level Access to Privileged CPU Features》（2012）。作业 17 截止。
- **11 月 17 日，第 20 讲（ab）：** 内核与高级语言。预习 Biscuit 论文（2018）及常见问题。作业 18 截止；布置 [fs：文件系统](fs.md)。**11 月 18 日：** 退课截止日，lock 截止。
- **11 月 22 日，第 21 讲（ab）：** 网络。预习《Receive Livelock》（1996）。作业 19 截止。
- **11 月 24 日：** 实验工作日，不举行课堂；fs 截止；布置 [mmap：内存映射](mmap.md)。**11 月 25—26 日：** 感恩节假期。
- **11 月 29 日，第 22 讲（ab）：** Meltdown。预习《Meltdown》（2018）。作业 20 截止。

## 12 月

- **12 月 1 日，第 23 讲（ab）：** 多核可扩展性与 RCU。预习 RCU 论文（2013）及常见问题。作业 21 截止。
- **12 月 6 日，第 24 讲（助教）：** 当前研究：抗辐射能力。预习 NASA CALIPSO 论文（2006）作为背景。作业 22 截止。
- **12 月 7 日：** mmap 截止。
- **12 月 8 日，第 25 讲（ab）：** 答疑。作业 23 截止。
- **12 月 9 日：** 最后一个上课日。

有关 6.S081 的问题或意见，可发送邮件至 [6S081@lists.csail.mit.edu](mailto:6s081@lists.csail.mit.edu)。官方页面最后更新于 2021 年 12 月 6 日。

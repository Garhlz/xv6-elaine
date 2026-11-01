# 2021 年 xv6 实验指导中文译文

本目录根据 [MIT 6.S081 2021 官方实验指导](https://pdos.csail.mit.edu/6.828/2021/schedule.html)翻译。英文原文快照位于 [labs-2021](../labs-2021/README.md)；实验难度标注根据官网原始 HTML 补回。

标为“译注”的内容用于说明原文笔误、操作歧义或必要的术语背景，不构成额外实验要求。提交说明中的 `answers-实验名.txt` 是文件名模板，需将“实验名”替换为实验标识，例如 `answers-traps.txt`。

| 顺序 | 实验 | 中文译文 | 英文快照 |
| --- | --- | --- | --- |
| 1 | Unix 实用工具 | [util](util.md) | [util](../labs-2021/util.md) |
| 2 | 系统调用 | [syscall](syscall.md) | [syscall](../labs-2021/syscall.md) |
| 3 | 页表 | [pgtbl](pgtbl.md) | [pgtbl](../labs-2021/pgtbl.md) |
| 4 | 陷阱 | [traps](traps.md) | [traps](../labs-2021/traps.md) |
| 5 | 写时复制 fork | [cow](cow.md) | [cow](../labs-2021/cow.md) |
| 6 | 多线程 | [thread](thread.md) | [thread](../labs-2021/thread.md) |
| 7 | 网络 | [net](net.md) | [net](../labs-2021/net.md) |
| 8 | 锁 | [lock](lock.md) | [lock](../labs-2021/lock.md) |
| 9 | 文件系统 | [fs](fs.md) | [fs](../labs-2021/fs.md) |
| 10 | mmap | [mmap](mmap.md) | [mmap](../labs-2021/mmap.md) |

附：[实验通用指导](guidance.md)（[英文快照](../labs-2021/guidance.md)）、[课程日程中文整理](schedule.md)（[官网](https://pdos.csail.mit.edu/6.828/2021/schedule.html)）。英文快照中的日程表转换为 `[TABLE]` 占位符，因此日程整理依据官网原始 HTML，讲义等完整链接请以官网为准。

译文保留课程原版的分支、路径、构建与提交说明，便于理解 2021 年的作业；当前集成仓库的实际开发流程请遵循项目根目录的 `AGENTS.md`。代码、命令与示例输出以官网原文为依据；例如英文快照中 `xargs` 的测试命令因 HTML 转换遗漏了重定向，中文译文已按官网恢复为 `sh < xargstest.sh`。官网原文及其更新仍是权威来源。

# Kernel Code Organization

本文档记录内核模块整理的约束和目标结构。具体执行顺序以 `TODO.md` 为准；这里不重复维护功能 roadmap。

## 原则

- 小步提交，单次重构保持行为不变。
- 不在同一提交中混合文件移动、广泛重命名和语义修改。
- 优先拆分真正阻碍机制审计的边界，而不是追求目录形式完整。
- 新 helper 默认 `static`；跨模块 API 使用窄头文件。
- 新头文件必须自包含并使用 include guard。
- 保留 xv6 课程代码的可对照性和核心类型名。
- 每一步使用最小定向 suite 验证，阶段性重构运行 smoke。

## 已完成结构

syscall 层已经从原始集中结构拆分为：

| 模块 | 职责 |
| --- | --- |
| `syscall.c` | trap 后的 syscall 入口和 trace |
| `syscall_table.c` | syscall 编号、名称与 handler 分发表 |
| `sysarg.c` | 用户参数提取和地址/字符串读取 |
| `sysproc.c` | 进程与系统信息 syscall |
| `sysfd.c` | fd、read/write、fstat、pipe、lseek、getdents |
| `sysfile.c` | 路径和 inode 操作 |
| `sysmmap.c` | mmap/munmap syscall 参数层 |
| `sysnetcall.c` | 网络 fd syscall |

用户态 native runtime 也已拆分为 crt0、字符串/内存、文件 helper、USYSCALL helper、printf 和 allocator。

## 当前目标：VM 边界

目前 `usertrap()` 仍直接包含 COW fault 细节，VMA 查找、fault、unmap 和 cleanup 仍位于 `proc.c`。近期只推进以下边界：

```text
trap.c
  -> fault.c       page-fault 分类和高层分派
       -> cow.c    COW PTE、复制、引用计数和失败回滚
       -> vma.c    VMA 查找与 mmap lazy fault

proc.c
  -> vma.c         fork/exit/exec 调用的 VMA 生命周期 API
```

建议形成的窄接口：

- `handle_user_page_fault(scause, fault_addr)`
- `cow_fault(pagetable, va)`
- `vma_find(proc, va)`
- `vma_fault(proc, va, write)`
- `vma_unmap(proc, addr, length)`
- `vma_cleanup(proc, mode)`

具体函数名可随实现调整，关键是明确责任：trap 层不处理 PTE copy，进程生命周期层不实现 VMA 拆分细节。

## 头文件策略

短期继续使用 `kernel/*.h` 平铺布局。新增 VM 模块可以增加：

```text
kernel/fault.h
kernel/cow.h
kernel/vma.h
```

不要立即创建完整 `kernel/include/` 树，也不要一次性移除 `defs.h`。当多个稳定模块都形成独立公开接口后，再评估 include 目录迁移。

`defs.h` 的收缩顺序：

1. 新增模块不再扩大全局声明集合。
2. VM 新 API 进入窄头文件。
3. 每次只迁出一个子系统的声明。
4. 只有依赖已经足够少时才考虑删除 `defs.h`。

## 后续候选边界

VM 稳定后，按实际收益选择一个方向：

- 文件系统：区分 inode、block allocation、directory/namei、file 和 WAL。
- 进程：分离 scheduler/sleep/wakeup，但必须先记录锁顺序。
- 驱动与网络：只在接口边界确实阻碍维护时移动目录。

这些都不是当前并行任务。尤其不要为了获得“现代目录结构”一次性搬动整个 kernel。

## 重构验收

每个结构重构提交应满足：

- `git diff` 可清楚区分代码移动和少量适配。
- 锁顺序、引用计数、PTE flags 和失败路径保持不变。
- 新头文件通过 `clangd --check=<file>`。
- 修改的 C/头文件经过 `clang-format`。
- `make test-quick` 和对应 subsystem suite 通过。
- VM 阶段性提交额外运行 `make test-smoke`。

需要改变语义时，应先完成纯结构重构，再用独立提交实现和测试行为变化。

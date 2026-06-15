# Code Organization Migration Plan

本文档记录 `dev/all` 分支的代码组织迁移计划。项目可以逐步形成更清晰的 kernel 工程结构，但短期仍需要保留 xv6 / 课程实现的可对照性，避免在功能整合、测试迁移和代码重构之间制造过多变量。

迁移原则：小步提交、行为不变、模块边界清晰、每步可测试。

## 1. 目标

* 优先拆分当前职责最混杂、最影响后续维护的模块边界。
* 短期先整理 VM fault / COW / mmap；中期再考虑文件系统、进程调度和驱动目录。
* 保留现有 xv6 头文件布局，只有在模块边界稳定后再评估 `kernel/include/`。
* 收缩而不是立即删除 `defs.h`：新增模块尽量使用局部头文件，旧声明逐步迁移。
* 旧函数重命名放在模块拆分和测试稳定之后。
* 保留核心类型名，例如 `struct proc`、`struct inode`、`struct file`、`struct buf`。
* 对新写代码使用清晰语义名；旧代码只在对应模块重构时局部改善命名。

## 2. 非目标

* 不一次性重写整个 kernel。
* 不在同一提交里混合拆文件、重命名和逻辑优化。
* 不把全仓 include 迁移作为第一阶段目标。
* 不引入复杂构建系统替代 Makefile。
* 不改变 xv6 当前功能语义。
* 不追求生产级内核结构，本项目仍以学习、实验和可维护性为核心。
* 不在测试迁移尚未稳定前做大规模 kernel 文件移动。

## 3. 当前优先级

当前仓库已经有 Go host-side runner 并行入口 `make test-smoke-go`。代码组织重构应建立在测试入口可稳定验证的基础上。

当前状态：

* `make smoke` 仍是稳定对照入口，继续使用现有 Python grader。
* `make test-smoke-go` 是 Go host-side runner 的并行入口。
* Go runner 源码位于 `tests/host/cmd/xv6test/` 和 `tests/host/internal/testrunner/`。
* 构建产物和测试日志应继续保留在 `build/` 下，不重新混入源码目录。
* 测试迁移细节以 `docs/test-migrate.md` 为准，本文档只约束 kernel 代码组织重构。

短期优先级：

1. 稳定测试基线：`GOCACHE=/tmp/go-build-xv6test go test ./...`、`make test-smoke-go`、相关 `make grade-*`。
2. 拆出 `kernel/fault.c`，让 `trap.c` 只保留 trap 高层分派。
3. 拆出 COW fault helper，降低 `usertrap()` 中的 VM 细节密度。
4. 拆出 mmap / VMA 管理边界，减少 `proc.c` 的职责。
5. 完成上述边界后，再评估文件系统和头文件目录迁移。

## 4. 长期目标目录结构

长期目标结构如下：

```text
kernel/
  include/
    types.h
    param.h
    memlayout.h

    arch/
      riscv.h

    lock/
      spinlock.h
      sleeplock.h

    core/
      console.h
      printf.h

    mm/
      kalloc.h
      vm.h
      cow.h
      mmap.h
      fault.h

    proc/
      proc.h
      sched.h

    fs/
      fs.h          # on-disk filesystem format
      buf.h
      bio.h
      log.h
      inode.h
      file.h
      pipe.h

    dev/
      virtio.h
      virtio_disk.h
      uart.h
      plic.h
      e1000.h

    net/
      net.h

    syscall/
      syscall.h

  arch/
    riscv/
      start.c
      trampoline.S
      kernelvec.S
      swtch.S

  core/
    main.c
    printf.c
    console.c

  mm/
    kalloc.c
    vm.c
    cow.c
    mmap.c
    fault.c

  proc/
    proc.c
    sched.c
    exec.c

  fs/
    block.c
    bio.c
    log.c
    inode.c
    rw.c
    dir.c
    namei.c
    file.c
    pipe.c
    sysfile.c

  dev/
    virtio_disk.c
    uart.c
    plic.c
    e1000.c

  net/
    net.c
    sysnet.c

  syscall/
    syscall.c
    sysproc.c
```

这是长期目标，不是短期执行清单。短期可以只新增 `kernel/fault.c`、`kernel/cow.c`、`kernel/mmap.c` 等少量文件，继续沿用当前 `kernel/*.h` 头文件布局。

## 5. 头文件规范

### 5.1 基本原则

短期原则：

* 继续允许现有 `kernel/*.h` 布局。
* 新增模块可以先新增 `kernel/fault.h`、`kernel/cow.h`、`kernel/mmap.h` 这类窄头文件。
* 新增头文件必须自包含。
* 新增头文件必须有 include guard。
* 新增跨文件 API 不再随意塞进 `defs.h`。
* 只有跨文件调用时暴露函数。
* 文件内 helper 默认 `static`。

长期原则：

* 评估是否引入 `kernel/include/`。
* 如果引入，`kernel/include/` 下的头文件视为模块公开接口。
* 子系统内部共享声明放在该子系统的 `internal.h`，不对外暴露。
* `.h` 只放公开类型、公开常量、公开函数声明。
* `.c` 放实现细节、私有 helper、私有全局变量。
* 长期目标是收缩并最终删除 `defs.h`，但不作为第一阶段目标。

### 5.2 Include 路径

短期不要求 Makefile 立刻加入新的 include path。只有当 `kernel/include/` 目录真正开始迁移时，才加入：

```makefile
CFLAGS += -Ikernel/include
```

长期推荐 include 风格：

```c
#include "types.h"
#include "param.h"

#include "lock/spinlock.h"
#include "lock/sleeplock.h"

#include "fs/fs.h"
#include "fs/buf.h"
#include "fs/bio.h"
#include "fs/log.h"
```

长期不推荐继续依赖：

```c
#include "defs.h"
#include "fs.h"
#include "buf.h"
```

### 5.3 Include guard 风格

```c
#ifndef XV6_FS_BIO_H
#define XV6_FS_BIO_H

#include "types.h"

struct buf;

void binit(void);
struct buf *bread(uint dev, uint block_no);
void bwrite(struct buf *buf);
void brelse(struct buf *buf);
void bpin(struct buf *buf);
void bunpin(struct buf *buf);

#endif
```

## 6. `defs.h` 收缩计划

`defs.h` 是原版 xv6 的全局声明集合。短期不要为了删除它而牵动所有 `.c` 文件；更稳妥的策略是先停止继续扩大它，再逐步把新模块和高变动模块的声明迁出。

短期规则：

* 新增模块优先拥有自己的小头文件。
* 旧模块移动函数时，可以先保留 `defs.h` 兼容，避免一次性改太多 include。
* 每次迁出一组声明后，运行对应测试。
* 只有在大部分声明已经迁出后，再考虑删除 `defs.h`。

长期迁移顺序：

* [ ] 按模块重新整理 `defs.h` 分组。
* [ ] 新建或整理 VM / fault / COW / mmap 相关头文件。
* [ ] 新建或整理 fs / bio / log / inode 相关头文件。
* [ ] 新建或整理 proc / sched 相关头文件。
* [ ] 新建或整理 dev / net / syscall 相关头文件。
* [ ] 修改 `.c` 文件 include 对应模块头。
* [ ] 移除所有对 `defs.h` 的依赖。
* [ ] 删除 `defs.h`。

## 7. C 文件组织规范

推荐文件结构：

```c
// 文件职责说明

#include ...

// private macros

// private types

// private globals

// static helper declarations, if needed

// public functions

// static helper definitions
```

规则：

* 文件内 helper 使用 `static`。
* public API 放入对应头文件。
* 私有全局变量只在当前 `.c` 中定义。
* 跨模块共享状态必须谨慎，优先通过函数接口访问。
* 锁、引用计数、sleep/wakeup、日志事务相关函数必须写清调用约定。

## 8. 命名规范

### 8.1 总体规范

* 函数名：`lower_snake_case`
* 局部变量：`lower_snake_case`
* 宏：`UPPER_SNAKE_CASE`
* 类型名：保留现有 xv6 类型名，例如 `struct proc`、`struct inode`
* 文件内 helper：`static`
* 模块公开函数：使用清晰语义名，必要时加模块前缀

### 8.2 局部变量

新写代码和正在重构的局部代码应尽量使用清晰命名，但不要求为了命名全仓机械替换。保留课程对照价值的旧代码可以暂时继续使用 xv6 原始短名。

不推荐：

```c
struct inode *ip;
struct buf *bp;
struct proc *p;
uint bn;
uint off;
uint n;
uint addr;
```

推荐：

```c
struct inode *inode;
struct buf *buf;
struct proc *proc;
uint logical_block_no;
uint offset;
uint byte_count;
uint block_addr;
```

含义变化的变量必须拆分。

不推荐：

```c
uint addr;
```

推荐：

```c
uint doubly_indirect_addr;
uint indirect_addr;
uint data_block_addr;
```

允许极短作用域的循环变量：

```c
for (int i = 0; i < count; i++)
```

### 8.3 旧函数重命名策略

旧函数重命名放在模块拆分之后进行。

规则：

* 先拆模块。
* 再重命名。
* 每次只重命名一个子系统或一个很小的函数族。
* 重命名提交不修改行为。
* 重命名后运行对应测试。
* 如果函数名仍有 xv6 对照价值，可以暂缓重命名。

候选重命名：

```text
bget              -> buffer_get
bread             -> buffer_read
bwrite            -> buffer_write
brelse            -> buffer_release
bpin              -> buffer_pin
bunpin            -> buffer_unpin

ialloc            -> inode_alloc
iget              -> inode_get
idup              -> inode_dup
ilock             -> inode_lock
iunlock           -> inode_unlock
iput              -> inode_put
iupdate           -> inode_update
itrunc            -> inode_truncate
readi             -> inode_read
writei            -> inode_write

dirlookup         -> dir_lookup
dirlink           -> dir_link
namex             -> path_resolve
namei             -> path_lookup
nameiparent       -> path_lookup_parent

walk              -> pagetable_walk
walkaddr          -> pagetable_walk_addr
mappages          -> pagetable_map_pages
uvmunmap          -> user_vm_unmap
uvmcopy           -> user_vm_copy
copyin            -> user_copy_in
copyout           -> user_copy_out
```

## 9. 模块拆分计划

本节同时保留短期执行清单和长期目标。短期任务应优先选择“新增少量文件、行为不变、验证明确”的边界；长期任务只有在测试基线和模块 API 稳定后再执行。

### 9.1 VM / Fault / mmap

目标：

```text
kernel/vm.c       基础页表操作，短期保留原路径
kernel/kalloc.c   物理页分配和 refcount，短期保留原路径
kernel/cow.c      COW fork / COW fault
kernel/mmap.c     VMA / mmap / munmap
kernel/fault.c    user page fault 分派
```

TODO：

* [ ] 新建 `kernel/fault.h`。
* [ ] 新建 `kernel/fault.c`。
* [ ] 从 `trap.c` 抽出 page fault 分派。
* [ ] 新建 `kernel/cow.h`。
* [ ] 新建 `kernel/cow.c`。
* [ ] 迁移 COW fault、COW PTE 判断、COW page copy。
* [ ] 新建 `kernel/mmap.h`。
* [ ] 新建 `kernel/mmap.c`。
* [ ] 迁移 VMA 查找、mmap fault、munmap、cleanup。
* [ ] 保持 `trap.c` 只负责 trap 高层分派。
* [ ] 暂不移动 `vm.c` / `kalloc.c`，避免 Makefile 和 include 变化过大。
* [ ] 运行 `GOCACHE=/tmp/go-build-xv6test go test ./...`。
* [ ] 运行 `make test-smoke-go`。
* [ ] 运行 `make grade-traps`。
* [ ] 运行 `make grade-cow`。
* [ ] 运行 `make grade-mmap`。
* [ ] 必要时运行 `make smoke` 保留 Python grader 对照。

### 9.2 文件系统

长期目标：

```text
fs/block.c    superblock、balloc、bfree、bzero
fs/bio.c      buffer cache
fs/log.c      filesystem log
fs/inode.c    inode 生命周期
fs/rw.c       bmap、inode_read、inode_write
fs/dir.c      directory entry 操作
fs/namei.c    path resolution
fs/file.c     file table
fs/pipe.c     pipe
fs/sysfile.c  file-related syscalls
```

内部头：

```text
kernel/fs/internal.h
```

短期建议先不拆整个文件系统目录；如果要推进，应从 `fs.c` 中可独立移动且行为边界清楚的块开始，例如 block allocator 或 inode lifecycle。

TODO：

短期任务：

* [ ] 梳理 `kernel/fs.c`、`kernel/bio.c`、`kernel/log.c`、`kernel/file.c`、`kernel/pipe.c` 的职责边界。
* [ ] 优先选择一个低风险边界，例如 block allocator 或 inode lifecycle。
* [ ] 如需跨文件声明，先新增窄头文件，例如 `kernel/fs_block.h` 或 `kernel/inode.h`。
* [ ] 每次只移动一组函数，不重命名、不优化逻辑。
* [ ] 每一步运行 `make build`。
* [ ] 每一步运行 `make test-smoke-go`。
* [ ] 完成后运行 `make grade-fs`。
* [ ] 完成后运行 `make smoke`。

长期任务：

* [ ] 新建 `kernel/include/fs/fs.h`，放 on-disk format。
* [ ] 新建 `kernel/include/fs/inode.h`。
* [ ] 新建 `kernel/include/fs/bio.h`。
* [ ] 新建 `kernel/include/fs/log.h`。
* [ ] 新建 `kernel/fs/internal.h`。
* [ ] 拆出 `kernel/fs/block.c`。
* [ ] 移动 `bio.c` 到 `kernel/fs/bio.c`。
* [ ] 移动 `log.c` 到 `kernel/fs/log.c`。
* [ ] 拆出 `kernel/fs/inode.c`。
* [ ] 拆出 `kernel/fs/rw.c`。
* [ ] 拆出 `kernel/fs/dir.c`。
* [ ] 拆出 `kernel/fs/namei.c`。

### 9.3 Process / Scheduler

目标：

```text
proc/proc.c    进程生命周期
proc/sched.c   scheduler、sched、yield、sleep、wakeup
proc/exec.c    exec
```

TODO：

短期任务：

* [ ] 梳理 `proc.c` 中 process lifecycle、scheduler、sleep/wakeup、wait/exit 的边界。
* [ ] 如需跨文件声明，先新增窄头文件，例如 `kernel/sched.h`。
* [ ] 新建 `kernel/proc/sched.c`。
* [ ] 迁移 scheduler/sched/yield/sleep/wakeup。
* [ ] 暂不移动 `exec.c`。
* [ ] 保持锁顺序不变。
* [ ] 运行 `make grade-traps`。
* [ ] 运行 `make grade-cow`。
* [ ] 运行 `make smoke`。

长期任务：

* [ ] 新建 `kernel/include/proc/proc.h`。
* [ ] 新建 `kernel/include/proc/sched.h`。
* [ ] 移动 `exec.c` 到 `kernel/proc/exec.c`。

### 9.4 Device Drivers

目标：

```text
dev/virtio_disk.c
dev/uart.c
dev/plic.c
dev/e1000.c
```

TODO：

短期任务：

* [ ] 修正 virtio 相关锁和 DMA 注释。
* [ ] 运行 `make grade-net`。
* [ ] 运行 `make smoke`。

长期任务：

* [ ] 新建 `kernel/include/dev/virtio.h`。
* [ ] 新建 `kernel/include/dev/virtio_disk.h`。
* [ ] 新建 `kernel/include/dev/uart.h`。
* [ ] 新建 `kernel/include/dev/plic.h`。
* [ ] 新建 `kernel/include/dev/e1000.h`。
* [ ] 移动驱动实现到 `kernel/dev/`。

### 9.5 Network

目标：

```text
net/net.c
net/sysnet.c
```

TODO：

短期任务：

* [ ] 检查 e1000 与 net stack 的 include 边界。
* [ ] 运行 `make grade-net`。
* [ ] 运行 `make smoke`。

长期任务：

* [ ] 新建 `kernel/include/net/net.h`。
* [ ] 移动 `net.c` 到 `kernel/net/net.c`。
* [ ] 移动 `sysnet.c` 到 `kernel/net/sysnet.c`。

### 9.6 Syscall

目标：

```text
syscall/syscall.c
syscall/sysproc.c
```

TODO：

短期任务：

* [ ] 梳理 syscall dispatch、`sysproc.c`、`sysfile.c`、`sysnet.c` 的归属边界。
* [ ] 评估 `sysfile.c`、`sysnet.c` 是否保留在对应子系统，而不是放入 syscall 目录。
* [ ] 运行 `make grade-syscall`。
* [ ] 运行 `make smoke`。

长期任务：

* [ ] 新建 `kernel/include/syscall/syscall.h`。
* [ ] 移动 syscall dispatch。
* [ ] 移动 sysproc。

## 10. 注释规范

注释主要解释：

* 模块职责。
* 锁约定。
* 生命周期。
* 引用计数。
* 崩溃一致性。
* 硬件交互。
* 非显然边界条件。

不写显而易见注释。

不推荐：

```c
i++; // i 加 1
```

推荐：

```c
// sleep() 会原子释放 vdisk_lock；中断处理完成后通过 wakeup(buf) 唤醒等待者。
sleep(buf, &disk.vdisk_lock);
```

技术名保留英文：

```text
超级块 (superblock)
空闲位图 (bitmap)
间接块 (indirect block)
页表项 (PTE)
写时复制 (copy-on-write, COW)
内存映射 (mmap)
描述符环 (descriptor ring)
```

锁相关注释必须说明：

* 调用者是否必须持锁。
* 函数内部是否可能 sleep。
* 是否会释放传入锁。
* 返回时持有什么锁。
* 是否可能触发 I/O。

## 11. 测试要求

### 11.1 基础检查

```bash
make build
make image
```

### 11.2 常规检查

```bash
GOCACHE=/tmp/go-build-xv6test go test ./...
make test-smoke-go
make smoke
```

### 11.3 子系统检查

```bash
make grade-fs
make grade-mmap
make grade-cow
make grade-net
make grade-lock
make grade-syscall
make grade-traps
```

### 11.4 重型回归

```bash
make regression
make grade-all-heavy
```

### 11.5 Go runner

```bash
GOCACHE=/tmp/go-build-xv6test go test ./...
make test-smoke-go
```

## 12. 提交规范

每个提交只做一种事。

推荐：

```text
refactor(fs): split inode lifecycle module
```

正文示例：

```text
- Move inode allocation, locking, update, and release helpers into fs/inode.c
- Add fs/inode.h and any narrow module header needed
- Keep inode behavior unchanged
- Validate with make test-smoke-go, make grade-fs, and make smoke
```

不推荐：

```text
拆 fs.c、重命名变量、优化 bmap、改 mmap 语义、更新测试
```

迁移顺序建议：

```text
1. 新增头文件
2. 移动函数
3. 修 include
4. 跑测试
5. 提交
6. 再做命名优化
7. 再做逻辑优化
```

## 13. 分阶段路线

### Phase 0：确认规范与测试基线

* [ ] 确认短期继续沿用 `kernel/*.h` 头文件布局。
* [ ] 确认 `kernel/include/` 是长期目标，不作为第一阶段任务。
* [ ] 确认类型名保留。
* [ ] 确认旧函数重命名放在模块拆分之后。
* [ ] 确认每个重构提交必须行为不变。
* [ ] 确认基线验证命令：`GOCACHE=/tmp/go-build-xv6test go test ./...`、`make test-smoke-go`、相关 `make grade-*`。
* [ ] 将规范同步到 `AGENTS.md`。

### Phase 1：拆 VM fault / COW / mmap 边界

* [ ] 新建 `kernel/fault.c` / `kernel/fault.h`。
* [ ] 将 `usertrap()` 中的 page fault 分派迁入 `fault.c`。
* [ ] 新建 `kernel/cow.c` / `kernel/cow.h`。
* [ ] 将 COW fault、PTE_COW 判断、COW page copy 迁入 `cow.c`。
* [ ] 新建 `kernel/mmap.c` / `kernel/mmap.h`。
* [ ] 将 VMA 查找、mmap fault、munmap、cleanup 迁入 `mmap.c`。
* [ ] 暂不移动 `vm.c` / `kalloc.c`。
* [ ] 运行 `GOCACHE=/tmp/go-build-xv6test go test ./...`。
* [ ] 运行 `make test-smoke-go`。
* [ ] 运行 `make grade-traps`、`make grade-cow`、`make grade-mmap`。

### Phase 2：收缩 `defs.h`

* [ ] 整理 `defs.h` 分组，标出 VM / COW / mmap / fs / proc / net 声明边界。
* [ ] 新增模块不再继续扩大 `defs.h`。
* [ ] 将已经拆出的 `fault`、`cow`、`mmap` API 迁入窄头文件。
* [ ] 每次只迁出一小组声明。
* [ ] 运行 `make test-smoke-go` 和相关 `make grade-*`。
* [ ] 暂不要求删除 `defs.h`。

### Phase 3：文件系统小步拆分

* [ ] 优先选择一个低风险边界，例如 block allocator 或 inode lifecycle。
* [ ] 每次只移动一组函数，不重命名、不优化逻辑。
* [ ] 保留当前头文件布局，必要时新增窄头。
* [ ] 运行 `make build`。
* [ ] 运行 `make test-smoke-go`。
* [ ] 运行 `make grade-fs`。

### Phase 4：proc / scheduler 小步拆分

* [ ] 先评估 scheduler/sleep/wakeup 与 `proc.c` 的锁边界。
* [ ] 只在锁顺序明确后移动函数。
* [ ] 暂不同时移动 `exec.c`。
* [ ] 运行 traps/cow/smoke 相关测试。

### Phase 5：drivers / net / syscall 目录迁移

* [ ] 只有在 VM 和 fs 拆分稳定后再启动。
* [ ] 每次移动一个子系统。
* [ ] 保持 Makefile 变更最小。
* [ ] 运行对应 `make grade-*` 和 `make test-smoke-go`。

### Phase 6：评估 `kernel/include/`

* [ ] 只有当多个模块已经形成稳定边界后再评估。
* [ ] 决定是否创建 `kernel/include/`。
* [ ] 如果创建，先迁移新增模块头文件，不一次性迁移所有 xv6 原头。
* [ ] 调整 Makefile include path。
* [ ] 运行全量 smoke 和相关 grader。

### Phase 7：旧函数重命名

* [ ] 按子系统制定 rename 表。
* [ ] 每次只重命名一个子系统。
* [ ] 保留仍有课程对照价值的函数名。
* [ ] 不修改行为。
* [ ] 跑对应测试。
* [ ] 更新文档和注释。

### Phase 8：逻辑优化

仅在结构稳定后进行。

* [ ] 优化 buffer cache replacement。
* [ ] 优化 filesystem log normal install path。
* [ ] 优化 block/inode allocation hint。
* [ ] 增加 stats 和测试。
* [ ] 每项优化单独提交。

## 14. 风险控制

主要风险：

* include 顺序变化导致编译失败。
* Makefile 漏编译新文件。
* public/static 边界错误导致链接失败。
* 移动函数时遗漏声明。
* 锁语义被误改。
* 重命名和逻辑修改混在一起导致 bug 难定位。
* 测试覆盖不足导致重构回归未发现。

控制策略：

* 一次只迁移一个模块。
* 迁移提交不改行为。
* 重命名提交不改行为。
* 逻辑优化单独提交。
* 每步跑对应测试。
* 锁、sleep/wakeup、refcount、log transaction 相关代码额外审查。

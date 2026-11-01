# 实验：页表

本实验将探索页表，并修改页表来加快某些系统调用，以及检测哪些页面被访问过。

开始编码前，请阅读 [xv6 教材](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)第 3 章和相关文件：

- `kern/memlayout.h`：描述内存布局。
- `kern/vm.c`：包含大部分虚拟内存（VM）代码。
- `kernel/kalloc.c`：包含分配和释放物理内存的代码。

参考 [RISC-V 特权架构手册](https://github.com/riscv/riscv-isa-manual/releases/download/Ratified-IMFDQC-and-Priv-v1.11/riscv-privileged-20190608.pdf)也可能有所帮助。

> 译注：上面的 `kern/` 路径沿用官网原文；实际源码目录名为 `kernel/`，应查看 `kernel/memlayout.h` 和 `kernel/vm.c`。

开始实验时切换到 `pgtbl` 分支：

```sh
git fetch
git checkout pgtbl
make clean
```

## 加速系统调用（简单）

某些操作系统（例如 Linux）通过在用户空间和内核之间共享只读区域中的数据来加速特定系统调用，使执行这些调用时无须进入内核。为了学习如何向页表插入映射，你的第一个任务是在 xv6 中为 `getpid()` 实现这一优化。

创建每个进程时，在 `USYSCALL`（`memlayout.h` 中定义的虚拟地址）映射一个只读页面。在页面开头存放一个 `struct usyscall`（也在 `memlayout.h` 中定义），并将其初始化为保存当前进程的 PID。本实验已在用户空间提供 `ugetpid()`，它会自动使用 `USYSCALL` 映射。运行 `pgtbltest` 时，如果 `ugetpid` 测试通过，即可获得本部分全部分数。

提示：

- 可以在 `kernel/proc.c` 的 `proc_pagetable()` 中建立映射。
- 选择权限位，使用户空间只能读取该页面。
- `mappages()` 可能是有用的辅助函数。
- 别忘了在 `allocproc()` 中分配并初始化页面。
- 确保在 `freeproc()` 中释放页面。

还有哪些 xv6 系统调用可以利用这个共享页面加速？请解释方法。

## 打印页表（简单）

为了帮助你直观理解 RISC-V 页表，也可能方便日后调试，第二项任务是编写一个打印页表内容的函数。

定义函数 `vmprint()`，接受一个 `pagetable_t` 参数，并按下述格式打印页表。在 `exec.c` 的 `return argc` 之前插入 `if(p->pid==1) vmprint(p->pagetable)`，打印第一个进程的页表。通过 `make grade` 中的 `pte printout` 测试即可获得本部分全部分数。

现在启动 xv6 时，应打印类似下面的输出，描述第一个进程刚通过 `exec()` 运行 `init` 后的页表：

```text
page table 0x0000000087f6e000
 ..0: pte 0x0000000021fda801 pa 0x0000000087f6a000
 .. ..0: pte 0x0000000021fda401 pa 0x0000000087f69000
 .. .. ..0: pte 0x0000000021fdac1f pa 0x0000000087f6b000
 .. .. ..1: pte 0x0000000021fda00f pa 0x0000000087f68000
 .. .. ..2: pte 0x0000000021fd9c1f pa 0x0000000087f67000
 ..255: pte 0x0000000021fdb401 pa 0x0000000087f6d000
 .. ..511: pte 0x0000000021fdb001 pa 0x0000000087f6c000
 .. .. ..509: pte 0x0000000021fdd813 pa 0x0000000087f76000
 .. .. ..510: pte 0x0000000021fddc07 pa 0x0000000087f77000
 .. .. ..511: pte 0x0000000020001c0b pa 0x0000000080007000
```

第一行显示传给 `vmprint` 的参数。此后每个 PTE 占一行，包括指向页表树中更深层页表页面的 PTE。每行用若干个 `" .."` 缩进，表示该项在树中的深度。每行显示 PTE 在其页表页中的索引、PTE 位以及从 PTE 中提取的物理地址。不要打印无效的 PTE。

在上述例子中，顶层页表页的第 0 项和第 255 项有映射；第 0 项对应的下一层只有索引 0 有映射；再往下的最底层中，索引 0、1、2 有映射。

你的代码输出的物理地址可能与示例不同，但条目数量和虚拟地址应相同。这里的“虚拟地址”是指页表项在页表树中对应的虚拟地址范围；示例本身打印的是各级页表项的索引。

提示：

- 可以将 `vmprint()` 放在 `kernel/vm.c` 中。
- 使用 `kernel/riscv.h` 末尾的宏。
- `freewalk` 函数可能提供启发。
- 在 `kernel/defs.h` 中声明 `vmprint` 的原型，以便从 `exec.c` 调用。
- 在 `printf` 中使用 `%p`，像示例一样打印完整的 64 位十六进制 PTE 和地址。

结合教材图 3-4 解释 `vmprint` 的输出。第 0 页包含什么？第 2 页包含什么？进程运行在用户态时，能否读写第 1 页映射的内存？倒数第三页包含什么？

## 检测哪些页面被访问过（困难）

一些垃圾收集器（一种自动内存管理方式）可以利用页面是否被访问过（读或写）的信息。本部分要求你为 xv6 添加功能：通过检查 RISC-V 页表中的访问位，检测并向用户空间报告这些信息。RISC-V 硬件页表遍历器每次处理 TLB 未命中时，都会在 PTE 中设置这些位。

你的任务是实现系统调用 `pgaccess()`，报告哪些页面被访问过。该调用接受三个参数：第一个是要检查的第一个用户页的起始虚拟地址；第二个是要检查的页面数量；第三个是用户空间缓冲区的地址，用于将结果存为位掩码（一种每页对应一位的数据结构，第一页对应最低有效位）。运行 `pgtbltest` 时，如果 `pgaccess` 测试通过，即可获得本部分全部分数。

提示：

- 从实现 `kernel/sysproc.c` 中的 `sys_pgaccess()` 开始。
- 需要使用 `argaddr()` 和 `argint()` 解析参数。
- 对于输出位掩码，先在内核临时缓冲区中设置各位，再通过 `copyout()` 复制给用户更容易。
- 可以对一次扫描的页数设定上限。
- `kernel/vm.c` 的 `walk()` 对查找正确的 PTE 很有用。
- 需要在 `kernel/riscv.h` 中定义访问位 `PTE_A`，其值可查阅 RISC-V 手册。
- 检查 `PTE_A` 是否置位后，一定要清除它；否则无法判断自上次调用 `pgaccess()` 后页面是否被访问过，因为该位会一直保持置位。
- `vmprint()` 可能有助于调试页表。

## 提交实验

**至此实验完成。** 确保通过 `make grade` 的所有测试。如有问题，别忘了将答案写入 `answers-实验名.txt`（将“实验名”替换为本实验标识）。提交修改（包括添加答案文件），然后在实验目录运行 `make handin`。

### 耗费时间

创建新文件 `time.txt`，其中只写一个整数，表示完成实验所花的小时数。别忘了对该文件执行 `git add` 和 `git commit`。

### 提交

通过[提交网站](https://6828.scripts.mit.edu/2021/handin.py/)提交作业。首次提交作业或实验前，需要在网站申请 API 密钥。提交最终修改后，运行 `make handin`：

```text
$ git commit -am "ready to submit my lab"
[util c2e3c8b] ready to submit my lab
 2 files changed, 18 insertions(+), 2 deletions(-)

$ make handin
tar: Removing leading `/' from member names
Get an API key for yourself by visiting https://6828.scripts.mit.edu/2021/handin.py/
Please enter your API key: XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100 79258  100   239  100 79019    853   275k --:--:-- --:--:-- --:--:--  276k
$
```

`make handin` 会将 API 密钥保存在 `myapi.key` 中。如需更换密钥，删除该文件并让 `make handin` 重新生成；该文件不能包含换行符。

如果运行 `make handin` 时有未提交的修改或未跟踪文件，会看到类似输出：

```text
 M hello.c
?? bar.c
?? foo.pyc
Untracked files will not be handed in.  Continue? [y/N]
```

检查这些行，确保解答所需的所有文件均已被跟踪，即没有出现在以 `??` 开头的行中。可使用 `git add filename` 跟踪新文件。

如果 `make handin` 无法正常工作，可以尝试修复 curl 或 Git 命令的问题；也可以运行 `make tarball`，再通过[网页界面](https://6828.scripts.mit.edu/2021/handin.py/)上传生成的 tar 文件。

- 运行 `make grade`，确保代码通过全部测试。
- 运行 `make handin` 前提交修改过的源代码。
- 可在[提交网站](https://6828.scripts.mit.edu/2021/handin.py/)查看提交状态并下载已提交的代码。

## 可选挑战

- 使用超级页减少页表中的 PTE 数量。
- 取消用户进程第一页的映射，使解引用空指针时触发异常。你需要让用户代码段从例如 4096 而不是 0 的地址开始。
- 添加一个系统调用，使用 `PTE_D` 报告脏页（被修改过的页面）。

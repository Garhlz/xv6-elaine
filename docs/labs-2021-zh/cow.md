# 实验：xv6 的写时复制 fork

虚拟内存提供了一层间接映射：内核可以将页表项（PTE）标记为无效或只读，从而在访问内存时触发缺页异常，拦截内存访问；内核也可以通过修改 PTE 改变地址的含义。计算机系统中有一种说法：任何系统问题都可以通过增加一层间接映射来解决。惰性分配实验是一个例子，本实验将探讨另一个例子：写时复制 fork。

开始实验前，切换到 `cow` 分支：

```sh
git fetch
git checkout cow
make clean
```

## 问题

xv6 的 `fork()` 系统调用会把父进程的全部用户空间内存复制给子进程。父进程较大时，复制可能耗时很长。更糟的是，这些工作常常大部分被浪费：例如，`fork()` 后紧接着执行 `exec()` 的子进程会丢弃复制得到的内存，甚至可能从未使用过其中大部分页面。另一方面，如果父子进程都使用某个页面，而且至少有一方要写入它，就确实需要复制。

## 解决方案

写时复制（copy-on-write，COW）`fork()` 的目标是推迟物理页面的分配和复制，直到父进程或子进程需要写入共享页面时才进行；如果始终不需要写入，就无需复制。

COW `fork()` 只为子进程创建页表，让用户内存的 PTE 指向父进程的物理页面，并将父子进程中的所有用户 PTE 标记为不可写。当任一进程试图写入这些 COW 页面时，CPU 会触发缺页异常。内核的缺页处理程序检测到这种情况后，为发生异常的进程分配一个物理页，将原页面复制到新页面，并修改该进程的对应 PTE，使其指向新页面并标记为可写。缺页处理程序返回后，用户进程就可以写入自己的副本。

COW `fork()` 使用户内存物理页的释放变得稍微复杂：一个物理页可能被多个进程的页表引用，只有最后一个引用消失时才能释放。

## 实现写时复制（困难）

你的任务是在 xv6 内核中实现写时复制 `fork`。修改后的内核能够成功运行 `cowtest` 和 `usertests` 两个程序，即完成实验。

我们提供了 xv6 程序 `cowtest`（源码位于 `user/cowtest.c`）来帮助测试。它包含多个测试，但在未修改的 xv6 上，连第一个测试都会失败：

```text
$ cowtest
simple: fork() failed
$
```

`simple` 测试分配超过一半的可用物理内存，然后调用 `fork()`。由于没有足够的空闲物理内存为子进程完整复制父进程的内存，`fork` 会失败。

完成后，你的内核应通过 `cowtest` 和 `usertests` 的所有测试：

```text
$ cowtest
simple: ok
simple: ok
three: zombie!
ok
three: zombie!
ok
three: zombie!
ok
file: ok
ALL COW TESTS PASSED
$ usertests
...
ALL TESTS PASSED
$
```

可以按照以下步骤实现：

1. 修改 `uvmcopy()`，将父进程的物理页映射到子进程，而不是分配新页。清除父子进程 PTE 中的 `PTE_W`。
2. 修改 `usertrap()`，使其能够识别缺页异常。COW 页面发生缺页异常时，使用 `kalloc()` 分配新页，将旧页复制到新页，再更新 PTE 使其指向新页，并设置 `PTE_W`。
3. 确保物理页在最后一个 PTE 引用消失时才被释放。一个好办法是为每个物理页维护一个“引用计数”，记录有多少用户页表引用它。`kalloc()` 分配页面时将计数设为 1；`fork` 使子进程共享页面时增加计数；进程从页表中移除页面时减少计数。只有计数为零，`kfree()` 才能将页面放回空闲链表。可以用固定大小的整数数组存放计数；你需要确定索引方式和数组大小。例如，可以用物理地址除以 4096 得到索引，并根据 `kalloc.c` 中 `kinit()` 放入空闲链表的页面的最高物理地址，确定足以容纳最大索引的数组大小。
4. 修改 `copyout()`，使其遇到 COW 页面时使用与缺页处理相同的方案。

提示：

- 惰性页面分配实验可能已经让你熟悉了许多与 COW 相关的 xv6 内核代码。不过，不要在惰性分配的解答上继续本实验；请按上面的说明从一份新的 xv6 开始。
- 记录每个 PTE 是否为 COW 映射可能很有用。可以使用 RISC-V PTE 中的 RSW（保留给软件使用）位。
- `usertests` 覆盖了 `cowtest` 没有测试的场景，因此别忘了检查两个程序的所有测试都通过。
- `kernel/riscv.h` 末尾有一些有用的页表标志宏和定义。
- 如果发生 COW 缺页异常时没有空闲内存，应终止该进程。

## 提交实验

**至此实验完成。** 确保通过 `make grade` 的所有测试。如果实验包含问题，别忘了将答案写入 `answers-实验名.txt`（将“实验名”替换为本实验标识）。提交你的修改（包括添加答案文件），然后在实验目录中运行 `make handin`。

### 耗费时间

创建新文件 `time.txt`，其中只写一个整数，表示你花在实验上的小时数。别忘了对该文件执行 `git add` 和 `git commit`。

### 提交

你需要通过[提交网站](https://6828.scripts.mit.edu/2021/handin.py/)提交作业。首次提交作业或实验前，需要在网站上申请 API 密钥。

提交最终修改后，运行 `make handin`：

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

`make handin` 会将 API 密钥保存在 `myapi.key` 中。如需更换密钥，只需删除该文件，让 `make handin` 重新生成；`myapi.key` 不能包含换行符。

如果运行 `make handin` 时存在未提交的修改或未跟踪文件，会看到类似输出：

```text
 M hello.c
?? bar.c
?? foo.pyc
Untracked files will not be handed in.  Continue? [y/N]
```

检查这些行，确保解答所需的文件都已被 Git 跟踪，即没有出现在以 `??` 开头的行中。可通过 `git add filename` 跟踪新文件。

如果 `make handin` 无法正常工作，可以尝试修复 curl 或 Git 命令的问题；也可以运行 `make tarball`，生成 tar 文件后通过[网页界面](https://6828.scripts.mit.edu/2021/handin.py/)上传。

- 请运行 `make grade`，确保代码通过所有测试。
- 运行 `make handin` 前提交所有修改过的源代码。
- 可以在[提交网站](https://6828.scripts.mit.edu/2021/handin.py/)查看提交状态并下载提交的代码。

## 可选挑战

- 修改 xv6，使其同时支持惰性页面分配和 COW。
- 测量你的 COW 实现减少了多少 xv6 复制的字节数和分配的物理页数，并寻找机会进一步减少这些数字。

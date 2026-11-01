# 实验：锁

本实验将让你练习重新设计代码以提高并行性。多核机器上并行性差的常见表现是锁竞争激烈。提高并行性通常需要同时改变数据结构和加锁策略，减少竞争。本实验将对 xv6 的内存分配器和块缓存进行这样的改造。

编码前，请阅读 [xv6 教材](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)以下内容：

- 第 6 章“锁”和对应代码。
- 第 3.5 节“代码：物理内存分配器”。
- 第 8.1 至 8.3 节：“概览”“缓冲区缓存层”和“代码：缓冲区缓存”。

```sh
git fetch
git checkout lock
make clean
```

## 内存分配器（中等）

程序 `user/kalloctest` 对 xv6 内存分配器施加压力：三个进程不断扩张和缩小地址空间，导致大量调用 `kalloc` 和 `kfree`。这两个函数都要获取 `kmem.lock`。`kalloctest` 为 `kmem` 锁及其他几把锁打印 `acquire` 循环的迭代次数（标为 `#fetch-and-add`）：这些迭代来自尝试获取已被其他核心持有的锁。迭代次数可以粗略衡量锁竞争。实验完成前，输出类似：

```text
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmem: #fetch-and-add 83375 #acquire() 433015
lock: bcache: #fetch-and-add 0 #acquire() 1260
--- top 5 contended locks:
lock: kmem: #fetch-and-add 83375 #acquire() 433015
lock: proc: #fetch-and-add 23737 #acquire() 130718
lock: virtio_disk: #fetch-and-add 11159 #acquire() 114
lock: proc: #fetch-and-add 5937 #acquire() 130786
lock: proc: #fetch-and-add 4080 #acquire() 130786
tot= 83375
test1 FAIL
```

`acquire` 为每把锁维护两项计数：该锁的 `acquire` 调用次数，以及 `acquire` 循环尝试获取锁却失败的次数。`kalloctest` 调用一个系统调用，让内核打印本实验关注的 `kmem`、`bcache` 锁，以及竞争最激烈的五把锁的计数。锁竞争激烈时，失败重试的循环次数会很大。该系统调用返回 `kmem` 和 `bcache` 锁的循环次数总和。

本实验必须使用专用、空闲的多核机器。如果机器正在做其他事情，`kalloctest` 打印的计数将没有意义。可以使用专用 Athena 工作站或自己的笔记本电脑，但不要使用 dialup 机器。

`kalloctest` 锁竞争的根本原因是 `kalloc()` 只有一条由一把锁保护的空闲链表。要消除竞争，需要重新设计分配器，避免单一的锁和链表。基本思路是为每个 CPU 维护一条空闲链表，每条链表有自己的锁。不同 CPU 上的分配和释放可以并行，因为各 CPU 操作不同链表。

主要挑战是处理某个 CPU 的空闲链表为空而另一个 CPU 仍有空闲内存的情况：前者必须从后者的空闲链表中“窃取”一部分。窃取可能产生锁竞争，但希望它发生得较少。

你的任务是实现每 CPU 空闲链表，以及链表为空时的窃取机制。所有锁名都必须以 `kmem` 开头，即对每把锁调用 `initlock` 时传入以 `kmem` 开头的名称。运行 `kalloctest` 检查是否减少了锁竞争；运行 `usertests sbrkmuch` 检查是否仍能分配全部内存。输出应与下例类似，尽管具体数字不同，`kmem` 锁的总竞争应大幅减少。确保 `usertests` 全部通过，且 `make grade` 报告 `kalloctest` 测试通过。

```text
$ kalloctest
start test1
test1 results:
--- lock kmem/bcache stats
lock: kmem: #fetch-and-add 0 #acquire() 42843
lock: kmem: #fetch-and-add 0 #acquire() 198674
lock: kmem: #fetch-and-add 0 #acquire() 191534
lock: bcache: #fetch-and-add 0 #acquire() 1242
--- top 5 contended locks:
lock: proc: #fetch-and-add 43861 #acquire() 117281
lock: virtio_disk: #fetch-and-add 5347 #acquire() 114
lock: proc: #fetch-and-add 4856 #acquire() 117312
lock: proc: #fetch-and-add 4168 #acquire() 117316
lock: proc: #fetch-and-add 2797 #acquire() 117266
tot= 0
test1 OK
start test2
total free number of pages: 32499 (out of 32768)
.....
test2 OK
$ usertests sbrkmuch
usertests starting
test sbrkmuch: OK
ALL TESTS PASSED
$ usertests
...
ALL TESTS PASSED
$
```

提示：

- 可以使用 `kernel/param.h` 中的常量 `NCPU`。
- 让 `freerange` 将所有空闲内存交给运行它的 CPU。
- `cpuid` 返回当前核心编号，但只有关闭中断时调用它并使用其结果才安全。应使用 `push_off()` 和 `pop_off()` 关闭与恢复中断。
- 可以参考 `kernel/sprintf.c` 的 `snprintf` 了解字符串格式化；不过将所有锁都命名为 `kmem` 也可以。

## 缓冲区缓存（困难）

本部分与前半部分独立；无论是否完成前半部分，都可以完成并通过本部分测试。

如果多个进程密集使用文件系统，很可能会竞争 `bcache.lock`。该锁保护 `kernel/bio.c` 中的磁盘块缓存。`bcachetest` 创建多个进程反复读取不同文件，以制造对 `bcache.lock` 的竞争。实验完成前，输出类似：

```text
$ bcachetest
start test0
test0 results:
--- lock kmem/bcache stats
lock: kmem: #fetch-and-add 0 #acquire() 33035
lock: bcache: #fetch-and-add 16142 #acquire() 65978
--- top 5 contended locks:
lock: virtio_disk: #fetch-and-add 162870 #acquire() 1188
lock: proc: #fetch-and-add 51936 #acquire() 73732
lock: bcache: #fetch-and-add 16142 #acquire() 65978
lock: uart: #fetch-and-add 7505 #acquire() 117
lock: proc: #fetch-and-add 6937 #acquire() 73420
tot= 16142
test0: FAIL
start test1
test1 OK
```

实际输出可能不同，但 `bcache` 锁的 `acquire` 循环次数会很高。查看 `kernel/bio.c` 可知，`bcache.lock` 保护已缓存块缓冲区的链表、每个缓冲区的引用计数（`b->refcnt`），以及缓存块的标识（`b->dev` 和 `b->blockno`）。

修改块缓存，使运行 `bcachetest` 时 bcache 中所有锁的 `acquire` 循环次数接近零。理想情况下这些锁的计数总和应为零，但小于 500 也可以。修改 `bget` 和 `brelse`，使对 bcache 中不同块的并发查找与释放不太可能在锁上冲突，例如不必全部等待 `bcache.lock`。必须维持“不超过一个缓存副本对应同一个块”的不变量。完成后输出应与下例类似但不必完全相同。确保 `usertests` 仍然通过，且 `make grade` 通过全部测试。

```text
$ bcachetest
start test0
test0 results:
--- lock kmem/bcache stats
lock: kmem: #fetch-and-add 0 #acquire() 32954
lock: kmem: #fetch-and-add 0 #acquire() 75
lock: kmem: #fetch-and-add 0 #acquire() 73
lock: bcache: #fetch-and-add 0 #acquire() 85
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4159
lock: bcache.bucket: #fetch-and-add 0 #acquire() 2118
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4274
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4326
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6334
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6321
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6704
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6696
lock: bcache.bucket: #fetch-and-add 0 #acquire() 7757
lock: bcache.bucket: #fetch-and-add 0 #acquire() 6199
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4136
lock: bcache.bucket: #fetch-and-add 0 #acquire() 4136
lock: bcache.bucket: #fetch-and-add 0 #acquire() 2123
--- top 5 contended locks:
lock: virtio_disk: #fetch-and-add 158235 #acquire() 1193
lock: proc: #fetch-and-add 117563 #acquire() 3708493
lock: proc: #fetch-and-add 65921 #acquire() 3710254
lock: proc: #fetch-and-add 44090 #acquire() 3708607
lock: proc: #fetch-and-add 43252 #acquire() 3708521
tot= 128
test0: OK
start test1
test1 OK
$ usertests
  ...
ALL TESTS PASSED
$
```

请让所有锁名以 `bcache` 开头，即对每把锁调用 `initlock` 时传入以 `bcache` 开头的名称。

减少块缓存竞争比减少 `kalloc` 竞争更难，因为 bcache 缓冲区确实由进程（因此也由 CPU）共享。对于 `kalloc`，可以为每个 CPU 提供独立分配器以消除大部分竞争；块缓存则不能这样做。建议使用每个哈希桶各有一把锁的哈希表，在缓存中查找块号。

以下情况允许解答发生锁冲突：

- 两个进程并发使用相同块号。`bcachetest` 的 `test0` 不会这样做。
- 两个进程并发发生缓存未命中，都需要查找未使用块进行替换。`bcachetest` 的 `test0` 不会这样做。
- 两个进程使用的块在你所采用的块和锁分区方案中发生冲突，例如块号哈希到同一个桶。`bcachetest` 的 `test0` 是否发生这种情况取决于你的设计，但应尽量调整方案细节避免冲突，例如改变哈希表大小。

`bcachetest` 的 `test1` 使用的不同块数量超过缓冲区数量，并会覆盖许多文件系统代码路径。

提示：

- 阅读 xv6 教材第 8.1 至 8.3 节对块缓存的描述。
- 可以使用固定数量的桶，而不动态调整哈希表大小。使用素数个桶（例如 13）以减少哈希冲突的可能性。
- 在哈希表中查找缓冲区，以及未找到时为其分配条目的过程，必须是原子的。
- 移除所有缓冲区的链表（`bcache.head` 等），改为用缓冲区最近使用时间作为时间戳，即使用 `kernel/trap.c` 中的 `ticks`。这样 `brelse` 无须获取 bcache 锁，`bget` 可根据时间戳选择最近最少使用的块。
- 可以将 `bget` 中的淘汰过程串行化，即查找缓存未命中时选择要复用的缓冲区的部分。
- 某些情况下可能需要同时持有两把锁，例如淘汰时同时持有 bcache 锁和桶锁。确保避免死锁。
- 替换块时，可能需要将 `struct buf` 从一个桶移到另一个桶，因为新块哈希到不同的桶。有一个棘手情况：新旧块可能哈希到同一桶。确保此时不会死锁。
- 调试时，可以先实现桶锁，但保留 `bget` 开头和结尾对全局 `bcache.lock` 的获取与释放，使代码串行。确认正确且不存在竞态后，再移除全局锁并处理并发问题。也可以运行 `make CPUS=1 qemu` 进行单核运行测试。

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

- 使缓冲区缓存的查找无锁。提示：使用 GCC 的 `__sync_*` 函数。如何说服自己实现是正确的？

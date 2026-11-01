# 实验：多线程

本实验帮助你熟悉多线程。你将为用户级线程库实现线程切换，使用多线程加速程序，并实现一个屏障。

开始编码前，请确保阅读 [xv6 教材](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)的“第 7 章：调度”，并研究对应代码。

开始实验时切换到 `thread` 分支：

```sh
git fetch
git checkout thread
make clean
```

## Uthread：线程间切换（中等）

本练习要求设计并实现用户级线程系统的上下文切换机制。为帮助你入门，xv6 中提供了 `user/uthread.c`、`user/uthread_switch.S`，以及 Makefile 中构建 `uthread` 的规则。`uthread.c` 包含用户级线程库的大部分代码和三个简单测试线程；线程创建与线程切换部分代码尚未完成。

你的任务是设计线程创建以及通过保存／恢复寄存器切换线程的方案，并实现它。完成后，`make grade` 应报告通过 `uthread` 测试。

完成后，在 xv6 中运行 `uthread` 应看到以下输出，三个线程的启动顺序可能不同：

```text
$ make qemu
...
$ uthread
thread_a started
thread_b started
thread_c started
thread_c 0
thread_a 0
thread_b 0
thread_c 1
thread_a 1
thread_b 1
...
thread_c 99
thread_a 99
thread_b 99
thread_c: exit after 100
thread_a: exit after 100
thread_b: exit after 100
thread_schedule: no runnable threads
$
```

输出来自三个测试线程，每个线程都循环打印一行，再将 CPU 让给其他线程。目前由于没有上下文切换代码，你不会看到任何输出。

你需要补充 `user/uthread.c` 中的 `thread_create()`、`thread_schedule()`，以及 `user/uthread_switch.S` 中的 `thread_switch`。目标之一是让 `thread_schedule()` 首次运行某个线程时，该线程在自己的栈上执行传给 `thread_create()` 的函数。另一个目标是让 `thread_switch` 保存被切出线程的寄存器、恢复被切入线程的寄存器，并返回后者上次停止执行的位置。

你需要决定在哪里保存和恢复寄存器；修改 `struct thread` 来存放寄存器是一个好方案。还需在 `thread_schedule` 中调用 `thread_switch`。你可以向它传递所需的参数，但目的应是从线程 `t` 切换到 `next_thread`。

提示：

- `thread_switch` 只需保存和恢复调用约定中由被调用者负责保存的寄存器（callee-save registers）。为什么？
- 可在 `user/uthread.asm` 中查看 `uthread` 的汇编代码，可能有助于调试。
- 测试时可以使用 `riscv64-linux-gnu-gdb` 单步执行 `thread_switch`。例如：

  ```text
  (gdb) file user/_uthread
  Reading symbols from user/_uthread...
  (gdb) b uthread.c:60
  ```

  这会在 `uthread.c` 第 60 行设置断点。断点可能在运行 `uthread` 之前就触发，也可能不会。为什么？

  xv6 shell 运行后输入 `uthread`，GDB 会在第 60 行中断。可用以下命令查看 `uthread` 的状态：

  ```text
  (gdb) p/x *next_thread
  ```

  使用 `x` 查看内存位置的内容：

  ```text
  (gdb) x/x next_thread->stack
  ```

  可以运行到 `thread_switch` 的开头：

  ```text
  (gdb) b thread_switch
  (gdb) c
  ```

  使用 `(gdb) si` 单步执行汇编指令。GDB 在线文档见[这里](https://sourceware.org/gdb/current/onlinedocs/gdb/)。

## 使用线程（中等）

本练习使用哈希表探索线程和锁的并行编程。请在有多个核心的真实 Linux 或 macOS 电脑上完成，而不是在 xv6 或 QEMU 中完成。大多数近年的笔记本电脑都有多核处理器。

本练习使用 UNIX `pthread` 线程库。可通过 `man pthreads` 阅读手册，也可查看 [`pthread_mutex_lock`](https://pubs.opengroup.org/onlinepubs/007908799/xsh/pthread_mutex_lock.html)、[`pthread_mutex_init`](https://pubs.opengroup.org/onlinepubs/007908799/xsh/pthread_mutex_init.html) 和 [`pthread_create`](https://pubs.opengroup.org/onlinepubs/007908799/xsh/pthread_create.html) 的在线文档。

`notxv6/ph.c` 包含一个简单哈希表，单线程使用时正确，多线程使用时则不正确。在 xv6 主目录（例如 `~/xv6-labs-2021`）运行：

```sh
make ph
./ph 1
```

Makefile 构建 `ph` 时使用操作系统自带的 GCC，而不是 6.S081 工具链。`ph` 的参数指定对哈希表执行 put 和 get 操作的线程数。运行一小段时间后，`ph 1` 的输出类似：

```text
100000 puts, 3.991 seconds, 25056 puts/second
0: 0 keys missing
100000 gets, 3.981 seconds, 25118 gets/second
```

根据电脑速度、核心数量以及是否忙于其他任务，你看到的数字可能与示例相差两倍甚至更多。

`ph` 运行两项基准测试：先调用 `put()` 向哈希表添加大量键，并打印每秒 put 次数；再通过 `get()` 获取键，打印本应由 put 加入但实际缺失的键数（此处为零）以及每秒 get 次数。

传入大于 1 的参数可让多个线程同时使用哈希表。试试 `ph 2`：

```text
$ ./ph 2
100000 puts, 1.885 seconds, 53044 puts/second
1: 16579 keys missing
0: 16579 keys missing
200000 gets, 4.322 seconds, 46274 gets/second
```

第一行表示两个线程并发添加条目时，总共达到每秒 53,044 次插入，大约是 `ph 1` 单线程速度的两倍。这是约 2 倍的理想并行加速：核心数加倍，单位时间内完成的工作也加倍。

但两行 `16579 keys missing` 表示许多原本应由 put 加入哈希表的键缺失了。请查看 `notxv6/ph.c`，尤其是 `put()` 和 `insert()`。

为什么两个线程会丢失键，而一个线程不会？指出两个线程中可能导致键丢失的一系列事件，并将这个事件序列及简短解释写入 `answers-thread.txt`。

为了避免这种事件，在 `notxv6/ph.c` 的 `put` 和 `get` 中加入加锁与解锁，使两个线程运行时缺失键的数量始终为 0。相关 pthread 调用如下：

```c
pthread_mutex_t lock;            // 声明锁
pthread_mutex_init(&lock, NULL); // 初始化锁
pthread_mutex_lock(&lock);       // 获取锁
pthread_mutex_unlock(&lock);     // 释放锁
```

当 `make grade` 报告通过 `ph_safe` 测试时，本阶段完成；该测试要求两个线程运行时没有缺失的键。此时未通过 `ph_fast` 测试也没关系。

别忘了调用 `pthread_mutex_init()`。先用一个线程测试，再用两个线程测试。结果是否正确，即是否消除了缺失的键？双线程版本相对于单线程版本是否实现了并行加速，也就是单位时间内完成更多总工作量？

有些并发的 `put()` 操作读写的哈希表内存互不重叠，因此无需用锁保护彼此。能否修改 `ph.c`，利用这些情况让某些 `put()` 获得并行加速？提示：每个哈希桶使用一把锁如何？

修改代码，使部分 `put` 操作能并行运行，同时保持正确性。当 `make grade` 报告 `ph_safe` 和 `ph_fast` 均通过时完成。`ph_fast` 要求两个线程每秒完成的 put 数量至少是一个线程的 1.25 倍。

## 屏障（中等）

本练习实现一个[屏障（barrier）](http://en.wikipedia.org/wiki/Barrier_(computer_science))：程序中的一个位置，所有参与线程都必须在此等待，直到其他所有参与线程也到达。你将使用 pthread 条件变量，这是一种与 xv6 的 sleep 和 wakeup 类似的执行顺序协调机制。

请在真实电脑上完成本练习，不要在 xv6 或 QEMU 中完成。

`notxv6/barrier.c` 中包含一个有缺陷的屏障：

```text
$ make barrier
$ ./barrier 2
barrier: notxv6/barrier.c:42: thread: Assertion `i == t' failed.
```

参数 2 指定在屏障处同步的线程数（`barrier.c` 中的 `nthread`）。每个线程执行一个循环，每次迭代调用 `barrier()`，然后睡眠随机数量的微秒。断言触发的原因是一个线程在另一个线程到达屏障之前就离开了。期望的行为是：每个线程都在 `barrier()` 中阻塞，直到全部 `nthreads` 个线程都调用了 `barrier()`。

你的目标是实现这一行为。除了 `ph` 练习中见过的锁操作外，还需要以下 pthread 操作；详情参见 [`pthread_cond_wait`](https://pubs.opengroup.org/onlinepubs/007908799/xsh/pthread_cond_wait.html) 和 [`pthread_cond_broadcast`](https://pubs.opengroup.org/onlinepubs/007908799/xsh/pthread_cond_broadcast.html)：

```c
pthread_cond_wait(&cond, &mutex); // 等待 cond，释放 mutex；唤醒后重新获取
pthread_cond_broadcast(&cond);    // 唤醒所有等待 cond 的线程
```

确保解答通过 `make grade` 的 `barrier` 测试。调用 `pthread_cond_wait` 时它会释放 `mutex`，并在返回前重新获取该锁。

我们已提供 `barrier_init()`。你的任务是实现 `barrier()`，使测试中的断言失败不再发生。我们也定义了 `struct barrier`，其中的字段可供使用。

有两个问题使任务更复杂：

- 需要处理连续多次屏障调用，每次称为一轮。`bstate.round` 记录当前轮次；每当所有线程到达屏障，应将它递增。
- 必须处理某个线程在其他线程离开屏障前就快速进入下一次循环的情况。尤其需要注意，`bstate.nthread` 会在不同轮次复用。要确保提前离开屏障的线程不会在上一轮仍在使用该计数时，将它误当作下一轮的计数并增加。

分别使用一个、两个和两个以上线程测试代码。

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



## Uthread 的可选挑战

用户级线程库与操作系统之间存在多种不理想的交互。例如，如果一个用户级线程在系统调用中阻塞，其他用户级线程也不会运行，因为用户级线程调度器不知道其中一个线程已被 xv6 调度器换出。又如，两个用户级线程不会在不同核心上并发运行，因为 xv6 调度器不知道有多个线程可以并行运行。注意，如果两个用户级线程真正并行运行，当前实现也会因多个竞态而无法工作：例如，不同处理器上的两个线程可能同时调用 `thread_schedule`，选择同一个可运行线程，并在两个处理器上同时运行它。

解决方法有多种，包括[调度器激活（scheduler activations）](http://en.wikipedia.org/wiki/Scheduler_activations)，或为每个用户级线程使用一个内核线程（Linux 内核采用后者）。在 xv6 中实现其中一种方法并不容易；例如，更新多线程用户进程的页表时，需要实现 TLB shootdown。

> 译注：TLB shootdown 指通知其他相关核心，使它们缓存的旧地址转换失效，避免页表更新后仍使用过期映射。

为线程库添加锁、条件变量、屏障等功能。

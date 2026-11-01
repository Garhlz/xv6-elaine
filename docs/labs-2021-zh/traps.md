# 实验：陷阱

本实验探讨如何使用陷阱（trap）实现系统调用。你将首先通过栈相关练习热身，然后实现一个用户态陷阱处理示例。

开始编码前，请阅读 [xv6 教材](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)第 4 章和相关源文件：

- `kernel/trampoline.S`：负责用户空间与内核空间之间切换的汇编代码。
- `kernel/trap.c`：处理所有中断的代码。

开始实验时，切换到 `traps` 分支：

```sh
git fetch
git checkout traps
make clean
```

## RISC-V 汇编（简单）

理解一些在 6.004 中接触过的 RISC-V 汇编很重要。xv6 仓库中有文件 `user/call.c`；`make fs.img` 会编译它，并在 `user/call.asm` 中生成可读的汇编版本。

阅读 `call.asm` 中函数 `g`、`f` 和 `main` 的代码。RISC-V 指令手册位于[参考页面](https://pdos.csail.mit.edu/6.828/2021/reference.html)。请回答以下问题，并将答案保存到 `answers-traps.txt`：

- 哪些寄存器存放函数参数？例如，`main` 调用 `printf` 时，哪个寄存器存放 13？
- `main` 的汇编代码中，调用 `f` 的指令在哪里？调用 `g` 的指令在哪里？提示：编译器可能会内联函数。
- 函数 `printf` 位于哪个地址？
- `main` 中执行跳转到 `printf` 的 `jalr` 指令后，寄存器 `ra` 的值是多少？

运行以下代码：

```c
unsigned int i = 0x00646c72;
printf("H%x Wo%s", 57616, &i);
```

输出是什么？[ASCII 表](http://web.cs.mun.ca/~michael/c/ascii-table.html)列出了字节与字符的对应关系。

输出依赖 RISC-V 采用小端序。假如 RISC-V 使用大端序，要得到相同输出，应将 `i` 设为什么值？是否需要将 `57616` 改为其他值？参见[大小端序说明](http://www.webopedia.com/TERM/b/big_endian.html)和[另一篇比较有趣的说明](http://www.networksorcery.com/enp/ien/ien137.txt)。

下面代码中，`'y='` 后面会打印什么？注意答案并不是一个确定的值。为什么会这样？

```c
printf("x=%d y=%d", 3);
```

## 栈回溯（中等）

调试时，栈回溯（backtrace）通常很有用：它列出错误发生位置之上调用栈中的函数调用。

在 `kernel/printf.c` 中实现 `backtrace()`。在 `sys_sleep` 中调用它，然后运行会调用 `sys_sleep` 的 `bttest`。输出应类似：

```text
backtrace:
0x0000000080002cda
0x0000000080002bb6
0x0000000080002898
```

运行 `bttest` 后退出 QEMU。地址可能略有不同；在终端运行 `addr2line -e kernel/kernel`（或 `riscv64-unknown-elf-addr2line -e kernel/kernel`），并按如下方式粘贴地址：

```text
$ addr2line -e kernel/kernel
0x0000000080002de2
0x0000000080002f4a
0x0000000080002bfc
Ctrl-D
```

应看到类似输出：

```text
kernel/sysproc.c:74
kernel/syscall.c:224
kernel/trap.c:85
```

编译器使用帧指针定位当前栈帧，并在栈帧中保存调用者的帧指针。`backtrace` 应沿保存的帧指针逐层向上遍历调用栈，打印每个栈帧保存的返回地址。

提示：

- 在 `kernel/defs.h` 中添加 `backtrace` 原型，以便在 `sys_sleep` 中调用。
- GCC 将当前执行函数的帧指针保存在寄存器 `s0`。在 `kernel/riscv.h` 中添加下列函数，并在 `backtrace` 中调用它读取当前帧指针。该函数使用[内联汇编](https://gcc.gnu.org/onlinedocs/gcc/Using-Assembly-Language-with-C.html)读取 `s0`：

  ```c
  static inline uint64
  r_fp()
  {
    uint64 x;
    asm volatile("mv %0, s0" : "=r" (x) );
    return x;
  }
  ```

- 这些[讲义](https://pdos.csail.mit.edu/6.828/2021/lec/l-riscv-slides.pdf)中有栈帧布局图。返回地址位于栈帧帧指针的固定偏移 -8 处，保存的帧指针位于固定偏移 -16 处。
- xv6 为每个内核栈分配一个页对齐的页面。可使用 `PGROUNDDOWN(fp)` 和 `PGROUNDUP(fp)`（见 `kernel/riscv.h`）计算栈页的底部和顶部地址，帮助 `backtrace` 确定循环何时结束。

回溯功能正常工作后，在 `kernel/printf.c` 的 `panic` 中调用它，以便内核 panic 时打印回溯。

## Alarm（困难）

本练习要求为 xv6 添加功能：进程消耗 CPU 时间时，周期性地通知该进程。这对想限制 CPU 时间消耗的计算密集型进程，或需要一边计算一边周期性执行其他操作的进程可能有用。更一般地说，你将实现一种基本形式的用户态中断／异常处理程序；例如，可以用类似方式在应用程序中处理缺页异常。如果通过 `alarmtest` 和 `usertests`，解答即为正确。

添加系统调用 `sigalarm(interval, handler)`。应用调用 `sigalarm(n, fn)` 后，每消耗 `n` 个 CPU 时间 tick，内核就应使应用函数 `fn` 被调用。`fn` 返回时，应用应从中断处继续执行。xv6 中的 tick 是一个较为任意的时间单位，由硬件定时器产生中断的频率决定。调用 `sigalarm(0, 0)` 时，内核应停止生成周期性 alarm 调用。

仓库中提供了 `user/alarmtest.c`，请将它加入 Makefile。在添加 `sigalarm` 和 `sigreturn` 系统调用之前，它无法正确编译。

`alarmtest` 的 `test0` 调用 `sigalarm(2, periodic)`，要求内核每两个 tick 强制调用一次 `periodic()`，然后循环一段时间。可以在 `user/alarmtest.asm` 中查看 `alarmtest` 的汇编代码，这可能有助于调试。当 `alarmtest` 输出如下内容且 `usertests` 也正确运行时，解答即为正确：

```text
$ alarmtest
test0 start
........alarm!
test0 passed
test1 start
...alarm!
..alarm!
...alarm!
..alarm!
...alarm!
..alarm!
...alarm!
..alarm!
...alarm!
..alarm!
test1 passed
test2 start
................alarm!
test2 passed
$ usertests
...
ALL TESTS PASSED
$
```

完成后的代码可能只有几行，但要写对并不容易。我们会使用原始仓库中的 `alarmtest.c` 测试。你可以修改该文件辅助调试，但要确保原版 `alarmtest` 报告所有测试通过。

### test0：调用处理函数

首先修改内核，使其跳转到用户空间的 alarm 处理函数，从而让 test0 打印 `alarm!`。暂时不用担心打印之后发生什么，即使程序随后崩溃也可以。提示如下：

- 修改 Makefile，使 `alarmtest.c` 编译成 xv6 用户程序。
- 在 `user/user.h` 中添加正确声明：

  ```c
  int sigalarm(int ticks, void (*handler)());
  int sigreturn(void);
  ```

- 更新 `user/usys.pl`（用于生成 `user/usys.S`）、`kernel/syscall.h` 和 `kernel/syscall.c`，使 `alarmtest` 能调用 `sigalarm` 和 `sigreturn`。
- 暂时让 `sys_sigreturn` 直接返回零。
- `sys_sigalarm()` 应将 alarm 间隔和处理函数指针存入 `proc` 结构体（`kernel/proc.h`）中新增的字段。
- 需要跟踪自上次调用某进程的 alarm 处理函数以来过去了多少 tick，或距离下次调用还剩多少 tick；为此还需要在 `struct proc` 中增加字段。可以在 `proc.c` 的 `allocproc()` 中初始化这些字段。
- 每个 tick，硬件时钟都会强制产生中断，由 `kernel/trap.c` 的 `usertrap()` 处理。
- 只有定时器中断时才需要操作进程的 alarm tick，可以使用类似 `if(which_dev == 2) ...` 的判断。
- 仅当进程设置了定时器时才调用 alarm 函数。注意用户 alarm 函数的地址可能是 0，例如 `user/alarmtest.asm` 中的 `periodic` 位于地址 0。
- 修改 `usertrap()`，使进程的 alarm 间隔到期后执行用户处理函数。RISC-V 上陷阱返回用户空间时，什么决定用户代码恢复执行的指令地址？
- 可让 QEMU 只使用一个 CPU，以便通过 GDB 查看陷阱：运行 `make CPUS=1 qemu-gdb`，再从另一个终端连接 GDB。
- 如果 `alarmtest` 打印了 `alarm!`，本阶段即告成功。

### test1/test2：恢复被中断的代码

`alarmtest` 可能在 test0 或 test1 打印 `alarm!` 后崩溃，也可能最终打印 `test1 failed`，或在未打印 `test1 passed` 时退出。为解决这些问题，必须确保 alarm 处理函数结束后，控制流回到用户程序最初被定时器中断的指令。还必须将寄存器恢复为中断发生时的值，使用户程序在 alarm 之后不受干扰地继续执行。最后，每次 alarm 触发后都应重新设置计数器，使处理函数周期性调用。

作为起点，我们已替你做出一个设计决定：用户 alarm 处理函数结束时必须调用 `sigreturn` 系统调用。`alarmtest.c` 中的 `periodic` 是一个例子。因此，你可以在 `usertrap` 和 `sys_sigreturn` 中添加相互配合的代码，使用户进程处理完 alarm 后正确恢复。

提示：

- 解答需要保存和恢复寄存器。要正确恢复被中断的代码，需要保存和恢复哪些寄存器？提示：数量会很多。
- 定时器触发时，让 `usertrap` 在 `struct proc` 中保存足够的状态，以便 `sigreturn` 正确返回被中断的用户代码。
- 防止处理函数重入：如果处理函数尚未返回，内核不应再次调用它。`test2` 会测试这一点。

通过 `test0`、`test1` 和 `test2` 后，运行 `usertests`，确保没有破坏内核的其他部分。

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

- 在 `backtrace()` 中打印函数名和行号，而不是数字地址。

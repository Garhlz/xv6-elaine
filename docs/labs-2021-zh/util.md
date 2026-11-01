# 实验：xv6 与 Unix 实用工具

本实验帮助你熟悉 xv6 及其系统调用。

## 启动 xv6（简单）

你可以在 Athena 机器或自己的电脑上完成实验。使用自己的电脑时，请参考[实验工具页面](https://pdos.csail.mit.edu/6.828/2021/tools.html)配置环境。

使用 Athena 时，必须使用 x86 机器，即 `uname -a` 的输出应包含 `i386 GNU/Linux`、`i686 GNU/Linux` 或 `x86_64 GNU/Linux`。可以用 `ssh -X athena.dialup.mit.edu` 登录公共 Athena 主机。课程已在 Athena 上配置编译器和模拟器，运行 `add -f 6.828` 即可使用。每次登录都要执行该命令，或者将它加入 `~/.environment`。如果编译或运行 `qemu` 时遇到难以理解的错误，请检查是否加载了课程 locker。

获取实验的 xv6 源码并切换到 `util` 分支：

```text
$ git clone git://g.csail.mit.edu/xv6-labs-2021
Cloning into 'xv6-labs-2021'...
...
$ cd xv6-labs-2021
$ git checkout util
Branch 'util' set up to track remote branch 'util' from 'origin'.
Switched to a new branch 'util'
```

`xv6-labs-2021` 仓库与教材中的 `xv6-riscv` 略有不同，主要是增加了一些文件。有兴趣的话可以通过 `git log` 查看历史。

本实验和后续实验所需的文件都使用 [Git](http://www.git-scm.com/) 版本控制系统分发。上面的 `git checkout util` 切换到了包含本实验专用 xv6 版本的分支。想了解 Git，可以阅读 [Git 用户手册](http://www.kernel.org/pub/software/scm/git/docs/user-manual.html)或[面向计算机科学学习者的 Git 概述](http://eagain.net/articles/git-for-computer-scientists/)。Git 可以跟踪代码的修改。例如，完成一个练习后，可以提交修改作为进度检查点：

```text
$ git commit -am 'my solution for util lab exercise 1'
Created commit 60d2135: my solution for util lab exercise 1
 1 files changed, 1 insertions(+), 0 deletions(-)
$
```

使用 `git diff` 查看上次提交以来的修改，使用 `git diff origin/util` 查看相对于远程 `util` 分支初始代码的修改。

> 译注：原文随后将 `origin/xv6-labs-2021` 称为课程初始代码所在的分支名；这一名称与前面的示例不一致，实际应以仓库中的分支名为准。

构建并运行 xv6：

```text
$ make qemu
riscv64-unknown-elf-gcc    -c -o kernel/entry.o kernel/entry.S
riscv64-unknown-elf-gcc -Wall -Werror -O -fno-omit-frame-pointer -ggdb -DSOL_UTIL -MD -mcmodel=medany -ffreestanding -fno-common -nostdlib -mno-relax -I. -fno-stack-protector -fno-pie -no-pie   -c -o kernel/start.o kernel/start.c
...
riscv64-unknown-elf-ld -z max-page-size=4096 -N -e main -Ttext 0 -o user/_zombie user/zombie.o user/ulib.o user/usys.o user/printf.o user/umalloc.o
riscv64-unknown-elf-objdump -S user/_zombie > user/zombie.asm
riscv64-unknown-elf-objdump -t user/_zombie | sed '1,/SYMBOL TABLE/d; s/ .* / /; /^$/d' > user/zombie.sym
mkfs/mkfs fs.img README  user/xargstest.sh user/_cat user/_echo user/_forktest user/_grep user/_init user/_kill user/_ln user/_ls user/_mkdir user/_rm user/_sh user/_stressfs user/_usertests user/_grind user/_wc user/_zombie
nmeta 46 (boot, super, log blocks 30 inode blocks 13, bitmap blocks 1) blocks 954 total 1000
balloc: first 591 blocks have been allocated
balloc: write bitmap block at sector 45
qemu-system-riscv64 -machine virt -bios none -kernel kernel/kernel -m 128M -smp 3 -nographic -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0

xv6 kernel is booting

hart 2 starting
hart 1 starting
init: starting sh
$
```

在提示符下输入 `ls`，应看到类似输出：

```text
$ ls
.              1 1 1024
..             1 1 1024
README         2 2 2059
xargstest.sh   2 3 93
cat            2 4 24256
echo           2 5 23080
forktest       2 6 13272
grep           2 7 27560
init           2 8 23816
kill           2 9 23024
ln             2 10 22880
ls             2 11 26448
mkdir          2 12 23176
rm             2 13 23160
sh             2 14 41976
stressfs       2 15 24016
usertests      2 16 148456
grind          2 17 38144
wc             2 18 25344
zombie         2 19 22408
console        3 20 0
```

这些是 `mkfs` 放入初始文件系统的文件，其中大部分是可运行程序。你刚刚运行的 `ls` 就是其中之一。

xv6 没有 `ps` 命令，但按 `Ctrl-p` 时内核会打印每个进程的信息。现在试一下，你会看到两行，分别对应 `init` 和 `sh`。退出 QEMU 时，先按 `Ctrl-a`，再按 `x`。

## 评分与提交

可以运行 `make grade`，用评分程序测试你的解答。助教会使用同一个评分程序给提交的实验评分。此外，课程还会安排实验检查会议，参见[评分政策](https://pdos.csail.mit.edu/6.828/2021/general.html#grading)。

实验代码提供了用于简化提交的 GNU Make 规则。提交最终修改后，运行 `make handin` 提交实验；详细说明见[提交实验](#提交实验)。

## sleep（简单）

为 xv6 实现 UNIX 程序 `sleep`。程序应暂停用户指定的 tick 数。tick 是 xv6 内核定义的时间单位，即时钟芯片两次中断之间的时间。将解答写在 `user/sleep.c` 中。

提示：

- 编码前阅读 [xv6 教材](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)第 1 章。
- 阅读 `user/` 下其他程序，例如 `user/echo.c`、`user/grep.c` 和 `user/rm.c`，了解如何获取命令行参数。
- 用户忘记提供参数时，`sleep` 应打印错误信息。
- 命令行参数以字符串传入，可用 `atoi` 转换成整数（见 `user/ulib.c`）。
- 使用 `sleep` 系统调用。
- 查看 `kernel/sysproc.c` 中实现该系统调用的 `sys_sleep`，`user/user.h` 中用户程序可调用的 C 声明，以及 `user/usys.S` 中从用户代码进入内核的汇编代码。
- 确保 `main` 调用 `exit()` 退出程序。
- 将 `sleep` 加入 Makefile 中的 `UPROGS`。之后 `make qemu` 会编译它，你可以从 xv6 shell 运行。
- 可以阅读 Kernighan 和 Ritchie 的《The C Programming Language（第二版）》（K&R）学习 C。

从 xv6 shell 运行：

```text
$ make qemu
...
init: starting sh
$ sleep 10
（短暂等待）
$
```

如果程序如上暂停，即为正确。运行 `make grade` 检查是否通过 sleep 测试。

`make grade` 会运行全部测试，包括下面尚未完成的练习。要只运行与 `sleep` 匹配的测试，可以运行：

```sh
./grade-lab-util sleep
```

或者运行等价命令：

```sh
make GRADEFLAGS=sleep grade
```

## pingpong（简单）

编写程序，使用 UNIX 系统调用，通过一对管道在两个进程之间来回传递一个字节，每个方向使用一个管道。父进程向子进程发送一个字节；子进程打印“`<pid>: received ping`”（`<pid>` 为子进程 ID），通过管道将字节写回父进程，然后退出；父进程读取这个字节，打印“`<pid>: received pong`”，然后退出。将解答写在 `user/pingpong.c` 中。

提示：

- 使用 `pipe` 创建管道，使用 `fork` 创建子进程。
- 使用 `read` 从管道读取，使用 `write` 向管道写入。
- 使用 `getpid` 获取调用进程的 ID。
- 将程序加入 Makefile 的 `UPROGS`。
- xv6 用户程序只能使用有限的库函数，列表见 `user/user.h`；除系统调用外，源码位于 `user/ulib.c`、`user/printf.c` 和 `user/umalloc.c`。

运行时应输出：

```text
$ make qemu
...
init: starting sh
$ pingpong
4: received ping
3: received pong
$
```

如果程序在两个进程间交换一个字节并产生上述输出，即为正确。

## primes（中等／困难）

使用管道编写并发版素数筛。这个想法来自 Unix 管道的发明者 Doug McIlroy。[此页面](http://swtch.com/~rsc/thread/)中间的图片及其上下文介绍了实现方法。将解答写在 `user/primes.c` 中。

使用 `pipe` 和 `fork` 建立流水线。第一个进程将 2 到 35 输入流水线。对于每个素数，创建一个进程，通过管道从左邻居读取，再通过另一个管道写给右邻居。由于 xv6 的文件描述符和进程数量有限，第一个进程处理到 35 即可停止。

提示：

- 注意关闭进程不需要的文件描述符，否则在第一个进程处理到 35 之前，xv6 的资源可能耗尽。
- 第一个进程处理完 35 后，应等待整条流水线结束，包括所有子进程、孙进程等。因此主 `primes` 进程必须在全部输出打印完毕、所有其他 `primes` 进程退出后才能退出。
- 管道写端关闭后，`read` 返回零。
- 直接向管道写入 32 位（4 字节）`int`，比使用格式化 ASCII 输入输出更简单。
- 只在需要时创建流水线中的进程。
- 将程序加入 Makefile 的 `UPROGS`。

正确的管道筛应输出：

```text
$ make qemu
...
init: starting sh
$ primes
prime 2
prime 3
prime 5
prime 7
prime 11
prime 13
prime 17
prime 19
prime 23
prime 29
prime 31
$
```

## find（中等）

编写简化版 UNIX `find`，查找目录树中具有指定名称的所有文件。将解答写在 `user/find.c` 中。

提示：

- 查看 `user/ls.c`，了解如何读取目录。
- 使用递归进入子目录，但不要递归进入 `.` 和 `..`。
- 文件系统的修改会在多次运行 QEMU 之间保留；如需全新文件系统，先运行 `make clean`，再运行 `make qemu`。
- 需要使用 C 字符串，可以参考 K&R 第 5.5 节。
- 注意，C 中的 `==` 不像 Python 那样比较字符串内容，应使用 `strcmp()`。
- 将程序加入 Makefile 的 `UPROGS`。

文件系统中存在 `b` 和 `a/b` 时，正确输出如下：

```text
$ make qemu
...
init: starting sh
$ echo > b
$ mkdir a
$ echo > a/b
$ find . b
./b
./a/b
$
```

## xargs（中等）

编写简化版 UNIX `xargs`：从标准输入逐行读取，每读到一行就运行一次命令，并将该行作为命令参数。将解答写在 `user/xargs.c` 中。

下面的例子展示其行为：

```text
$ echo hello too | xargs echo bye
bye hello too
$
```

这里的命令是 `echo bye`，额外参数是 `hello too`，组合成 `echo bye hello too`，因此输出 `bye hello too`。

UNIX 中的 `xargs` 通常会将多个输入项合并到同一次命令调用中，以减少启动进程的次数。本实验不要求实现这一优化。要让 UNIX `xargs` 按本实验期望的方式运行，即每次调用只处理一个输入项，请使用 `-n 1`，例如：

```text
$ echo "1\n2" | xargs -n 1 echo line
line 1
line 2
$
```

提示：

- 使用 `fork` 和 `exec` 对每一行输入调用命令；父进程使用 `wait` 等待子进程完成。
- 每次读取一个字符，直到遇到换行符（`'\n'`），即可逐行读取输入。
- `kernel/param.h` 声明了 `MAXARG`，声明 `argv` 数组时可能有用。
- 将程序加入 Makefile 的 `UPROGS`。
- 文件系统修改在 QEMU 多次运行之间保留；要获得全新文件系统，运行 `make clean`，然后运行 `make qemu`。

`xargs`、`find` 和 `grep` 可以很好地组合：

```sh
find . b | xargs grep hello
```

该命令会对 `.` 下每个名为 `b` 的文件运行 `grep hello`。

使用 shell 脚本 `xargstest.sh` 测试解答。正确输出应为：

```text
$ make qemu
...
init: starting sh
$ sh < xargstest.sh
$ $ $ $ $ $ hello
hello
hello
$ $
```

你可能需要回头修复 `find` 中的错误。输出中有许多 `$`，因为 xv6 shell 没有意识到它正在处理来自文件而非控制台的命令，会为文件中的每条命令打印一个 `$`。

## 提交实验

**至此实验完成。** 确保通过 `make grade` 的全部测试。如有问题，别忘了将答案写入 `answers-实验名.txt`（将“实验名”替换为本实验标识）。提交修改（包括答案文件），然后在实验目录中运行 `make handin`。

### 耗费时间

创建 `time.txt`，其中只写一个整数，表示实验耗费的小时数。别忘了执行 `git add` 和 `git commit`。

### 提交

通过[提交网站](https://6828.scripts.mit.edu/2021/handin.py/)提交作业；首次提交前需要在网站上申请 API 密钥。提交最终修改后运行 `make handin`：

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

`make handin` 会将密钥保存在 `myapi.key`。如需更换，删除该文件并让 `make handin` 重新生成；文件不能包含换行符。

如果存在未提交的修改或未跟踪文件，运行 `make handin` 时会看到类似输出：

```text
 M hello.c
?? bar.c
?? foo.pyc
Untracked files will not be handed in.  Continue? [y/N]
```

检查输出，确保解答所需的文件均已被跟踪，即不在以 `??` 开头的行中。可使用 `git add filename` 跟踪新文件。

如果 `make handin` 无法正常运行，尝试修复 curl 或 Git 命令的问题；也可以运行 `make tarball`，将生成的 tar 文件通过[网页界面](https://6828.scripts.mit.edu/2021/handin.py/)上传。

- 请运行 `make grade`，确保代码通过全部测试。
- 运行 `make handin` 前提交修改过的源代码。
- 可在[提交网站](https://6828.scripts.mit.edu/2021/handin.py/)查看提交状态并下载提交的代码。

## 可选挑战

- 编写 `uptime` 程序，使用 `uptime` 系统调用以 tick 为单位打印系统运行时间。
- 让 `find` 的名称匹配支持正则表达式。`grep.c` 有一些基本的正则表达式支持。
- xv6 shell（`user/sh.c`）只是另一个用户程序，你可以改进它。这个极简 shell 缺少真实 shell 的许多功能。例如，可以让 shell 在处理文件中的命令时不打印 `$`；支持 `wait`；支持使用 `;` 分隔的命令列表；通过实现 `(` 和 `)` 支持子 shell；支持 Tab 补全；保存历史命令；或者实现你希望 shell 具备的其他功能。如果目标很大，可能需要修改内核以提供所需功能，因为 xv6 支持的功能并不多。

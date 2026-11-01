# 实验：系统调用

上一个实验中，你使用系统调用编写了几个实用程序。本实验将在 xv6 中添加新的系统调用，帮助你理解系统调用的工作方式，并接触 xv6 内核的一些内部机制。后续实验还会添加更多系统调用。

开始编码前，请阅读 [xv6 教材](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev1.pdf)第 2 章、第 4 章的 4.3 和 4.4 节，以及以下相关源文件：

- 系统调用的用户态代码位于 `user/user.h` 和 `user/usys.pl`。
- 内核态代码位于 `kernel/syscall.h` 和 `kernel/syscall.c`。
- 与进程相关的代码位于 `kernel/proc.h` 和 `kernel/proc.c`。

开始实验时，切换到 `syscall` 分支：

```sh
git fetch
git checkout syscall
make clean
```

如果运行 `make grade`，会发现评分脚本无法执行 `trace` 和 `sysinfotest`。你的任务是添加必要的系统调用和存根，使它们能够运行。

## 系统调用跟踪（中等）

在本练习中，你将添加系统调用跟踪功能，它可能有助于调试后续实验。你需要创建一个新的 `trace` 系统调用来控制跟踪。它接受一个整数参数“掩码”（mask），其中各位指定需要跟踪的系统调用。例如，要跟踪 `fork` 系统调用，程序调用 `trace(1 << SYS_fork)`，其中 `SYS_fork` 是 `kernel/syscall.h` 中的系统调用号。

修改 xv6 内核：每当某个系统调用即将返回，如果掩码中与该系统调用号对应的位为 1，就打印一行信息。该行应包含进程 ID、系统调用名称和返回值，无须打印参数。`trace` 系统调用应为调用它的进程及其随后通过 `fork` 创建的子进程启用跟踪，但不应影响其他进程。

我们提供了用户程序 `trace`，它会启用跟踪并运行另一个程序（见 `user/trace.c`）。完成后，输出应类似：

```text
$ trace 32 grep hello README
3: syscall read -> 1023
3: syscall read -> 966
3: syscall read -> 70
3: syscall read -> 0
$
$ trace 2147483647 grep hello README
4: syscall trace -> 0
4: syscall exec -> 3
4: syscall open -> 3
4: syscall read -> 1023
4: syscall read -> 966
4: syscall read -> 70
4: syscall read -> 0
4: syscall close -> 0
$
$ grep hello README
$
$ trace 2 usertests forkforkfork
usertests starting
test forkforkfork: 407: syscall fork -> 408
408: syscall fork -> 409
409: syscall fork -> 410
410: syscall fork -> 411
409: syscall fork -> 412
410: syscall fork -> 413
409: syscall fork -> 414
411: syscall fork -> 415
...
$
```

第一个例子中，`trace` 调用 `grep`，只跟踪 `read` 系统调用；32 等于 `1<<SYS_read`。第二个例子跟踪所有系统调用，2147483647 的低 31 位均为 1。第三个例子没有启用跟踪，因此没有跟踪输出。第四个例子跟踪 `usertests` 中 `forkforkfork` 测试的所有后代进程的 `fork` 系统调用。如果程序行为如上所示，即为正确，进程 ID 可以不同。

提示：

- 将 `$U/_trace` 加入 Makefile 中的 `UPROGS`。
- 运行 `make qemu`，编译器会因为尚无该系统调用的用户态存根而无法编译 `user/trace.c`。在 `user/user.h` 中添加函数原型，在 `user/usys.pl` 中添加存根，并在 `kernel/syscall.h` 中添加系统调用号。Makefile 会调用 Perl 脚本 `user/usys.pl`，生成包含实际系统调用存根的 `user/usys.S`；这些存根使用 RISC-V `ecall` 指令进入内核。解决编译问题后，运行 `trace 32 grep hello README`；它仍会失败，因为你还没有在内核中实现该系统调用。
- 在 `kernel/sysproc.c` 中添加 `sys_trace()`，将其参数保存在 `proc` 结构体（见 `kernel/proc.h`）中新增加的变量中。用于从用户空间获取系统调用参数的函数位于 `kernel/syscall.c`，可参考 `kernel/sysproc.c` 中的用法。
- 修改 `fork()`（见 `kernel/proc.c`），将父进程的跟踪掩码复制到子进程。
- 修改 `kernel/syscall.c` 中的 `syscall()`，打印跟踪信息。你需要添加一个可按系统调用号索引的名称数组。

## Sysinfo（中等）

在本练习中，你将添加系统调用 `sysinfo`，收集正在运行的系统的信息。该调用接受一个参数：指向 `struct sysinfo` 的指针（见 `kernel/sysinfo.h`）。内核应填写该结构体：将 `freemem` 设为空闲内存字节数，将 `nproc` 设为 `state` 不为 `UNUSED` 的进程数。我们提供测试程序 `sysinfotest`；如果它打印 `sysinfotest: OK`，即通过本练习。

提示：

- 将 `$U/_sysinfotest` 加入 Makefile 中的 `UPROGS`。
- 运行 `make qemu`，此时 `user/sysinfotest.c` 无法编译。按照上一个练习的步骤添加 `sysinfo` 系统调用。要在 `user/user.h` 中声明 `sysinfo()` 原型，需先声明 `struct sysinfo` 的存在：

  ```c
  struct sysinfo;
  int sysinfo(struct sysinfo *);
  ```

  解决编译问题后运行 `sysinfotest`；它仍会失败，因为你还没有在内核中实现该系统调用。
- `sysinfo` 需要将 `struct sysinfo` 复制回用户空间；参见 `sys_fstat()`（`kernel/sysfile.c`）和 `filestat()`（`kernel/file.c`）中使用 `copyout()` 的例子。
- 在 `kernel/kalloc.c` 中添加函数，统计空闲内存大小。
- 在 `kernel/proc.c` 中添加函数，统计进程数量。

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

- 打印被跟踪的系统调用的参数。
- 计算系统平均负载，并通过 `sysinfo` 提供该信息。

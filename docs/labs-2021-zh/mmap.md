# 实验：mmap（困难）

`mmap` 和 `munmap` 系统调用使 UNIX 程序能够细致地控制自己的地址空间。它们可用于在进程间共享内存、将文件映射到进程地址空间，也可用于用户级缺页处理机制，例如课堂讨论的垃圾收集算法。本实验将在 xv6 中添加 `mmap` 和 `munmap`，重点是内存映射文件。

获取实验源码并切换到 `mmap` 分支：

```sh
git fetch
git checkout mmap
make clean
```

手册页（运行 `man 2 mmap`）给出的 `mmap` 声明如下：

```c
void *mmap(void *addr, size_t length, int prot, int flags,
           int fd, off_t offset);
```

`mmap` 的调用方式很多，但本实验只要求实现与文件映射相关的一部分功能。可以假设 `addr` 始终为零，表示由内核决定映射文件的虚拟地址。成功时 `mmap` 返回该地址，失败时返回 `0xffffffffffffffff`。`length` 是要映射的字节数，可能与文件长度不同。

`prot` 表示内存映射是否可读、可写和／或可执行；可以假设它为 `PROT_READ`、`PROT_WRITE` 或两者的组合。`flags` 为 `MAP_SHARED`（对映射内存的修改应写回文件）或 `MAP_PRIVATE`（修改不应写回），不要求实现其他标志位。`fd` 是要映射文件的已打开文件描述符。可以假设 `offset` 为零；它表示文件中映射的起始位置。

映射同一个 `MAP_SHARED` 文件的进程**不共享物理页**也可以。

`munmap(addr, length)` 应移除指定地址范围内的 mmap 映射。如果进程修改了内存且映射为 `MAP_SHARED`，应先将修改写入文件。一次 `munmap` 可能只覆盖映射区域的一部分，但可以假设它只会取消区域开头、末尾或整个区域的映射，不会在中间挖洞。

你应实现足够的 `mmap` 和 `munmap` 功能，使 `mmaptest` 测试程序能够工作。若 `mmaptest` 没有使用某项 `mmap` 功能，就不必实现该功能。

完成后，应看到以下输出：

```text
$ mmaptest
mmap_test starting
test mmap f
test mmap f: OK
test mmap private
test mmap private: OK
test mmap read-only
test mmap read-only: OK
test mmap read/write
test mmap read/write: OK
test mmap dirty
test mmap dirty: OK
test not-mapped unmap
test not-mapped unmap: OK
test mmap two files
test mmap two files: OK
mmap_test: ALL OK
fork_test starting
fork_test OK
mmaptest: all tests succeeded
$ usertests
usertests starting
...
ALL TESTS PASSED
$
```

提示：

- 首先将 `_mmaptest` 加入 `UPROGS`，并添加 `mmap` 和 `munmap` 系统调用，使 `user/mmaptest.c` 能编译。暂时让两个系统调用直接返回错误。`PROT_READ` 等常量已在 `kernel/fcntl.h` 中定义。运行 `mmaptest`，它会在第一次调用 mmap 时失败。
- 响应缺页异常，惰性地填充页表。也就是说，`mmap` 不应分配物理内存或读取文件；应像惰性页面分配实验一样，在 `usertrap` 中或其调用的缺页处理代码中完成。惰性处理可以确保映射大文件时速度快，并使映射大于物理内存的文件成为可能。
- 跟踪每个进程通过 `mmap` 建立的映射。定义一个对应第 15 讲中 VMA（虚拟内存区域）的结构体，记录 `mmap` 创建的虚拟地址范围的地址、长度、权限、文件等。由于 xv6 内核没有通用的内核对象分配器，可以声明固定大小的 VMA 数组，按需使用其中的空闲项；16 个条目应已足够。

  > 译注：原文称 xv6 内核没有“内存分配器”，这里是指没有按任意大小分配内核对象的通用分配器；xv6 仍有按页分配物理内存的 `kalloc()`。
- 实现 `mmap`：在进程地址空间中找到一个未使用的区域来映射文件，并在进程映射区域表中添加 VMA。VMA 应包含指向映射文件的 `struct file` 的指针；`mmap` 应增加文件引用计数，防止关闭文件后该结构体消失（提示：查看 `filedup`）。运行 `mmaptest`，第一次 `mmap` 应成功，但第一次访问映射内存会触发缺页异常并终止 `mmaptest`。
- 添加代码：映射区域发生缺页异常时，分配一个物理页，将相关文件的 4096 字节读入该页，再把它映射到用户地址空间。使用 `readi` 读取文件，它接受文件读取偏移量；传给 `readi` 的 inode 需要先加锁、随后解锁。别忘了正确设置页面权限。运行 `mmaptest`，应能执行到第一次 `munmap`。
- 实现 `munmap`：找到地址范围对应的 VMA，取消指定页面的映射（提示：使用 `uvmunmap`）。如果 `munmap` 移除了某次 `mmap` 的所有页面，应减少对应 `struct file` 的引用计数。如果被取消映射的页面已修改且文件使用 `MAP_SHARED` 映射，应将页面写回文件。可参考 `filewrite`。
- 理想情况下，只有程序实际修改过的 `MAP_SHARED` 页面才会写回。RISC-V PTE 的脏位（`D`）表示页面是否被写入。不过，`mmaptest` 不检查未修改的页面是否被写回，因此可以不检查 `D` 位而直接写回页面。
- 修改 `exit`，像调用了 `munmap` 一样取消进程的映射区域。运行 `mmaptest`，`mmap_test` 应通过，但 `fork_test` 可能还不能通过。
- 修改 `fork`，确保子进程拥有与父进程相同的映射区域。别忘了增加 VMA 对应 `struct file` 的引用计数。在子进程的缺页处理程序中，可以分配新物理页，而不与父进程共享页面。共享会更有意思，但需要更多实现工作。运行 `mmaptest`，`mmap_test` 和 `fork_test` 都应通过。

运行 `usertests`，确保其他功能仍然正常。

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

- 如果两个进程映射同一文件（如 `fork_test`），让它们共享物理页。你需要为物理页维护引用计数。
- 你的实现可能为从映射文件读取的每个页面分配新物理页，尽管数据也已经存在于内核缓冲区缓存中。修改实现以使用缓存中的物理内存，而不是分配新页面。这要求文件块与页面大小相同（将 `BSIZE` 设置为 4096）。你需要将映射的块固定在缓冲区缓存中，还需要处理引用计数。
- 消除惰性分配实现与映射文件实现之间的重复。提示：为惰性分配区域创建 VMA。
- 修改 `exec`，为二进制文件的不同段使用 VMA，从而实现按需分页的可执行文件。这会加快程序启动，因为 `exec` 无须从文件系统读取任何数据。
- 实现页面换出和换入：物理内存不足时，让内核将进程的一部分内容移到磁盘；进程引用被换出的内存时再将其换入。

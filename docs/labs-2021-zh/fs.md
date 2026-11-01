# 实验：文件系统

本实验将在 xv6 文件系统中增加大文件和符号链接支持。

编码前，请阅读 [xv6 教材](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)的“第 8 章：文件系统”，并研究对应代码。

获取实验的 xv6 源码并切换到 `fs` 分支：

> 译注：原文此处的文字误写为 `util`，下方命令和本实验实际使用的分支均为 `fs`。

```sh
git fetch
git checkout fs
make clean
```

## 大文件（中等）

本练习要求增加 xv6 文件的最大长度。目前 xv6 文件最多包含 268 个块，即 268×`BSIZE` 字节（xv6 中 `BSIZE` 为 1024）。这一限制源于 xv6 inode 包含 12 个“直接”块号和一个“一级间接”块号：后者指向一个最多存放 256 个块号的块，因此总计 12+256=268 个块。

`bigfile` 命令会创建尽可能大的文件并报告其大小：

```text
$ bigfile
..
wrote 268 blocks
bigfile: file is too small
$
```

测试失败，是因为 `bigfile` 期望能够创建包含 65803 个块的文件，而未修改的 xv6 将文件限制为 268 个块。

你需要修改 xv6 文件系统代码，使每个 inode 支持一个“二级间接”块。该块包含 256 个一级间接块的地址，而每个一级间接块最多包含 256 个数据块地址。最终文件最多可包含 65803 个块，即 256×256+256+11 个块；直接块从 12 个减少到 11 个，因为需要牺牲一个直接块号来存放二级间接块号。

### 准备工作

`mkfs` 程序创建 xv6 文件系统磁盘镜像，并确定文件系统的总块数；该大小由 `kernel/param.h` 的 `FSSIZE` 控制。本实验仓库中 `FSSIZE` 设为 200000 个块。make 输出中应能看到 `mkfs/mkfs` 打印：

```text
nmeta 70 (boot, super, log blocks 30 inode blocks 13, bitmap blocks 25) blocks 199930 total 200000
```

该行描述 `mkfs/mkfs` 创建的文件系统：70 个元数据块（用于描述文件系统的块）和 199930 个数据块，总计 200000 个块。

如果实验中需要从头重新构建文件系统，可以运行 `make clean`，迫使 make 重新构建 `fs.img`。

### 需要阅读的代码

磁盘 inode 的格式由 `fs.h` 中的 `struct dinode` 定义。请特别关注 `NDIRECT`、`NINDIRECT`、`MAXFILE` 和 `struct dinode` 的 `addrs[]` 字段。xv6 教材图 8.3 展示了标准 xv6 inode 的结构。

在磁盘上查找文件数据的代码位于 `fs.c` 的 `bmap()`。请阅读并确保理解它的功能。读取和写入文件时都会调用 `bmap()`；写入时，它根据需要分配新的数据块，以及用于保存块地址的间接块。

`bmap()` 处理两类块号：参数 `bn` 是“逻辑块号”，即相对于文件开头的文件内块号；`ip->addrs[]` 中的块号和 `bread()` 的参数则是磁盘块号。可以将 `bmap()` 理解为把文件逻辑块号映射为磁盘块号。

### 你的任务

修改 `bmap()`，使其除了支持直接块和一级间接块外，还支持二级间接块。为了腾出空间，直接块只能保留 11 个，不能改变磁盘 inode 的大小。`ip->addrs[]` 的前 11 项应为直接块，第 12 项为一级间接块（与现有结构一样），第 13 项为新增的二级间接块。

当 `bigfile` 写入 65803 个块且 `usertests` 运行成功时，练习完成：

```text
$ bigfile
..................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................
wrote 65803 blocks
done; ok
$ usertests
...
ALL TESTS PASSED
$
```

`bigfile` 至少需要运行一分半钟。

提示：

- 确保理解 `bmap()`。画图表示 `ip->addrs[]`、间接块、二级间接块及其指向的一级间接块、数据块之间的关系。确保理解为什么增加二级间接块能将最大文件大小增加 256×256 个块；严格说要减 1，因为直接块数量减少了一个。
- 思考如何使用逻辑块号索引二级间接块及其指向的一级间接块。
- 如果改变 `NDIRECT`，可能还需修改 `file.h` 中 `struct inode` 的 `addrs[]` 声明。确保 `struct inode` 与 `struct dinode` 的 `addrs[]` 数组元素数量相同。
- 如果改变 `NDIRECT`，务必重新创建 `fs.img`，因为 `mkfs` 构建文件系统时会使用 `NDIRECT`。
- 如果文件系统因崩溃等原因进入错误状态，课程原文建议从宿主机的 Unix 环境中删除 `fs.img`，而不是在 xv6 内删除；之后 `make` 会重新构建干净的文件系统镜像。此操作会丢失镜像内的数据，执行前应确认不再需要这些数据。
- 对每个通过 `bread()` 读取的块，别忘了调用 `brelse()`。
- 与原有 `bmap()` 一样，应仅在需要时分配一级和二级间接块。
- 确保 `itrunc` 释放文件的所有块，包括二级间接块。

## 符号链接（中等）

本练习将为 xv6 添加符号链接。符号链接（或软链接）通过路径名引用目标文件；打开符号链接时，内核沿链接找到目标文件。符号链接与硬链接类似，但硬链接只能指向同一磁盘上的文件，而符号链接可以跨磁盘设备。尽管 xv6 不支持多个设备，实现该系统调用仍是理解路径查找过程的好练习。

### 你的任务

实现 `symlink(char *target, char *path)` 系统调用，在 `path` 创建一个指向 `target` 所命名文件的新符号链接。更多信息可查看 `symlink` 手册页。测试时，将 `symlinktest` 加入 Makefile 并运行。测试应产生以下输出，且 `usertests` 也应成功：

```text
$ symlinktest
Start: test symlinks
test symlinks: ok
Start: test concurrent symlinks
test concurrent symlinks: ok
$ usertests
...
ALL TESTS PASSED
$
```

提示：

- 首先为 `symlink` 创建新的系统调用号，在 `user/usys.pl`、`user/user.h` 中添加条目，并在 `kernel/sysfile.c` 中实现空的 `sys_symlink`。
- 在 `kernel/stat.h` 中添加新文件类型 `T_SYMLINK`，表示符号链接。
- 在 `kernel/fcntl.h` 中添加可用于 `open` 的新标志 `O_NOFOLLOW`。传给 `open` 的标志通过按位或组合，因此新标志不能与现有标志重叠。将 `user/symlinktest.c` 加入 Makefile 后，这些定义应使它能够编译。
- 实现 `symlink(target, path)`，在 `path` 创建指向 `target` 的新符号链接。调用成功不要求 `target` 已存在。需要选择位置保存链接目标路径，例如 inode 的数据块。`symlink` 应像 `link` 和 `unlink` 一样返回整数表示成功（0）或失败（-1）。
- 修改 `open`，处理路径指向符号链接的情况。如果目标文件不存在，`open` 必须失败。如果进程在 `open` 标志中指定 `O_NOFOLLOW`，应打开符号链接本身，而不是跟随链接。
- 如果目标文件也是符号链接，必须递归跟随，直到到达非链接文件。如果链接形成环，必须返回错误码。可使用近似方法：链接深度达到某个阈值（例如 10）时返回错误。
- 其他系统调用（例如 `link` 和 `unlink`）不能跟随符号链接，它们应操作符号链接本身。
- 本实验不要求处理指向目录的符号链接。

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

支持三级间接块。

### 致谢

感谢华盛顿大学 CSEP551（2019 年秋季）课程团队提供符号链接练习。

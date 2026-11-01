# 实验：网络

本实验要求为网络接口卡（NIC）编写 xv6 设备驱动。

获取实验源码并切换到 `net` 分支：

```sh
git fetch
git checkout net
make clean
```

## 背景

编码前，可以复习 [xv6 教材](https://pdos.csail.mit.edu/6.828/2021/xv6/book-riscv-rev2.pdf)的“第 5 章：中断和设备驱动”。

你将使用名为 E1000 的网络设备处理网络通信。对 xv6 和你编写的驱动而言，E1000 看起来像一块连接到真实以太网局域网（LAN）的硬件。实际上，驱动通信的 E1000 由 QEMU 模拟，它连接的局域网也由 QEMU 模拟。在该模拟局域网中，xv6（“客户机”）的 IP 地址为 `10.0.2.15`，运行 QEMU 的电脑则以 `10.0.2.2` 出现在局域网中。当 xv6 使用 E1000 向 `10.0.2.2` 发送数据包时，QEMU 会将数据包交给运行 QEMU 的真实电脑（“宿主机”）上对应的应用程序。

你将使用 QEMU 的“用户态网络栈”，详见 [QEMU 文档](https://wiki.qemu.org/Documentation/Networking#User_Networking_.28SLIRP.29)。我们已更新 Makefile，启用 QEMU 用户态网络栈和 E1000 网卡。

Makefile 将 QEMU 配置为把所有进出数据包记录到实验目录中的 `packets.pcap`。查看这些记录有助于确认 xv6 是否按预期发送和接收数据包。显示记录的方法：

```sh
tcpdump -XXnr packets.pcap
```

我们为本实验向 xv6 仓库添加了一些文件：`kernel/e1000.c` 包含 E1000 初始化代码，以及需要你补充的空发送和接收函数。`kernel/e1000_dev.h` 包含 E1000 定义的寄存器和标志位，其含义参见 Intel E1000 [软件开发手册](https://pdos.csail.mit.edu/6.828/2021/readings/8254x_GBe_SDM.pdf)。`kernel/net.c` 和 `kernel/net.h` 包含一个实现 [IP](https://en.wikipedia.org/wiki/Internet_Protocol)、[UDP](https://en.wikipedia.org/wiki/User_Datagram_Protocol) 和 [ARP](https://en.wikipedia.org/wiki/Address_Resolution_Protocol) 的简单网络栈，也包含名为 `mbuf` 的灵活数据结构，用于存放数据包。最后，`kernel/pci.c` 包含 xv6 启动时在 PCI 总线上搜索 E1000 网卡的代码。

## 你的任务（困难）

完成 `kernel/e1000.c` 中的 `e1000_transmit()` 和 `e1000_recv()`，使驱动能够发送和接收数据包。当 `make grade` 报告通过全部测试时，实验完成。

编写代码时需要参考 E1000 [软件开发手册](https://pdos.csail.mit.edu/6.828/2021/readings/8254x_GBe_SDM.pdf)。以下章节尤其有帮助：

- 第 2 章提供整个设备的概览，十分重要。
- 第 3.2 节概述数据包接收。
- 第 3.3 节以及第 3.4 节概述数据包发送。
- 第 13 章概述 E1000 使用的寄存器。
- 第 14 章可能有助于理解我们提供的初始化代码。

浏览手册。它涵盖数种关系密切的以太网控制器，QEMU 模拟的是 82540EM。先快速阅读第 2 章，了解设备的大致情况。编写驱动时需要熟悉第 3 章、第 14 章和第 4.1 节（但不包括 4.1 的子节），还要将第 13 章用作参考。其他章节主要介绍驱动无需交互的 E1000 组件。起初不必纠结细节，先熟悉文档结构，以便之后查找。E1000 有许多高级功能，大部分可以忽略；完成本实验只需一小组基本功能。

我们在 `e1000.c` 中提供的 `e1000_init()` 会配置 E1000，使其从 RAM 读取待发送数据包，并将收到的数据包写入 RAM。这称为 DMA（直接内存访问），因为 E1000 硬件会直接读写 RAM 中的数据包。

突发数据包的到达速度可能超过驱动的处理速度，因此 `e1000_init()` 为 E1000 提供多个可写入数据包的缓冲区。E1000 要求 RAM 中的“描述符”数组描述这些缓冲区；每个描述符包含一个 RAM 地址，E1000 可将接收的数据包写到那里。`struct rx_desc` 定义了接收描述符的格式。这个数组称为接收环或接收队列：网卡或驱动到达数组末尾后，会从开头继续处理。`e1000_init()` 使用 `mbufalloc()` 分配 `mbuf` 数据包缓冲区，供 E1000 通过 DMA 写入。还有一个发送环，驱动通过其中的描述符提交希望 E1000 发送的数据包。`e1000_init()` 分别将接收环和发送环的大小配置为 `RX_RING_SIZE` 和 `TX_RING_SIZE`。

`net.c` 中的网络栈需要发送数据包时，会调用 `e1000_transmit()`，并传入保存待发送数据包的 mbuf。你的发送代码必须将数据包数据指针放入 TX（发送）环的描述符。`struct tx_desc` 描述该格式。必须确保每个 mbuf 最终得到释放，但只能在 E1000 完成发送之后释放；E1000 会设置描述符中的 `E1000_TXD_STAT_DD` 位表示完成。

E1000 从以太网接收数据包时，先通过 DMA 将数据包写入 RX（接收）环中下一个描述符所指向的 mbuf，然后产生中断。你的 `e1000_recv()` 必须扫描 RX 环，并通过调用 `net_rx()` 将每个新数据包的 mbuf 交给 `net.c` 中的网络栈。之后需要分配一个新 mbuf 并放入描述符，使 E1000 再次到达 RX 环该位置时，有新的缓冲区可供 DMA 写入数据包。

除了读写 RAM 中的描述符环，驱动还需要通过内存映射控制寄存器与 E1000 交互：一方面检测是否有收到的数据包，另一方面通知 E1000：驱动已在 TX 描述符中填入待发送的数据包。全局变量 `regs` 保存指向 E1000 第一个控制寄存器的指针；驱动可将 `regs` 当作数组，通过索引访问其他寄存器，尤其需要使用 `E1000_RDT` 和 `E1000_TDT`。

测试驱动时，在一个窗口运行 `make server`，另一个窗口运行 `make qemu`，然后在 xv6 中运行 `nettests`。`nettests` 的第一个测试会尝试向宿主操作系统发送 UDP 包，目标是 `make server` 启动的程序。如果实验尚未完成，E1000 驱动实际上不会发送数据包，也就不会有什么反应。

完成实验后，驱动会发送数据包，QEMU 将其交给宿主机，`make server` 收到后发送响应，随后 E1000 驱动和 `nettests` 收到响应。不过，宿主机发送回复前，会先向 xv6 发送 ARP 请求包，查找它的 48 位以太网地址，并期望 xv6 返回 ARP 响应。完成 E1000 驱动后，`kernel/net.c` 会处理这一过程。如果一切顺利，`nettests` 会打印 `testing ping: OK`，`make server` 会打印 `a message from xv6!`。

`tcpdump -XXnr packets.pcap` 输出应以类似内容开头：

```text
reading from file packets.pcap, link-type EN10MB (Ethernet)
15:27:40.861988 IP 10.0.2.15.2000 > 10.0.2.2.25603: UDP, length 19
        0x0000:  ffff ffff ffff 5254 0012 3456 0800 4500  ......RT..4V..E.
        0x0010:  002f 0000 0000 6411 3eae 0a00 020f 0a00  ./....d.>.......
        0x0020:  0202 07d0 6403 001b 0000 6120 6d65 7373  ....d.....a.mess
        0x0030:  6167 6520 6672 6f6d 2078 7636 21         age.from.xv6!
15:27:40.862370 ARP, Request who-has 10.0.2.15 tell 10.0.2.2, length 28
        0x0000:  ffff ffff ffff 5255 0a00 0202 0806 0001  ......RU........
        0x0010:  0800 0604 0001 5255 0a00 0202 0a00 0202  ......RU........
        0x0020:  0000 0000 0000 0a00 020f                 ..........
15:27:40.862844 ARP, Reply 10.0.2.15 is-at 52:54:00:12:34:56, length 28
        0x0000:  ffff ffff ffff 5254 0012 3456 0806 0001  ......RT..4V....
        0x0010:  0800 0604 0002 5254 0012 3456 0a00 020f  ......RT..4V....
        0x0020:  5255 0a00 0202 0a00 0202                 RU........
15:27:40.863036 IP 10.0.2.2.25603 > 10.0.2.15.2000: UDP, length 17
        0x0000:  5254 0012 3456 5255 0a00 0202 0800 4500  RT..4VRU......E.
        0x0010:  002d 0000 0000 4011 62b0 0a00 0202 0a00  .-....@.b.......
        0x0020:  020f 6403 07d0 0019 3406 7468 6973 2069  ..d.....4.this.i
        0x0030:  7320 7468 6520 686f 7374 21              s.the.host!
```

实际输出会有所不同，但应包含 `ARP, Request`、`ARP, Reply`、`UDP`、`a.message.from.xv6` 和 `this.is.the.host` 这些字符串。

`nettests` 还会执行其他测试，最后通过真实互联网向 Google 的一个域名服务器发送 DNS 请求。确保代码通过全部测试，之后应看到：

```text
$ nettests
nettests running on port 25603
testing ping: OK
testing single-process pings: OK
testing multi-process pings: OK
testing DNS
DNS arecord for pdos.csail.mit.edu. is 128.52.129.126
DNS OK
all tests passed.
```

还应确保 `make grade` 同样认定解答通过。

## 提示

首先在 `e1000_transmit()` 和 `e1000_recv()` 中添加打印语句，运行 `make server` 并在 xv6 中运行 `nettests`。从打印信息中应能看到 `nettests` 触发了对 `e1000_transmit` 的调用。

实现 `e1000_transmit` 的提示：

- 首先读取 `E1000_TDT` 控制寄存器，获取应放置下一个待发送包的 TX 环描述符索引。
- 检查环是否溢出。如果 `E1000_TDT` 索引所指描述符没有设置 `E1000_TXD_STAT_DD`，说明 E1000 尚未完成该描述符对应的上一次发送请求，应返回错误。
- 否则，使用 `mbuffree()` 释放该描述符上一次发送的 mbuf（如果有）。
- 填写描述符。`m->head` 指向内存中的数据包内容，`m->len` 是包长度。设置必要的 cmd 标志（参见 E1000 手册第 3.3 节），并保存 mbuf 指针，供稍后释放。
- 最后将 `E1000_TDT` 加一并对 `TX_RING_SIZE` 取模，更新环位置。
- 如果成功将 mbuf 加入环，返回 0；失败时（例如没有可发送该 mbuf 的描述符）返回 -1，使调用者知道需要释放 mbuf。

实现 `e1000_recv` 的提示：

- 首先读取 `E1000_RDT` 控制寄存器，将其加一并对 `RX_RING_SIZE` 取模，得到下一个等待处理的接收包所在环索引（如果存在）。
- 检查描述符 `status` 中的 `E1000_RXD_STAT_DD` 位，判断是否有新包；如果没有则停止。
- 否则，将 mbuf 的 `m->len` 更新为描述符报告的长度，并通过 `net_rx()` 将 mbuf 交给网络栈。
- 使用 `mbufalloc()` 分配新的 mbuf，替换刚交给 `net_rx()` 的 mbuf。将其数据指针 `m->head` 写入描述符，并清零描述符状态位。
- 最后，将 `E1000_RDT` 寄存器更新为最后处理的环描述符索引。
- `e1000_init()` 使用 mbuf 初始化 RX 环；可以查看其实现并借鉴代码。
- 接收数据包的累计数量最终会超过环大小 16；确保代码能够处理这种情况。

需要使用锁，处理多个进程可能同时使用 E1000，或者内核线程使用 E1000 时发生中断的情况。

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

下面部分挑战的收益只能在真实的高性能硬件上测量或测试，也就是基于 x86 的电脑。

- 本实验的网络栈使用中断处理入站数据包，但不用于处理出站数据包。更复杂的策略是在软件中排队出站包，每次只向 NIC 提供有限数量，再依靠 TX 中断重新填充发送环。这样就可以为不同类型的出站流量设置优先级。
- 提供的网络代码只部分支持 ARP。实现完整的 [ARP 缓存](https://tools.ietf.org/html/rfc826)，并将其接入 `net_tx_eth()`。
- E1000 支持多个 RX 和 TX 环。配置 E1000 为每个核心提供一对环，并修改网络栈支持多个环。这可能提高网络吞吐量并减少锁竞争，但难以测试和测量。
- `sockrecvudp()` 使用单向链表查找目标 socket，效率较低。尝试改用哈希表和 RCU 提升性能，但严肃的实现将难以测试和测量。
- [ICMP](https://tools.ietf.org/html/rfc792) 可以通知网络流失败。检测这些通知，并通过 socket 系统调用接口将其作为错误向上传递。
- E1000 支持多种无状态硬件卸载功能，包括校验和计算、RSC 和 GRO。使用其中一种或多种提高网络栈吞吐量，但难以测试和测量。
- 本实验的网络栈容易发生接收活锁。结合课堂和阅读材料，设计并实现解决方案，但测试较困难。
- 为 xv6 实现 UDP 服务器。
- 实现最小 TCP 协议栈并下载网页。

如果你尝试挑战题，无论是否与网络有关，请告诉课程团队！

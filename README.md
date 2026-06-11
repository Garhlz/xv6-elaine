# xv6-labs-2021

这是一个基于 MIT 6.S081 / 6.828 2021 课程的 xv6-riscv 实验仓库，当前实验配置位于 `conf/lab.mk`，现在是 `LAB=net`。

## 目录说明

- `kernel/`：内核代码，包含进程、页表、文件系统、驱动和网络栈。
- `user/`：用户态程序与实验测试程序，例如 `nettests`。
- `mkfs/`：构建 `fs.img` 的宿主机工具。
- `conf/`：实验配置。
- 根目录脚本：
  - `grade-lab-net`
  - `gradelib.py`
  - `server.py`
  - `ping.py`

## 环境要求

需要以下工具在 `PATH` 中可用：

- `riscv64-linux-gnu-gcc` / `riscv64-linux-gnu-ld`
- `qemu-system-riscv64`
- `python3`

如果使用仓库里的 VS Code 配置，建议安装：

- `clangd`
- `clang-format`
- `ms-vscode.makefile-tools`

## 常用运行方式

### 启动 xv6

```bash
make qemu
```

### 启动并等待 GDB

```bash
make qemu-gdb
```

### 运行当前实验评分

```bash
make grade
```

### net lab 手工测试

先在一个终端启动宿主机 UDP 回显服务：

```bash
make server
```

再在另一个终端启动 xv6：

```bash
make qemu
```

进入 xv6 shell 后运行：

```text
nettests
```

也可以从宿主机发送一个测试包：

```bash
make ping
```

## 说明

- `make grade` 目前会调用 `grade-lab-net`。
- 生成文件如 `fs.img`、`kernel/kernel`、`*.o`、`*.asm`、`*.sym`、`packets.pcap` 不应当作为源码提交。
- 当前仓库已经补充了 `clangd` / `clang-format` / `compile_commands.json` 相关配置，适合继续做代码阅读和实验回顾。

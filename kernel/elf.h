// ELF (Executable and Linkable Format) 可执行文件格式定义。
//
// xv6 中 exec() 使用此格式加载用户程序：
//   1. 读取 elfhdr，校验 magic == ELF_MAGIC
//   2. 根据 phoff / phnum 遍历 proghdr 表
//   3. 对每个 type == ELF_PROG_LOAD 的段，通过 loadseg() 加载到页表
//   4. 将 entry 地址写入 epc，作为用户态起始 PC
//
// xv6 只使用 ELF 的子集：忽略 section header（shoff/shentsize/shnum/shstrndx），
// 只读取 program header 中的 LOAD 段。

#ifndef XV6_ELF_H
#define XV6_ELF_H

#include "types.h"

// ELF 魔数："\x7FELF" 的小端表示
#define ELF_MAGIC 0x464C457FU

// ELF 文件头 (file header)。
// exec() 首先读取此结构以验证文件类型并定位 program header 表。
struct elfhdr {
    uint magic;       // 必须等于 ELF_MAGIC
    uchar elf[12];    // ELF 标识字节（类别、数据编码、版本等）
    ushort type;      // 文件类型（ET_EXEC=可执行, ET_REL=可重定位等）
    ushort machine;   // 目标架构（RISC-V = 0xF3）
    uint version;     // ELF 版本
    uint64 entry;     // 入口虚拟地址——exec() 将其写入 epc，用户程序从此开始执行
    uint64 phoff;     // program header 表在文件中的偏移量
    uint64 shoff;     // section header 表偏移（xv6 忽略）
    uint flags;       // 处理器特定标志
    ushort ehsize;    // ELF 文件头自身的大小
    ushort phentsize; // 每个 program header 条目的大小
    ushort phnum;     // program header 表中的条目数
    ushort shentsize; // 每个 section header 条目的大小（xv6 忽略）
    ushort shnum;     // section header 条目数（xv6 忽略）
    ushort shstrndx;  // section 名称字符串表索引（xv6 忽略）
};

// Program header —— 描述一个需要加载到内存的段。
// exec() 逐段读取，只处理 type == ELF_PROG_LOAD 的段。
struct proghdr {
    uint32 type;   // 段类型，见下方 ELF_PROG_* 定义
    uint32 flags;  // 权限标志（R/W/X 组合），见 ELF_PROG_FLAG_*
    uint64 off;    // 段数据在文件中的偏移量
    uint64 vaddr;  // 段加载到的虚拟地址（必须页对齐）
    uint64 paddr;  // 物理地址（xv6 忽略，仅使用 vaddr）
    uint64 filesz; // 文件中该段数据的大小（字节）
    uint64 memsz;  // 内存中该段需要的大小（>= filesz，多余部分填零，即 BSS）
    uint64 align;  // 对齐要求（页对齐）
};

// Program header 类型
#define ELF_PROG_LOAD 1 // 可加载段——xv6 唯一处理的类型

// Program header flags —— 段的访问权限
#define ELF_PROG_FLAG_EXEC 1  // 可执行
#define ELF_PROG_FLAG_WRITE 2 // 可写
#define ELF_PROG_FLAG_READ 4  // 可读

#endif

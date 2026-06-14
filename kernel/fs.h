#ifndef XV6_FS_H
#define XV6_FS_H

#include "types.h"

// 磁盘文件系统格式 (on-disk file system format)，内核和用户程序共用此头文件。

#define ROOTINO 1  // 根目录的 inode 编号 (root i-number)
#define BSIZE 1024 // 磁盘块大小 (block size)，字节

// 磁盘布局 (on-disk layout):
// [ 启动块 (boot) | 超级块 (superblock) | 日志区 (log) | inode 区 | 空闲位图区 (bitmap) | 数据块区
// (data) ]
//
// mkfs 计算超级块并构建初始文件系统。
// 超级块 (superblock) 描述磁盘的整体布局参数:
struct superblock {
    uint magic;      // 魔数，必须等于 FSMAGIC
    uint size;       // 文件系统镜像大小（块数）
    uint nblocks;    // 数据块总数
    uint ninodes;    // inode 总数
    uint nlog;       // 日志块数
    uint logstart;   // 日志区起始块号
    uint inodestart; // inode 区起始块号
    uint bmapstart;  // 空闲位图区起始块号
};

#define FSMAGIC 0x10203040

// 文件数据块索引结构：NDIRECT 个直接块 (direct) + 1 个一级间接块 (indirect) + 1 个二级间接块
// (doubly-indirect)
#define NDIRECT 11
#define NINDIRECT (BSIZE / sizeof(uint))
#define NDOUBLY_INDIRECT (NINDIRECT * NINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NDOUBLY_INDIRECT)

// 磁盘上的 inode 结构 (dinode)
struct dinode {
    short type;              // 文件类型 (type): 0=空闲, T_FILE, T_DIR, T_DEVICE
    short major;             // 主设备号 (major)，仅 T_DEVICE 使用
    short minor;             // 次设备号 (minor)，仅 T_DEVICE 使用
    short nlink;             // 硬链接数 (nlink): 指向此 inode 的目录项数量
    uint size;               // 文件大小 (size)，字节
    uint addrs[NDIRECT + 2]; // 数据块地址数组 (addrs):
                             //   [0..10] 直接块地址
                             //   [11]    一级间接块地址
                             //   [12]    二级间接块地址
};

// 每个磁盘块可容纳的 inode 数量
#define IPB (BSIZE / sizeof(struct dinode))

// 包含第 i 个 inode 的块号
#define IBLOCK(i, sb) ((i) / IPB + sb.inodestart)

// 每个位图块可管理的块数（每块 1024 字节 × 8 位）
#define BPB (BSIZE * 8)

// 包含第 b 个数据块对应位图位的块号
#define BBLOCK(b, sb) ((b) / BPB + sb.bmapstart)

// 目录项 (dirent): 文件名 → inode 编号的映射，每个文件名最长 DIRSIZ 字符
#define DIRSIZ 14

struct dirent {
    ushort inum;
    char name[DIRSIZ];
};

#endif

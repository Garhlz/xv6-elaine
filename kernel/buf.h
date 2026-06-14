#ifndef XV6_BUF_H
#define XV6_BUF_H

#include "types.h"
#include "sleeplock.h"
#include "fs.h"

struct buf {
    int valid;             // 数据是否已从磁盘读取？（1 = 已加载到 data[]）
    int disk;              // 磁盘驱动是否正在操作此 buf？（1 = 磁盘持有）
    uint dev;              // 设备号
    uint blockno;          // 磁盘块号
    struct sleeplock lock; // 每个 buf 的睡眠锁，保护 data[] 内容
    uint refcnt;           // 引用计数，brelse() 递减，为 0 时进入 LRU 淘汰候选
    struct buf *prev;      // 所在哈希桶内的双向循环链表指针（兼 LRU 顺序）
    struct buf *next;
    uchar data[BSIZE]; // 磁盘块数据缓冲区（1024 字节）
    uint timestamp;    // 最后一次 brelse() 时的 ticks，用于 LRU 近似淘汰
};

#endif

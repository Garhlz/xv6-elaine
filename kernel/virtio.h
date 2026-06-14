#ifndef XV6_VIRTIO_H
#define XV6_VIRTIO_H

#include "types.h"

//
// virtio 设备定义 (device definitions)。
// 涵盖 virtio MMIO 接口和 virtio 描述符 (descriptor) 格式。
// 仅测试过 QEMU 环境，使用 "legacy" virtio 接口。
//
// Virtio 规范 (spec):
// https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf
//

// virtio MMIO 控制寄存器 (control registers)，内存映射基址: 0x10001000。
// 寄存器偏移定义来自 qemu virtio_mmio.h。
#define VIRTIO_MMIO_MAGIC_VALUE 0x000      // 魔数寄存器; 值应为 0x74726976
#define VIRTIO_MMIO_VERSION 0x004          // 版本号; legacy 接口版本为 1
#define VIRTIO_MMIO_DEVICE_ID 0x008        // 设备类型; 1=网卡, 2=磁盘
#define VIRTIO_MMIO_VENDOR_ID 0x00c        // 厂商 ID; QEMU 为 0x554d4551
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010  // 设备支持的功能位掩码 (读)
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020  // 驱动接受的功能位掩码 (写)
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028  // 页面大小 (仅写)
#define VIRTIO_MMIO_QUEUE_SEL 0x030        // 选择队列 (仅写)
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034    // 当前队列最大容量 (仅读)
#define VIRTIO_MMIO_QUEUE_NUM 0x038        // 设置队列容量 (仅写)
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03c      // used ring 对齐 (仅写)
#define VIRTIO_MMIO_QUEUE_PFN 0x040        // 队列内存的物理页号 (读/写)
#define VIRTIO_MMIO_QUEUE_READY 0x044      // 队列就绪标志位
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050     // 通知设备有新的 avail 条目 (仅写)
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060 // 中断状态 (仅读)
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064    // 中断确认 (仅写)
#define VIRTIO_MMIO_STATUS 0x070           // 设备状态 (读/写)

// 设备状态寄存器 (status register) 的位定义，来自 qemu virtio_config.h。
// 驱动通过逐步置位这些标志完成和设备的握手 (handshake)。
#define VIRTIO_CONFIG_S_ACKNOWLEDGE 1 // 已识别设备 (ACKNOWLEDGE)
#define VIRTIO_CONFIG_S_DRIVER 2      // 驱动已加载 (DRIVER)
#define VIRTIO_CONFIG_S_DRIVER_OK 4   // 驱动就绪 (DRIVER_OK)
#define VIRTIO_CONFIG_S_FEATURES_OK 8 // 功能协商完成 (FEATURES_OK)

// 设备功能位 (device feature bits)。
// 驱动通过清除不想要的功能位来和设备协商最终使用的功能集。
#define VIRTIO_BLK_F_RO 5              // 磁盘为只读 (Read Only)
#define VIRTIO_BLK_F_SCSI 7            // 支持 SCSI 命令透传
#define VIRTIO_BLK_F_CONFIG_WCE 11     // 配置空间支持写回缓存模式 (Writeback Cache Enable)
#define VIRTIO_BLK_F_MQ 12             // 支持多队列 (Multi-Queue)
#define VIRTIO_F_ANY_LAYOUT 27         // 描述符布局灵活
#define VIRTIO_RING_F_INDIRECT_DESC 28 // 支持间接描述符
#define VIRTIO_RING_F_EVENT_IDX 29     // 支持 event idx 事件抑制

// virtio 描述符总数。必须是 2 的幂。
// xv6 使用 8 个描述符——对于教学系统来说足够了（每个磁盘请求只需 3 个链式描述符）。
#define NUM 8

// 单个 virtio 描述符 (descriptor)，对应规范中的 virtq_desc。
// 多个描述符通过 flags 中的 VRING_DESC_F_NEXT 和 next 字段形成链 (chain)。
struct virtq_desc {
    uint64 addr;  // 数据缓冲区的物理地址
    uint32 len;   // 数据长度（字节）
    uint16 flags; // 标志位: VRING_DESC_F_NEXT / VRING_DESC_F_WRITE
    uint16 next;  // 链中下一个描述符的索引
};
#define VRING_DESC_F_NEXT 1  // 此描述符后还有下一个（形成描述符链）
#define VRING_DESC_F_WRITE 2 // 设备向此缓冲区写入（否则是设备从此缓冲区读取）

// avail ring (可用环): 驱动向设备提交待处理请求的环形队列。
// 驱动将"链头描述符索引"写入 ring[flags]，然后递增 idx 并通知设备。
struct virtq_avail {
    uint16 flags;     // 始终为 0
    uint16 idx;       // 驱动下次写入 ring[] 的位置（单调递增）
    uint16 ring[NUM]; // 待处理请求的链头描述符编号
    uint16 unused;    // 对齐填充
};

// used ring 中的单个条目 (used element)。
// 设备完成某个请求后，将链头描述符 id 和已处理字节数填入此结构。
struct virtq_used_elem {
    uint32 id;  // 已完成描述符链的链头索引
    uint32 len; // 设备实际处理/写入的字节数
};

// used ring (完成环): 设备通知驱动哪些请求已处理完毕的环形队列。
// 设备向 ring[idx % NUM] 写入 used_elem，然后递增 idx。
// 驱动通过比较 disk.used_idx 和 disk.used->idx 来检测新完成的请求。
struct virtq_used {
    uint16 flags; // 始终为 0
    uint16 idx;   // 设备新增 used 条目后递增此值
    struct virtq_used_elem ring[NUM];
};

// 以下定义专用于 virtio 块设备 (block device)，即磁盘。
// 参见 virtio 规范 Section 5.2。

#define VIRTIO_BLK_T_IN 0  // 读磁盘 (read)
#define VIRTIO_BLK_T_OUT 1 // 写磁盘 (write)

// 磁盘请求中第一个描述符的格式 (virtio_blk_req)。
// 紧随其后还有两个描述符: 第二个指向数据缓冲区 (b->data)，第三个指向
// 一字节的状态结果。三个描述符由 VRING_DESC_F_NEXT 串成链。
struct virtio_blk_req {
    uint32 type;     // 操作类型: VIRTIO_BLK_T_IN (读) 或 VIRTIO_BLK_T_OUT (写)
    uint32 reserved; // 保留字段，始终为 0
    uint64 sector;   // 起始扇区号 (LBA)，1 扇区 = 512 字节
};

#endif

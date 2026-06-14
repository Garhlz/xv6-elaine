//
// QEMU virtio 磁盘块设备的驱动 (driver for qemu's virtio disk device)。
// 使用 QEMU 提供的 virtio MMIO 接口，采用 "legacy" virtio 协议。
//
// QEMU 启动参数示例:
//   qemu ... -drive file=fs.img,if=none,format=raw,id=x0
//     -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "virtio.h"

// 访问 virtio MMIO 寄存器 r 的地址宏。
// VIRTIO0 是 MMIO 基址 (0x10001000)，寄存器偏移 r 来自 virtio.h。
// 使用 volatile 修饰避免编译器优化掉对 MMIO 的重复读写。
#define R(r) ((volatile uint32 *)(VIRTIO0 + (r)))

// 磁盘设备全局状态 (disk)。
// 包含 virtio 规范的三个内存区 (descriptor / avail / used) 和驱动的簿记数据。
// __attribute__((aligned(PGSIZE))) 确保 pages[] 物理地址页对齐。
static struct disk {
    // virtio 驱动和设备通过一组 RAM 中的共享数据结构来通信。
    // pages[] 是一个 2 页 (8 KiB) 的连续物理内存缓冲区，用作这些结构。
    // 使用静态数组而非 kalloc() 分配，因为 virtio 要求连续且页对齐的物理内存。
    char pages[2 * PGSIZE];

    // pages[] 被划分为三个区域: desc 区、avail ring 区、used ring 区。
    // 布局详见 virtio 规范 Section 2.6（legacy 接口）。
    // https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf

    // 区域 1: 描述符集合 (descriptor area)，不是环形队列，而是一个数组。
    // 驱动通过描述符告诉设备每次磁盘操作的 I/O 缓冲区位置和长度。
    // 大多数命令由若干描述符串成一条链 (chain)。共有 NUM 个描述符。
    // 此指针指向 pages[] 的起始位置。
    struct virtq_desc *desc;

    // 区域 2: 可用环 (avail ring)，一个环形队列。
    // 驱动将待处理的链头 (head descriptor) 编号写入此环，通知设备处理。
    // 共 NUM 个条目。此指针指向 pages[] 内 avail 区域的起始位置。
    struct virtq_avail *avail;

    // 区域 3: 完成环 (used ring)，一个环形队列。
    // 设备将已处理完成的链头描述符编号写入此环，通知驱动回收。
    // 共 NUM 个条目。此指针指向 pages[] 内 used 区域的起始位置。
    struct virtq_used *used;

    // — 以下为驱动私有簿记数据 —

    // free[i] == 1 表示第 i 号描述符空闲可用
    char free[NUM];
    // 驱动已消费到的 used ring 位置: disk.used_idx ~ disk.used->idx 之间是
    // 设备新完成的请求（由 virtio_disk_intr() 处理）
    uint16 used_idx;

    // 记录每个"在途" (in-flight) 磁盘操作的信息。
    // 以链头描述符索引 idx 为下标索引。中断到来时从中取出 struct buf 和状态。
    struct {
        struct buf *b; // 本次操作对应的缓冲区块指针
        char status;   // 设备写入的操作状态: 0=成功, 0xff=初始值
    } info[NUM];

    // 磁盘命令头数组。每个描述符链配一个 virtio_blk_req，方便索引。
    // ops[idx[0]] 和 desc[idx[0]] 一一对应。
    struct virtio_blk_req ops[NUM];

    // 保护整个 disk 结构的自旋锁。
    // 磁盘操作路径和中断处理路径都需要持有此锁。
    struct spinlock vdisk_lock;

} __attribute__((aligned(PGSIZE))) disk;

// 初始化 virtio 磁盘设备。
// 执行 virtio 规范定义的标准初始化序列:
//   1. 检查魔数、版本、设备 ID、厂商 ID
//   2. 逐步设置状态寄存器 (ACKNOWLEDGE → DRIVER → FEATURES_OK → DRIVER_OK)
//   3. 功能协商 (feature negotiation): 读取设备支持的功能位，清除不需要的位
//   4. 设置队列 0 (queue 0) 的大小和内存映射地址
//   5. 划分 pages[] 为 desc / avail / used 三个区域
//
// 初始化后，中断处理由 plic.c 和 trap.c 负责路由 (VIRTIO0_IRQ)。
void virtio_disk_init(void) {
    uint32 status = 0;

    initlock(&disk.vdisk_lock, "virtio_disk");

    // 检查设备身份: 魔数、legacy 版本 (1)、设备类型为磁盘 (2)、厂商为 QEMU
    if (*R(VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 || *R(VIRTIO_MMIO_VERSION) != 1 ||
        *R(VIRTIO_MMIO_DEVICE_ID) != 2 || *R(VIRTIO_MMIO_VENDOR_ID) != 0x554d4551) {
        panic("could not find virtio disk");
    }

    // 握手步骤 1: 驱动识别到设备存在
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    *R(VIRTIO_MMIO_STATUS) = status;

    // 握手步骤 2: 驱动已加载
    status |= VIRTIO_CONFIG_S_DRIVER;
    *R(VIRTIO_MMIO_STATUS) = status;

    // 功能协商 (feature negotiation): 读取设备支持的功能集，清除不需要的功能位。
    // xv6 是一个极简内核——几乎所有可选特性都不需要。
    uint64 features = *R(VIRTIO_MMIO_DEVICE_FEATURES);
    features &= ~(1 << VIRTIO_BLK_F_RO);             // 不需要只读磁盘
    features &= ~(1 << VIRTIO_BLK_F_SCSI);           // 不需要 SCSI 命令透传
    features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);     // 不配置写回缓存
    features &= ~(1 << VIRTIO_BLK_F_MQ);             // 不需要多队列
    features &= ~(1 << VIRTIO_F_ANY_LAYOUT);         // 不使用灵活布局
    features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);     // 不使用 event idx
    features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC); // 不使用间接描述符
    *R(VIRTIO_MMIO_DRIVER_FEATURES) = features;

    // 握手步骤 3: 功能协商完成
    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *R(VIRTIO_MMIO_STATUS) = status;

    // 握手步骤 4: 驱动完全就绪
    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R(VIRTIO_MMIO_STATUS) = status;

    // 告知设备 guest 页面大小
    *R(VIRTIO_MMIO_GUEST_PAGE_SIZE) = PGSIZE;

    // 初始化队列 0（virtio disk 只有一个队列）。
    *R(VIRTIO_MMIO_QUEUE_SEL) = 0;
    uint32 max = *R(VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max == 0)
        panic("virtio disk has no queue 0");
    if (max < NUM)
        panic("virtio disk max queue too short");
    *R(VIRTIO_MMIO_QUEUE_NUM) = NUM;
    memset(disk.pages, 0, sizeof(disk.pages));
    // QUEUE_PFN: 告知设备队列内存的物理页号
    *R(VIRTIO_MMIO_QUEUE_PFN) = ((uint64)disk.pages) >> PGSHIFT;

    // pages[] 布局 (8 KiB = 2 页):
    //   [0, PGSIZE)            → desc 区:    NUM 个 struct virtq_desc
    //   [NUM*sizeof(desc), PGSIZE) → avail ring: struct virtq_avail
    //   [PGSIZE, 2*PGSIZE)     → used ring:  struct virtq_used

    disk.desc = (struct virtq_desc *)disk.pages;
    disk.avail = (struct virtq_avail *)(disk.pages + NUM * sizeof(struct virtq_desc));
    disk.used = (struct virtq_used *)(disk.pages + PGSIZE);

    // 初始时所有 NUM 个描述符均为空闲
    for (int i = 0; i < NUM; i++)
        disk.free[i] = 1;
}

// 分配一个空闲描述符，标记为已使用，返回其索引。
// 如果没有空闲描述符则返回 -1。
static int alloc_desc() {
    for (int i = 0; i < NUM; i++) {
        if (disk.free[i]) {
            disk.free[i] = 0;
            return i;
        }
    }
    return -1;
}

// 释放一个描述符，标记为空闲。
// 清零描述符的所有字段（防御性），并唤醒可能在等待空闲描述符的线程。
static void free_desc(int desc_idx) {
    if (desc_idx >= NUM)
        panic("free_desc 1");
    if (disk.free[desc_idx])
        panic("free_desc 2");
    disk.desc[desc_idx].addr = 0;
    disk.desc[desc_idx].len = 0;
    disk.desc[desc_idx].flags = 0;
    disk.desc[desc_idx].next = 0;
    disk.free[desc_idx] = 1;
    wakeup(&disk.free[0]); // 唤醒在 alloc3_desc() 中等待空闲描述符的线程
}

// 释放整条描述符链 (chain)。
// 从 head_idx 开始，沿着 VRING_DESC_F_NEXT 和 next 字段遍历链中每个描述符。
// 在释放前先读取 flags 和 next——因为 free_desc() 会清零这些字段。
static void free_chain(int head_idx) {
    int desc_idx = head_idx;

    while (1) {
        int flags = disk.desc[desc_idx].flags;
        int next_idx = disk.desc[desc_idx].next;
        free_desc(desc_idx);
        if (flags & VRING_DESC_F_NEXT)
            desc_idx = next_idx; // 链中还有下一个描述符
        else
            break; // 链的末端
    }
}

// 分配三个描述符（不需要连续分配），索引存入 desc_idx[0..2]。
// 磁盘传输总是使用三描述符链:
//   desc_idx[0] — 命令头 (virtio_blk_req)
//   desc_idx[1] — 数据缓冲区 (b->data, BSIZE)
//   desc_idx[2] — 一字节状态结果
// 如果中途分配失败，回滚已分配的描述符并返回 -1。
static int alloc3_desc(int *desc_idx) {
    for (int slot = 0; slot < 3; slot++) {
        desc_idx[slot] = alloc_desc();
        if (desc_idx[slot] < 0) {
            // 分配失败——回滚前面已分配的描述符
            for (int rollback = 0; rollback < slot; rollback++)
                free_desc(desc_idx[rollback]);
            return -1;
        }
    }
    return 0;
}

// 执行磁盘读写操作 (virtio_disk_rw = read/write disk block via virtio)。
// 由 bio.c 中的 bread() / bwrite() 调用。
//
// is_write==0: 读磁盘块 (VIRTIO_BLK_T_IN)  — 设备将数据写入 buf->data
// is_write==1: 写磁盘块 (VIRTIO_BLK_T_OUT) — 设备从 buf->data 读取数据
//
// 每次操作构建一条 3 描述符链，提交给设备后睡眠等待中断完成。
// 整个函数持有 disk.vdisk_lock 自旋锁——磁盘操作被串行化。
void virtio_disk_rw(struct buf *buf, int is_write) {
    // 块号 (blockno) → 扇区号 (sector): BSIZE=1024B, 扇区=512B → 1 block = 2 sectors
    uint64 sector = buf->blockno * (BSIZE / 512);

    acquire(&disk.vdisk_lock);

    // virtio 规范 Section 5.2: legacy 块设备使用三条描述符组成的链:
    //   desc[0] — virtio_blk_req 命令头 (type + reserved + sector)
    //   desc[1] — 数据缓冲区 (buf->data, BSIZE bytes)
    //   desc[2] — 单字节状态结果

    // — 第 1 步: 分配三个空闲描述符 —
    int desc_idx[3];
    while (alloc3_desc(desc_idx) != 0) {
        // 暂时没有空闲描述符——等中断释放后再重试
        sleep(&disk.free[0], &disk.vdisk_lock);
    }

    // — 第 2 步: 填充三个描述符 —

    // 描述符 0: virtio_blk_req 命令头
    struct virtio_blk_req *req = &disk.ops[desc_idx[0]];
    req->type = is_write ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN;
    req->reserved = 0;
    req->sector = sector;

    disk.desc[desc_idx[0]].addr = (uint64)req;
    disk.desc[desc_idx[0]].len = sizeof(struct virtio_blk_req);
    disk.desc[desc_idx[0]].flags = VRING_DESC_F_NEXT;
    disk.desc[desc_idx[0]].next = desc_idx[1];

    // 描述符 1: 数据缓冲区 (buf->data, 1024 bytes)
    disk.desc[desc_idx[1]].addr = (uint64)buf->data;
    disk.desc[desc_idx[1]].len = BSIZE;
    // 设 write 标志位: 设备视角——写操作时设备读 buf, 读操作时设备写 buf
    if (is_write)
        disk.desc[desc_idx[1]].flags = 0; // 设备从此缓冲读
    else
        disk.desc[desc_idx[1]].flags = VRING_DESC_F_WRITE; // 设备向此缓冲写
    disk.desc[desc_idx[1]].flags |= VRING_DESC_F_NEXT;
    disk.desc[desc_idx[1]].next = desc_idx[2];

    // 描述符 2: 单字节状态 (0xff 初始值 → 设备成功时写为 0)
    disk.info[desc_idx[0]].status = 0xff;
    disk.desc[desc_idx[2]].addr = (uint64)&disk.info[desc_idx[0]].status;
    disk.desc[desc_idx[2]].len = 1;
    disk.desc[desc_idx[2]].flags = VRING_DESC_F_WRITE;
    disk.desc[desc_idx[2]].next = 0; // 链末端

    // — 第 3 步: 记录元数据，提交描述符链给设备 —
    buf->disk = 1;                  // 标记 buf 正被设备处理中
    disk.info[desc_idx[0]].b = buf; // 记下 buf 指针供中断处理使用

    disk.avail->ring[disk.avail->idx % NUM] = desc_idx[0];

    __sync_synchronize(); // 内存屏障: 确保描述符内容对设备可见

    disk.avail->idx += 1; // 告知设备有新条目

    __sync_synchronize(); // 内存屏障: 确保 idx 写入先于 QUEUE_NOTIFY

    *R(VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // 通知设备 (队列编号 0)

    // — 第 4 步: 睡眠等待设备完成 —
    // virtio_disk_intr() 完成后设 buf->disk=0 并 wakeup(buf)
    while (buf->disk == 1) {
        sleep(buf, &disk.vdisk_lock);
    }

    // — 第 5 步: 清理 —
    disk.info[desc_idx[0]].b = 0; // 解除引用
    free_chain(desc_idx[0]);      // 释放整条描述符链

    release(&disk.vdisk_lock);
}

// virtio 磁盘中断处理函数 (interrupt handler)。
// 由 trap.c 在接收到 VIRTIO0_IRQ 中断时调用。
//
// 处理 used ring 中所有已完成的请求:
//   对每个条目 → 检查状态 → 设 buf->disk=0 → wakeup(buf) → 推进 used_idx
void virtio_disk_intr() {
    acquire(&disk.vdisk_lock);

    // 中断确认 (interrupt acknowledge): 读中断状态并写回。
    // 设备在 ACK 之前不会发新中断，但可能已在 used ring 中累积了完成条目。
    // while 循环会一次处理完所有累积条目——下次中断可能无事可做。
    *R(VIRTIO_MMIO_INTERRUPT_ACK) = *R(VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

    __sync_synchronize();

    // disk.used->idx (设备写) 与 disk.used_idx (驱动读) 之间的差距 = 新完成的请求
    while (disk.used_idx != disk.used->idx) {
        __sync_synchronize(); // 确保读到设备最新写入的 used ring 条目
        uint16 ring_pos = disk.used_idx % NUM;
        int chain_head = disk.used->ring[ring_pos].id;

        // 状态字节为 0 表示成功
        if (disk.info[chain_head].status != 0)
            panic("virtio_disk_intr status");

        struct buf *buf = disk.info[chain_head].b;
        buf->disk = 0; // 通知 virtio_disk_rw() 操作完成
        wakeup(buf);   // 唤醒正在睡眠等待的 virtio_disk_rw()

        disk.used_idx += 1;
    }

    release(&disk.vdisk_lock);
}

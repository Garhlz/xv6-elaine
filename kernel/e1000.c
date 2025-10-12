#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "e1000_dev.h"
#include "net.h"

#define TX_RING_SIZE 16
static struct tx_desc tx_ring[TX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *tx_mbufs[TX_RING_SIZE];

#define RX_RING_SIZE 16
static struct rx_desc rx_ring[RX_RING_SIZE] __attribute__((aligned(16)));
static struct mbuf *rx_mbufs[RX_RING_SIZE];

#define TX_SW_QUEUE_SIZE 64
static struct mbuf *tx_sw_queue[TX_SW_QUEUE_SIZE];
static uint32 tx_sw_head = 0;
static uint32 tx_sw_tail = 0;
static struct spinlock tx_sw_queue_lock;

// remember where the e1000's registers live.
static volatile uint32 *regs;

// struct spinlock e1000_lock;
struct spinlock tx_lock;
struct spinlock rx_lock;

// called by pci_init().
// xregs is the memory address at which the
// e1000's registers are mapped.
void e1000_init(uint32 *xregs)
{
  int i;

  initlock(&tx_lock, "tx_lock");
  initlock(&rx_lock, "rx_lock");
  initlock(&tx_sw_queue_lock, "tx_sw_queue_lock");
  regs = xregs;

  // Reset the device
  regs[E1000_IMS] = 0; // disable interrupts
  regs[E1000_CTL] |= E1000_CTL_RST;
  regs[E1000_IMS] = 0; // redisable interrupts
  __sync_synchronize();

  // [E1000 14.5] Transmit initialization
  memset(tx_ring, 0, sizeof(tx_ring));
  for (i = 0; i < TX_RING_SIZE; i++)
  {
    tx_ring[i].status = E1000_TXD_STAT_DD;
    tx_mbufs[i] = 0;
  }
  regs[E1000_TDBAL] = (uint64)tx_ring;
  // 设置Transmit Descriptor Base Address Low
  if (sizeof(tx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_TDLEN] = sizeof(tx_ring);
  // 设置Transmit Descriptor Length
  regs[E1000_TDH] = regs[E1000_TDT] = 0;
  // 把网卡的“已处理到哪”的指针 (TDH, Head) 和我们的“下一个要放哪”的指针 (TDT, Tail) 都拨到 0 号位置。
  //  [E1000 14.4] Receive initialization
  memset(rx_ring, 0, sizeof(rx_ring));

  // 接受环初始化
  for (i = 0; i < RX_RING_SIZE; i++)
  {
    // 准备好空缓冲区
    rx_mbufs[i] = mbufalloc(0);
    if (!rx_mbufs[i])
      panic("e1000");
    rx_ring[i].addr = (uint64)rx_mbufs[i]->head;
  }
  regs[E1000_RDBAL] = (uint64)rx_ring;
  if (sizeof(rx_ring) % 128 != 0)
    panic("e1000");
  regs[E1000_RDH] = 0;
  regs[E1000_RDT] = RX_RING_SIZE - 1;
  regs[E1000_RDLEN] = sizeof(rx_ring);

  // filter by qemu's MAC address, 52:54:00:12:34:56
  regs[E1000_RA] = 0x12005452;
  regs[E1000_RA + 1] = 0x5634 | (1 << 31);
  // multicast table
  for (int i = 0; i < 4096 / 32; i++)
    regs[E1000_MTA + i] = 0;

  // transmitter control bits. 启用发送器
  regs[E1000_TCTL] = E1000_TCTL_EN |                 // enable
                     E1000_TCTL_PSP |                // pad short packets
                     (0x10 << E1000_TCTL_CT_SHIFT) | // collision stuff
                     (0x40 << E1000_TCTL_COLD_SHIFT);
  regs[E1000_TIPG] = 10 | (8 << 10) | (6 << 20); // inter-pkt gap

  // receiver control bits. 启用接收器
  regs[E1000_RCTL] = E1000_RCTL_EN |      // enable receiver
                     E1000_RCTL_BAM |     // enable broadcast
                     E1000_RCTL_SZ_2048 | // 2048-byte rx buffers
                     E1000_RCTL_SECRC;    // strip CRC

  // ask e1000 for receive interrupts. 开启接收中断
  regs[E1000_RDTR] = 0; // interrupt after every received packet (no timer)
  regs[E1000_RADV] = 0; // interrupt after every packet (no timer)
  // regs[E1000_IMS] = (1 << 7); // RXDW -- Receiver Descriptor Write Back
  // 同时开启接收中断 (RXT0) 和发送队列空中断 (TXQE)
  regs[E1000_IMS] = E1000_IMS_RXT0 | E1000_IMS_TXQE;
}

static void e1000_tx_kickstart();

int e1000_transmit(struct mbuf *m)
{
  acquire(&tx_sw_queue_lock);
  if ((tx_sw_tail + 1) % TX_SW_QUEUE_SIZE == tx_sw_head)
  {
    release(&tx_sw_queue_lock);
    return -1;
  }

  tx_sw_queue[tx_sw_tail] = m;
  tx_sw_tail = (tx_sw_tail + 1) % TX_SW_QUEUE_SIZE;

  e1000_tx_kickstart();

  release(&tx_sw_queue_lock);

  return 0;
}

static void
e1000_recv(void)
{
  //
  // Your code here.
  //
  // Check for packets that have arrived from the e1000
  // Create and deliver an mbuf for each packet (using net_rx()).
  //
  acquire(&rx_lock);

  for (;;)
  {
    // RDT 是驱动上次处理到的位置，所以新包应该在 RDT+1
    uint32 idx = (regs[E1000_RDT] + 1) % RX_RING_SIZE;

    // DD 表示网卡已经把数据放好了
    if (!(rx_ring[idx].status & E1000_RXD_STAT_DD))
    {
      // 如果 DD 位没有被设置，说明没有更多的新包了，退出循环
      break;
    }

    struct mbuf *m = rx_mbufs[idx];

    m->len = rx_ring[idx].length;

    net_rx(m);

    rx_mbufs[idx] = mbufalloc(0);

    if (!rx_mbufs[idx])
    {
      panic("e1000");
    }

    rx_ring[idx].addr = (uint64)rx_mbufs[idx]->head;
    rx_ring[idx].status = 0;

    regs[E1000_RDT] = idx;
  }
  release(&rx_lock);
}

static void
e1000_tx_kickstart()
{
  // acquire(&tx_sw_queue_lock) 应该由调用者持有

  // 循环地尝试从软件队列搬运包到硬件环
  while (tx_sw_head != tx_sw_tail)
  {
    acquire(&tx_lock);
    uint32 idx = regs[E1000_TDT];

    if (!(tx_ring[idx].status & E1000_TXD_STAT_DD))
    {
      // 硬件环满了，我们无能为力，只能等下一次中断再试
      release(&tx_lock);
      break;
    }

    // 释放上一个从这个描述符发送出去的 mbuf
    if (tx_mbufs[idx])
      mbuffree(tx_mbufs[idx]);

    // 从软件队列的头部取出一个 mbuf
    struct mbuf *m = tx_sw_queue[tx_sw_head];
    tx_sw_head = (tx_sw_head + 1) % TX_SW_QUEUE_SIZE;

    // 把它放到硬件环上
    tx_mbufs[idx] = m;
    tx_ring[idx].addr = (uint64)m->head;
    tx_ring[idx].length = m->len;
    tx_ring[idx].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS;

    regs[E1000_TDT] = (idx + 1) % TX_RING_SIZE;
    release(&tx_lock);
  }
}

void e1000_intr(void)
{
  // 读取中断原因寄存器
  uint32 status = regs[E1000_ICR];

  // 检查是否包含接收中断标志
  if (status & E1000_ICR_RXT0)
  {
    e1000_recv();
  }

  // 检查是否包含发送中断标志
  if (status & E1000_ICR_TXQE)
  {
    acquire(&tx_sw_queue_lock);
    e1000_tx_kickstart();
    release(&tx_sw_queue_lock);
  }

  // 清除我们已经处理过的中断标志
  regs[E1000_ICR] = status;
}
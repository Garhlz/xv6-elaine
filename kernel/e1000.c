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

// ----------------------我是一条可爱的分割线ovo----------------------

// --- Simple Pseudo-Random Number Generator (LCG) ---

// 我们的种子，需要一个初始值
static unsigned int next_seed = 1;

// 设置种子的函数 (可选，但良好实践)
void srand(unsigned int seed)
{
  next_seed = seed;
}

// 生成下一个伪随机数的函数
int rand(void)
{
  // 使用 glibc 中 rand() 的经典参数
  next_seed = next_seed * 1103515245 + 12345;
  // C 语言的 unsigned int 溢出会自动取模 (2^32)
  // 我们只取结果的一部分，让它分布更均匀
  return (unsigned int)(next_seed / 65536) % 32768;
}
// --- End of PRNG ---

#define TX_SW_QUEUE_SIZE 64
struct sw_queue
{
  struct spinlock lock;
  struct mbuf *mbufs[TX_SW_QUEUE_SIZE];
  uint32 count; // 当前队列中的元素数量
};

// 创建一个全局的队列实例
struct sw_queue tx_q;

// --- Heap Helper Functions ---

// 计算父/子节点的索引
#define HEAP_PARENT(i) (((i) - 1) / 2)
#define HEAP_LEFT(i) (2 * (i) + 1)
#define HEAP_RIGHT(i) (2 * (i) + 2)

// 交换堆中两个元素的位置
static void
heap_swap(int i, int j)
{
  struct mbuf *temp = tx_q.mbufs[i];
  tx_q.mbufs[i] = tx_q.mbufs[j];
  tx_q.mbufs[j] = temp;
}

// 上浮操作：将索引为 idx 的元素调整到正确位置
static void
sift_up(int idx)
{
  // 只要当前节点不是根节点，并且比它的父节点优先级高
  while (idx > 0 && tx_q.mbufs[idx]->priority > tx_q.mbufs[HEAP_PARENT(idx)]->priority)
  {
    // 就和父节点交换位置
    heap_swap(idx, HEAP_PARENT(idx));
    // 继续向上检查
    idx = HEAP_PARENT(idx);
  }
}

// 下沉操作：将索引为 idx 的元素调整到正确位置
static void
sift_down(int idx)
{
  int max_idx = idx;

  while (1)
  {
    int left_idx = HEAP_LEFT(idx);
    int right_idx = HEAP_RIGHT(idx);

    // 检查左子节点是否存在，且优先级更高
    if (left_idx < tx_q.count && tx_q.mbufs[left_idx]->priority > tx_q.mbufs[max_idx]->priority)
    {
      max_idx = left_idx;
    }
    // 检查右子节点是否存在，且优先级更高
    if (right_idx < tx_q.count && tx_q.mbufs[right_idx]->priority > tx_q.mbufs[max_idx]->priority)
    {
      max_idx = right_idx;
    }

    // 如果优先级最高的节点就是当前节点，说明调整完毕
    if (max_idx == idx)
    {
      break;
    }

    // 否则，和优先级更高的子节点交换，并继续向下检查
    heap_swap(idx, max_idx);
    idx = max_idx;
  }
}

// --- End of Heap Helper Functions ---

// 1. 初始化队列 (无需修改)
void tx_sw_queue_init(void)
{
  extern uint ticks;
  srand(ticks);
  initlock(&tx_q.lock, "tx_sw_queue");
  tx_q.count = 0;
  // 为保持清晰，明确将所有指针置为0
  for (int i = 0; i < TX_SW_QUEUE_SIZE; i++)
  {
    tx_q.mbufs[i] = 0;
  }
}

// 2. 入队操作 (使用堆的上浮逻辑)
int tx_sw_queue_enqueue(struct mbuf *m)
{
  acquire(&tx_q.lock);

  if (tx_q.count >= TX_SW_QUEUE_SIZE)
  {
    // 队列已满
    release(&tx_q.lock);
    return -1;
  }

  // 1. 将新元素放在堆的末尾
  tx_q.mbufs[tx_q.count] = m;

  // 2. 对新元素执行上浮操作，以维持堆的性质
  sift_up(tx_q.count);

  // 3. 元素数量加一
  tx_q.count++;

  release(&tx_q.lock);
  return 0;
}

// 3. 出队操作 (使用堆的下沉逻辑)
struct mbuf *
tx_sw_queue_dequeue(void)
{
  struct mbuf *m = 0;

  acquire(&tx_q.lock);

  if (tx_q.count == 0)
  {
    // 队列为空
    release(&tx_q.lock);
    return 0;
  }

  // 1. 堆顶元素永远是优先级最高的
  m = tx_q.mbufs[0];

  // 2. 将堆的最后一个元素挪到堆顶
  tx_q.mbufs[0] = tx_q.mbufs[tx_q.count - 1];
  tx_q.mbufs[tx_q.count - 1] = 0; // 清理掉最后一个位置

  // 3. 元素数量减一
  tx_q.count--;

  // 4. 对新的堆顶元素执行下沉操作，以恢复堆的性质
  if (tx_q.count > 0)
  {
    sift_down(0);
  }

  release(&tx_q.lock);
  return m;
}

// ----------------------我是一条可爱的分割线ovo----------------------

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
  // initlock(&tx_sw_queue_lock, "tx_sw_queue_lock");
  tx_sw_queue_init();
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
  // 模拟一个 0-100 的随机优先级
  m->priority = rand() % 101;

  if (tx_sw_queue_enqueue(m) < 0)
  {
    return -1; // 队列满了
  }

  // 启动发送流程
  // 注意：kickstart现在不需要在锁内调用了
  e1000_tx_kickstart();

  return 0;
}

static void
e1000_recv(void)
{
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
  struct mbuf *m;

  // 循环地尝试从软件队列搬运包到硬件环
  // 只要软件队列不为空，并且硬件环有空位
  while ((m = tx_sw_queue_dequeue()) != 0)
  {
    acquire(&tx_lock);
    uint32 idx = regs[E1000_TDT];

    if (!(tx_ring[idx].status & E1000_TXD_STAT_DD))
    {
      // 硬件环满了，把刚才取出的 mbuf 再塞回去
      // 这是一个简化的处理，实际中可能有更复杂的重试逻辑
      tx_sw_queue_enqueue(m);
      release(&tx_lock);
      break;
    }

    // 释放旧的 mbuf
    if (tx_mbufs[idx])
      mbuffree(tx_mbufs[idx]);

    // 把从优先队列取出的 mbuf 放到硬件环
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
  uint32 status = regs[E1000_ICR];

  if (status & E1000_ICR_RXT0)
  {
    e1000_recv();
  }

  if (status & E1000_ICR_TXQE)
  {
    // kickstart 函数现在自己管理锁
    e1000_tx_kickstart();
  }

  regs[E1000_ICR] = status;
}

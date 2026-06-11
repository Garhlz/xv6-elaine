//
// ramdisk that uses the disk image loaded by qemu -initrd fs.img
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

void ramdiskinit(void) {}

void ramdiskrw(struct buf *b) {
    if (!holdingsleep(&b->lock))
        panic("ramdiskrw: buf not locked");
    panic("ramdiskrw: unsupported");
}

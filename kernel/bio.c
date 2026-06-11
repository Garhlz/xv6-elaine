// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 29
#define BUCKET_HASH(blockno) ((blockno) % NBUCKET)

struct {
    struct spinlock evict_lock;
    struct spinlock locks[NBUCKET];
    struct buf buf[NBUF];
    struct buf buckets[NBUCKET];
} bcache;

static char bcache_lock_names[NBUCKET][16];

void binit(void) {
    struct buf *b;

    initlock(&bcache.evict_lock, "bcache_evict");
    for (int i = 0; i < NBUCKET; i++) {
        snprintf(bcache_lock_names[i], sizeof(bcache_lock_names[i]), "bcache_%d", i);
        initlock(&bcache.locks[i], bcache_lock_names[i]);
        bcache.buckets[i].prev = &bcache.buckets[i];
        bcache.buckets[i].next = &bcache.buckets[i];
    }

    for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
        b->next = bcache.buckets[0].next;
        b->prev = &bcache.buckets[0];
        initsleeplock(&b->lock, "buffer");
        bcache.buckets[0].next->prev = b;
        bcache.buckets[0].next = b;
    }
}

static int buf_in_bucket(struct buf *target, int bucket_idx) {
    struct buf *b;

    for (b = bcache.buckets[bucket_idx].next; b != &bcache.buckets[bucket_idx]; b = b->next) {
        if (b == target)
            return 1;
    }
    return 0;
}

static struct buf *bget(uint dev, uint blockno) {
    struct buf *b;
    int bucket_idx = BUCKET_HASH(blockno);

    // Hits only need the target bucket lock.
    acquire(&bcache.locks[bucket_idx]);
    for (b = bcache.buckets[bucket_idx].next; b != &bcache.buckets[bucket_idx]; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            release(&bcache.locks[bucket_idx]);
            acquiresleep(&b->lock);
            return b;
        }
    }
    release(&bcache.locks[bucket_idx]);

    acquire(&bcache.evict_lock);

retry:
    // Another CPU may have installed this block while we waited.
    acquire(&bcache.locks[bucket_idx]);
    for (b = bcache.buckets[bucket_idx].next; b != &bcache.buckets[bucket_idx]; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            b->refcnt++;
            release(&bcache.locks[bucket_idx]);
            release(&bcache.evict_lock);
            acquiresleep(&b->lock);
            return b;
        }
    }
    release(&bcache.locks[bucket_idx]);

    struct buf *victim = 0;
    int old_bucket_idx = -1;
    uint oldest_ts = 0xffffffff;
    for (int i = 0; i < NBUCKET; i++) {
        acquire(&bcache.locks[i]);
        for (b = bcache.buckets[i].next; b != &bcache.buckets[i]; b = b->next) {
            if (b->refcnt == 0 && (victim == 0 || b->timestamp < oldest_ts)) {
                victim = b;
                old_bucket_idx = i;
                oldest_ts = b->timestamp;
            }
        }
        release(&bcache.locks[i]);
    }

    if (victim == 0) {
        release(&bcache.evict_lock);
        panic("bget: no buffers");
    }

    acquire(&bcache.locks[old_bucket_idx]);
    if (victim->refcnt != 0 || BUCKET_HASH(victim->blockno) != old_bucket_idx ||
        !buf_in_bucket(victim, old_bucket_idx)) {
        release(&bcache.locks[old_bucket_idx]);
        goto retry;
    }
    victim->next->prev = victim->prev;
    victim->prev->next = victim->next;
    release(&bcache.locks[old_bucket_idx]);

    acquire(&bcache.locks[bucket_idx]);
    victim->dev = dev;
    victim->blockno = blockno;
    victim->valid = 0;
    victim->refcnt = 1;
    victim->timestamp = 0;

    victim->next = bcache.buckets[bucket_idx].next;
    victim->prev = &bcache.buckets[bucket_idx];
    bcache.buckets[bucket_idx].next->prev = victim;
    bcache.buckets[bucket_idx].next = victim;

    release(&bcache.locks[bucket_idx]);
    release(&bcache.evict_lock);

    acquiresleep(&victim->lock);
    return victim;
}

struct buf *bread(uint dev, uint blockno) {
    struct buf *b;

    b = bget(dev, blockno);
    if (!b->valid) {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

void bwrite(struct buf *b) {
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    virtio_disk_rw(b, 1);
}

void brelse(struct buf *b) {
    int bucket_idx;

    if (!holdingsleep(&b->lock))
        panic("brelse");

    releasesleep(&b->lock);

    bucket_idx = BUCKET_HASH(b->blockno);
    acquire(&bcache.locks[bucket_idx]);
    b->refcnt--;
    if (b->refcnt == 0)
        b->timestamp = ticks;
    release(&bcache.locks[bucket_idx]);
}

void bpin(struct buf *b) {
    int bucket_idx = BUCKET_HASH(b->blockno);

    acquire(&bcache.locks[bucket_idx]);
    b->refcnt++;
    release(&bcache.locks[bucket_idx]);
}

void bunpin(struct buf *b) {
    int bucket_idx = BUCKET_HASH(b->blockno);

    acquire(&bcache.locks[bucket_idx]);
    b->refcnt--;
    release(&bcache.locks[bucket_idx]);
}

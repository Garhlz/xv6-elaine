# lab fs handbook:

Lab: file system

In this lab you will add large files and symbolic links to the xv6 file system.

Before writing code, you should read "Chapter 8: File system" from the xv6 book and study the corresponding code.

Fetch the xv6 source for the lab and check out the util branch:

  $ git fetch
  $ git checkout fs
  $ make clean

Large files (moderate)

In this assignment you'll increase the maximum size of an xv6 file. Currently xv6 files are limited to 268 blocks, or 268*BSIZE bytes (BSIZE is 1024 in xv6). This limit comes from the fact that an xv6 inode contains 12 "direct" block numbers and one "singly-indirect" block number, which refers to a block that holds up to 256 more block numbers, for a total of 12+256=268 blocks.

The bigfile command creates the longest file it can, and reports that size:

$ bigfile
..
wrote 268 blocks
bigfile: file is too small
$

The test fails because bigfile expects to be able to create a file with 65803 blocks, but unmodified xv6 limits files to 268 blocks.

You'll change the xv6 file system code to support a "doubly-indirect" block in each inode, containing 256 addresses of singly-indirect blocks, each of which can contain up to 256 addresses of data blocks. The result will be that a file will be able to consist of up to 65803 blocks, or 256*256+256+11 blocks (11 instead of 12, because we will sacrifice one of the direct block numbers for the double-indirect block).
Preliminaries
The mkfs program creates the xv6 file system disk image and determines how many total blocks the file system has; this size is controlled by FSSIZE in kernel/param.h. You'll see that FSSIZE in the repository for this lab is set to 200,000 blocks. You should see the following output from mkfs/mkfs in the make output:

nmeta 70 (boot, super, log blocks 30 inode blocks 13, bitmap blocks 25) blocks 199930 total 200000

This line describes the file system that mkfs/mkfs built: it has 70 meta-data blocks (blocks used to describe the file system) and 199,930 data blocks, totaling 200,000 blocks.
If at any point during the lab you find yourself having to rebuild the file system from scratch, you can run make clean which forces make to rebuild fs.img.
What to Look At
The format of an on-disk inode is defined by struct dinode in fs.h. You're particularly interested in NDIRECT, NINDIRECT, MAXFILE, and the addrs[] element of struct dinode. Look at Figure 8.3 in the xv6 text for a diagram of the standard xv6 inode.

The code that finds a file's data on disk is in bmap() in fs.c. Have a look at it and make sure you understand what it's doing. bmap() is called both when reading and writing a file. When writing, bmap() allocates new blocks as needed to hold file content, as well as allocating an indirect block if needed to hold block addresses.

bmap() deals with two kinds of block numbers. The bn argument is a "logical block number" -- a block number within the file, relative to the start of the file. The block numbers in ip->addrs[], and the argument to bread(), are disk block numbers. You can view bmap() as mapping a file's logical block numbers into disk block numbers.
Your Job
Modify bmap() so that it implements a doubly-indirect block, in addition to direct blocks and a singly-indirect block. You'll have to have only 11 direct blocks, rather than 12, to make room for your new doubly-indirect block; you're not allowed to change the size of an on-disk inode. The first 11 elements of ip->addrs[] should be direct blocks; the 12th should be a singly-indirect block (just like the current one); the 13th should be your new doubly-indirect block. You are done with this exercise when bigfile writes 65803 blocks and usertests runs successfully:

$ bigfile
..................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................................
wrote 65803 blocks
done; ok
$ usertests
...
ALL TESTS PASSED
$ 

bigfile will take at least a minute and a half to run.

Hints:

    Make sure you understand bmap(). Write out a diagram of the relationships between ip->addrs[], the indirect block, the doubly-indirect block and the singly-indirect blocks it points to, and data blocks. Make sure you understand why adding a doubly-indirect block increases the maximum file size by 256*256 blocks (really -1, since you have to decrease the number of direct blocks by one).
    Think about how you'll index the doubly-indirect block, and the indirect blocks it points to, with the logical block number.
    If you change the definition of NDIRECT, you'll probably have to change the declaration of addrs[] in struct inode in file.h. Make sure that struct inode and struct dinode have the same number of elements in their addrs[] arrays.
    If you change the definition of NDIRECT, make sure to create a new fs.img, since mkfs uses NDIRECT to build the file system.
    If your file system gets into a bad state, perhaps by crashing, delete fs.img (do this from Unix, not xv6). make will build a new clean file system image for you.
    Don't forget to brelse() each block that you bread().
    You should allocate indirect blocks and doubly-indirect blocks only as needed, like the original bmap().
    Make sure itrunc frees all blocks of a file, including double-indirect blocks. 

Symbolic links (moderate)

In this exercise you will add symbolic links to xv6. Symbolic links (or soft links) refer to a linked file by pathname; when a symbolic link is opened, the kernel follows the link to the referred file. Symbolic links resembles hard links, but hard links are restricted to pointing to file on the same disk, while symbolic links can cross disk devices. Although xv6 doesn't support multiple devices, implementing this system call is a good exercise to understand how pathname lookup works.
Your job

You will implement the symlink(char *target, char *path) system call, which creates a new symbolic link at path that refers to file named by target. For further information, see the man page symlink. To test, add symlinktest to the Makefile and run it. Your solution is complete when the tests produce the following output (including usertests succeeding).

$ symlinktest
Start: test symlinks
test symlinks: ok
Start: test concurrent symlinks
test concurrent symlinks: ok
$ usertests
...
ALL TESTS PASSED
$ 

Hints:

    First, create a new system call number for symlink, add an entry to user/usys.pl, user/user.h, and implement an empty sys_symlink in kernel/sysfile.c.
    Add a new file type (T_SYMLINK) to kernel/stat.h to represent a symbolic link.
    Add a new flag to kernel/fcntl.h, (O_NOFOLLOW), that can be used with the open system call. Note that flags passed to open are combined using a bitwise OR operator, so your new flag should not overlap with any existing flags. This will let you compile user/symlinktest.c once you add it to the Makefile.
    Implement the symlink(target, path) system call to create a new symbolic link at path that refers to target. Note that target does not need to exist for the system call to succeed. You will need to choose somewhere to store the target path of a symbolic link, for example, in the inode's data blocks. symlink should return an integer representing success (0) or failure (-1) similar to link and unlink.
    Modify the open system call to handle the case where the path refers to a symbolic link. If the file does not exist, open must fail. When a process specifies O_NOFOLLOW in the flags to open, open should open the symlink (and not follow the symbolic link).
    If the linked file is also a symbolic link, you must recursively follow it until a non-link file is reached. If the links form a cycle, you must return an error code. You may approximate this by returning an error code if the depth of links reaches some threshold (e.g., 10).
    Other system calls (e.g., link and unlink) must not follow symbolic links; these system calls operate on the symbolic link itself.
    You do not have to handle symbolic links to directories for this lab. 

Submit the lab

This completes the lab. Make sure you pass all of the make grade tests. If this lab had questions, don't forget to write up your answers to the questions in answers-lab-name.txt. Commit your changes (including adding answers-lab-name.txt) and type make handin in the lab directory to hand in your lab.
Time spent

Create a new file, time.txt, and put in it a single integer, the number of hours you spent on the lab. Don't forget to git add and git commit the file.
Submit
You will turn in your assignments using the submission website. You need to request once an API key from the submission website before you can turn in any assignments or labs.

After committing your final changes to the lab, type make handin to submit your lab.

$ git commit -am "ready to submit my lab"
[util c2e3c8b] ready to submit my lab
 2 files changed, 18 insertions(+), 2 deletions(-)

$ make handin
tar: Removing leading `/' from member names
Get an API key for yourself by visiting https://6828.scripts.mit.edu/2021/handin.py/
Please enter your API key: XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
  % Total    % Received % Xferd  Average Speed   Time    Time     Time  Current
                                 Dload  Upload   Total   Spent    Left  Speed
100 79258  100   239  100 79019    853   275k --:--:-- --:--:-- --:--:--  276k
$

make handin will store your API key in myapi.key. If you need to change your API key, just remove this file and let make handin generate it again (myapi.key must not include newline characters).

If you run make handin and you have either uncomitted changes or untracked files, you will see output similar to the following:

 M hello.c
?? bar.c
?? foo.pyc
Untracked files will not be handed in.  Continue? [y/N]

Inspect the above lines and make sure all files that your lab solution needs are tracked i.e. not listed in a line that begins with ??. You can cause git to track a new file that you create using git add filename.

If make handin does not work properly, try fixing the problem with the curl or Git commands. Or you can run make tarball. This will make a tar file for you, which you can then upload via our web interface.

    Please run `make grade` to ensure that your code passes all of the tests
    Commit any modified source code before running `make handin`
    You can inspect the status of your submission and download the submitted code at https://6828.scripts.mit.edu/2021/handin.py/

Optional challenge exercises

Support triple-indirect blocks.
Acknowledgment

Thanks to the staff of UW's CSEP551 (Fall 2019) for the symlink exercise. 

# fs.c:
// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"
#include "fcntl.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
// there should be one superblock per disk device, but we run with
// only one device
struct superblock sb;

// Read the super block.
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Init fs
void fsinit(int dev)
{
  readsb(dev, &sb);
  if (sb.magic != FSMAGIC)
    panic("invalid file system");
  initlog(dev, &sb);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for (b = 0; b < sb.size; b += BPB)
  {
    bp = bread(dev, BBLOCK(b, sb));
    for (bi = 0; bi < BPB && b + bi < sb.size; bi++)
    {
      m = 1 << (bi % 8);
      if ((bp->data[bi / 8] & m) == 0)
      {                        // Is block free?
        bp->data[bi / 8] |= m; // Mark block in use.
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  panic("balloc: out of blocks");
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if ((bp->data[bi / 8] & m) == 0)
    panic("freeing free block");
  bp->data[bi / 8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inodes.
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at
// sb.startinode. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a table of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The in-memory
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in table: an entry in the inode table
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a table entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   table entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays in the table and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The itable.lock spin-lock protects the allocation of itable
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold itable.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

struct
{
  struct spinlock lock;
  struct inode inode[NINODE];
} itable;

void iinit()
{
  int i = 0;

  initlock(&itable.lock, "itable");
  for (i = 0; i < NINODE; i++)
  {
    initsleeplock(&itable.inode[i].lock, "inode");
  }
}

static struct inode *iget(uint dev, uint inum);

// Allocate an inode on device dev.
// Mark it as allocated by  giving it type type.
// Returns an unlocked but allocated and referenced inode.
struct inode *
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for (inum = 1; inum < sb.ninodes; inum++)
  {
    bp = bread(dev, IBLOCK(inum, sb));
    dip = (struct dinode *)bp->data + inum % IPB;
    if (dip->type == 0)
    { // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp); // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk.
// Caller must hold ip->lock.
void iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode *)bp->data + ip->inum % IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy. Does not lock
// the inode and does not read it from disk.
static struct inode *
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&itable.lock);

  // Is the inode already in the table?
  empty = 0;
  for (ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++)
  {
    if (ip->ref > 0 && ip->dev == dev && ip->inum == inum)
    {
      ip->ref++;
      release(&itable.lock);
      return ip;
    }
    if (empty == 0 && ip->ref == 0) // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode entry.
  if (empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&itable.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode *
idup(struct inode *ip)
{
  acquire(&itable.lock);
  ip->ref++;
  release(&itable.lock);
  return ip;
}

// Lock the given inode.
// Reads the inode from disk if necessary.
void ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if (ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if (ip->valid == 0)
  {
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode *)bp->data + ip->inum % IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if (ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void iunlock(struct inode *ip)
{
  if (ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// Drop a reference to an in-memory inode.
// If that was the last reference, the inode table entry can
// be recycled.
// If that was the last reference and the inode has no links
// to it, free the inode (and its content) on disk.
// All calls to iput() must be inside a transaction in
// case it has to free the inode.
void iput(struct inode *ip)
{
  acquire(&itable.lock);

  if (ip->ref == 1 && ip->valid && ip->nlink == 0)
  {
    // inode has no links and no other references: truncate and free.

    // ip->ref == 1 means no other process can have ip locked,
    // so this acquiresleep() won't block (or deadlock).
    acquiresleep(&ip->lock);

    release(&itable.lock);

    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    ip->valid = 0;

    releasesleep(&ip->lock);

    acquire(&itable.lock);
  }

  ip->ref--;
  release(&itable.lock);
}

// Common idiom: unlock, then put.
void iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.

// TODO 支持双重间接块
static uint
bmap(struct inode *inode_pointer, uint block_number)
{
  uint cur_addr, *cur_buffer_array;
  struct buf *buffer_pointer;

  if (block_number < NDIRECT)
  {
    if ((cur_addr = inode_pointer->addrs[block_number]) == 0)
      inode_pointer->addrs[block_number] = cur_addr = balloc(inode_pointer->dev);
    return cur_addr;
  }
  block_number -= NDIRECT;

  if (block_number < NINDIRECT)
  {
    // Load indirect block, allocating if necessary.
    if ((cur_addr = inode_pointer->addrs[NDIRECT]) == 0)
      inode_pointer->addrs[NDIRECT] = cur_addr = balloc(inode_pointer->dev);
    // 读取到了间接块的buffer
    buffer_pointer = bread(inode_pointer->dev, cur_addr);
    // 从中取出数据部分
    cur_buffer_array = (uint *)buffer_pointer->data;
    if ((cur_addr = cur_buffer_array[block_number]) == 0)
    {
      cur_buffer_array[block_number] = cur_addr = balloc(inode_pointer->dev);
      log_write(buffer_pointer);
    }
    brelse(buffer_pointer);
    return cur_addr;
  }

  // 在第一次写入时通过balloc分配双重间接块
  if (block_number < NINDIRECT * NINDIRECT)
  {
    if ((cur_addr = inode_pointer->addrs[NDIRECT + 1]) == 0)
    {
      cur_addr = balloc(inode_pointer->dev);
      inode_pointer->addrs[NDIRECT + 1] = cur_addr;
    }
    // 当前指向的是双重间接块
    buffer_pointer = bread(inode_pointer->dev, cur_addr);
    cur_buffer_array = (uint *)buffer_pointer->data;

    // 计算应该读取双重间接块中的哪个条目
    uint first_index = block_number / NINDIRECT;
    if ((cur_addr = cur_buffer_array[first_index]) == 0)
    {
      cur_addr = balloc(inode_pointer->dev);
      cur_buffer_array[first_index] = cur_addr;
      log_write(buffer_pointer);
    }
    brelse(buffer_pointer);
    // 读取到第一级间接块
    buffer_pointer = bread(inode_pointer->dev, cur_addr);
    cur_buffer_array = (uint *)buffer_pointer->data;

    // 计算应该读取单重间接块中的哪个条目
    uint second_index = block_number % NINDIRECT;
    if ((cur_addr = cur_buffer_array[second_index]) == 0)
    {
      cur_addr = balloc(inode_pointer->dev);
      cur_buffer_array[second_index] = cur_addr;
      log_write(buffer_pointer);
    }
    brelse(buffer_pointer);
    return cur_addr;
  }
  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Caller must hold ip->lock.
void itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for (i = 0; i < NDIRECT; i++)
  {
    if (ip->addrs[i])
    {
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if (ip->addrs[NDIRECT])
  {
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint *)bp->data;
    for (j = 0; j < NINDIRECT; j++)
    {
      if (a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  struct buf *bp2;
  uint *a2;
  if (ip->addrs[NDIRECT + 1])
  {
    bp = bread(ip->dev, ip->addrs[NDIRECT + 1]);
    a = (uint *)bp->data;
    for (j = 0; j < NINDIRECT; j++)
    {
      if (a[j])
      {
        bp2 = bread(ip->dev, a[j]);
        a2 = (uint *)bp2->data;

        for (i = 0; i < NINDIRECT; i++)
        {
          if (a2[i])
          {
            bfree(ip->dev, a2[i]);
          }
        }

        brelse(bp2);
        bfree(ip->dev, a[j]);
      }
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT + 1]);
    ip->addrs[NDIRECT + 1] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

// Read data from inode.
// Caller must hold ip->lock.
// If user_dst==1, then dst is a user virtual address;
// otherwise, dst is a kernel address.
int readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if (off > ip->size || off + n < off)
    return 0;
  if (off + n > ip->size)
    n = ip->size - off;

  for (tot = 0; tot < n; tot += m, off += m, dst += m)
  {
    bp = bread(ip->dev, bmap(ip, off / BSIZE));
    m = min(n - tot, BSIZE - off % BSIZE);
    if (either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1)
    {
      brelse(bp);
      tot = -1;
      break;
    }
    brelse(bp);
  }
  return tot;
}

// Write data to inode.
// Caller must hold ip->lock.
// If user_src==1, then src is a user virtual address;
// otherwise, src is a kernel address.
// Returns the number of bytes successfully written.
// If the return value is less than the requested n,
// there was an error of some kind.
int writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if (off > ip->size || off + n < off)
    return -1;
  if (off + n > MAXFILE * BSIZE)
    return -1;

  for (tot = 0; tot < n; tot += m, off += m, src += m)
  {
    bp = bread(ip->dev, bmap(ip, off / BSIZE));
    m = min(n - tot, BSIZE - off % BSIZE);
    if (either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1)
    {
      brelse(bp);
      break;
    }
    log_write(bp);
    brelse(bp);
  }

  if (off > ip->size)
    ip->size = off;

  // write the i-node back to disk even if the size didn't change
  // because the loop above might have called bmap() and added a new
  // block to ip->addrs[].
  iupdate(ip);

  return tot;
}

// Directories

int namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct inode *
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if (dp->type != T_DIR)
    panic("dirlookup not DIR");

  for (off = 0; off < dp->size; off += sizeof(de))
  {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if (de.inum == 0)
      continue;
    if (namecmp(name, de.name) == 0)
    {
      // entry matches path element
      if (poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
int dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if ((ip = dirlookup(dp, name, 0)) != 0)
  {
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for (off = 0; off < dp->size; off += sizeof(de))
  {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if (de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char *
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while (*path == '/')
    path++;
  if (*path == 0)
    return 0;
  s = path;
  while (*path != '/' && *path != 0)
    path++;
  len = path - s;
  if (len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else
  {
    memmove(name, s, len);
    name[len] = 0;
  }
  while (*path == '/')
    path++;
  return path;
}

#define MAX_SYMLINK_DEPTH 10
// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
// Must be called inside a transaction since it calls iput().
static struct inode *
namex(char *path, int nameiparent, char *name, int flags, int depth)
{
  struct inode *ip, *next;
  char target_path[MAXPATH];
  // 1. 确定起点
  if (*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(myproc()->cwd);

  // 2. 循环解析路径组件
  while ((path = skipelem(path, name)) != 0)
  {
    ilock(ip); // a. 锁住当前目录 ip
    if (ip->type != T_DIR)
    {
      iunlockput(ip);
      return 0;
    }
    // c. 如果是 nameiparent 且是最后一个组件，提前返回父目录
    if (nameiparent && *path == '\0')
    {
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    // d. 在当前目录 ip 中查找名为 name 的条目，获取其 inode next
    if ((next = dirlookup(ip, name, 0)) == 0)
    {
      iunlockput(ip);
      return 0;
    }

    // 处理".."
    if (namecmp(name, "..") == 0 && ip->inum == ROOTINO)
    {
      // 当前目录是根目录，并且查找的是 ".."
      // 不需要获取 next 的锁，也不需要更新 ip
      // 只需释放 next (它指向根目录自己) 的引用，然后继续处理路径的下一部分
      iput(next);
      // 解锁当前目录 ip (根目录)
      iunlock(ip);
      // 继续下一轮循环，ip 保持不变 (仍是根目录)
      continue;
    }

    // TODO 处理符号链接
    ilock(next);

    if (next->type == T_SYMLINK)
    {
      if ((flags & O_NOFOLLOW) && *path == '\0')
      {
        iunlock(ip);
        // 注意：next (符号链接 inode) 仍然是 locked 状态返回
        // sys_open 需要处理这种情况 (稍后会看到)
        return next;
      }

      if (depth >= MAX_SYMLINK_DEPTH)
      {
        iunlockput(ip);
        iunlockput(next);
        return 0;
      }

      // 读取符号链接的内容 (目标路径)
      if (readi(next, 0, (uint64)target_path, 0, MAXPATH) <= 0 || next->size >= MAXPATH)
      {
        // 读取失败或路径太长
        iunlockput(next);
        iunlockput(ip);
        return 0;
      }

      target_path[next->size] = '\0'; // 确保字符串结尾
      // iunlockput(next);
      // iunlockput(ip);

      struct inode *start_node;
      if (target_path[0] == '/')
      {
        start_node = iget(ROOTDEV, ROOTINO); // 绝对路径从根开始
      }
      else
      {
        start_node = idup(ip); // 相对路径从当前目录开始
      }
      iunlockput(next);
      iunlockput(ip);
      ip = start_node;
      path = target_path;
      depth++;
      continue;
    }
    // 这里不应该put next，只能解锁，因为之前为了检查符号链接而上锁了
    iunlock(next);
    iunlockput(ip);
    ip = next;
  }
  if (nameiparent)
  {
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode *
namei(char *path, int flags)
{
  char name[DIRSIZ];
  return namex(path, 0, name, flags, 0);
}

struct inode *
nameiparent(char *path, char *name)
{
  return namex(path, 1, name, 0, 0);
}


# sysfile.c
//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  if (argint(n, &fd) < 0)
    return -1;
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (pfd)
    *pfd = fd;
  if (pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for (fd = 0; fd < NOFILE; fd++)
  {
    if (p->ofile[fd] == 0)
    {
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if (argfd(0, 0, &f) < 0)
    return -1;
  if ((fd = fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  if (argfd(0, 0, &f) < 0 || argint(2, &n) < 0 || argaddr(1, &p) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if (argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  if (argfd(0, 0, &f) < 0 || argaddr(1, &st) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  // 因为 link 操作的是文件本身，不应跟随链接
  if ((ip = namei(old, O_NOFOLLOW)) == 0)
  {
    end_op();
    return -1;
  }

  ilock(ip);
  if (ip->type == T_DIR)
  {
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if ((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0)
  {
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de))
  {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if (de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((dp = nameiparent(path, name)) == 0)
  {
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if ((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if (ip->nlink < 1)
    panic("unlink: nlink < 1");
  if (ip->type == T_DIR && !isdirempty(ip))
  {
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if (ip->type == T_DIR)
  {
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode *
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if ((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if ((ip = dirlookup(dp, name, 0)) != 0)
  {
    iunlockput(dp);
    ilock(ip);
    // 不允许覆盖符号链接来创建文件/目录
    if (ip->type == T_SYMLINK)
    {
      iunlockput(ip);
      return 0; // 返回错误
    }
    if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if ((ip = ialloc(dp->dev, type)) == 0)
    panic("create: ialloc");

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if (type == T_DIR)
  {              // Create . and .. entries.
    dp->nlink++; // for ".."
    iupdate(dp);
    // No ip->nlink++ for ".": avoid cyclic ref count.
    // create 函数在创建新目录时，会自动调用 dirlink 添加 . 和 .. 这两个条目
    if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      panic("create dots");
  }

  if (dirlink(dp, name, ip->inum) < 0)
    panic("create: dirlink");

  iunlockput(dp);

  return ip;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;
  int need_unlock = 0;
  if ((n = argstr(0, path, MAXPATH)) < 0 || argint(1, &omode) < 0)
    return -1;

  begin_op();

  if (omode & O_CREATE)
  {
    ip = create(path, T_FILE, 0, 0);
    if (ip == 0)
    {
      end_op();
      return -1;
    }
  }
  else
  {
    if ((ip = namei(path, omode)) == 0)
    {
      end_op();
      return -1;
    }
    // 如果O_NOFOLLOW 且命中了最后一个 symlink， 返回的是上锁的链接文件

    if (ip->type != T_SYMLINK || !(omode & O_NOFOLLOW))
    {
      ilock(ip);
      need_unlock = 1;
    }
    else
    {
      // O_NOFOLLOW 命中了最后一个 symlink，namex 已经返回了 locked inode，我们什么都不用做
    }

    if (ip->type == T_DIR && omode != O_RDONLY)
    {
      iunlockput(ip);
      end_op();
      return -1;
    }

    // 最后是符号链接，而且没有指定O_NOFOLLOW, 只可能是出错了
    if (ip->type == T_SYMLINK && !(omode & O_NOFOLLOW))
    {
      if (need_unlock)
        iunlockput(ip);
      else
        iput(ip);
      end_op();
      return -1;
    }
  }

  if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV))
  {
    iunlockput(ip);
    end_op();
    return -1;
  }

  if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0)
  {
    if (f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if (ip->type == T_DEVICE)
  {
    f->type = FD_DEVICE;
    f->major = ip->major;
  }
  else
  {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if ((omode & O_TRUNC) && ip->type == T_FILE)
  {
    itrunc(ip);
  }

  if (!need_unlock && !(omode & O_CREATE))
  {
  }
  else
  {
    iunlock(ip);
  }
  end_op();
  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0)
  {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  if ((argstr(0, path, MAXPATH)) < 0 ||
      argint(1, &major) < 0 ||
      argint(2, &minor) < 0 ||
      (ip = create(path, T_DEVICE, major, minor)) == 0)
  {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path, 0)) == 0)
  {
    end_op();
    return -1;
  }
  ilock(ip);
  if (ip->type != T_DIR)
  {
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  if (argstr(0, path, MAXPATH) < 0 || argaddr(1, &uargv) < 0)
  {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for (i = 0;; i++)
  {
    if (i >= NELEM(argv))
    {
      goto bad;
    }
    if (fetchaddr(uargv + sizeof(uint64) * i, (uint64 *)&uarg) < 0)
    {
      goto bad;
    }
    if (uarg == 0)
    {
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if (argv[i] == 0)
      goto bad;
    if (fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }
  // 无需修改，原始exec会检查ELF头
  int ret = exec(path, argv);

  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

bad:
  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if (argaddr(0, &fdarray) < 0)
    return -1;
  if (pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0)
  {
    if (fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if (copyout(p->pagetable, fdarray, (char *)&fd0, sizeof(fd0)) < 0 ||
      copyout(p->pagetable, fdarray + sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0)
  {
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// TODO: 实现 sys_symlink 系统调用
uint64
sys_symlink(void)
{
  char target[MAXPATH], path[MAXPATH];
  struct inode *ip;

  if (argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0)
  {
    return -1;
  }

  begin_op();

  ip = create(path, T_SYMLINK, 0, 0);

  if (ip == 0)
  {
    end_op();
    return -1;
  }

  int target_len = strlen(target);
  if (writei(ip, 0, (uint64)target, 0, target_len) != target_len)
  {
    iunlockput(ip);
    end_op();
    return -1;
  }

  iunlockput(ip);
  end_op();
  return 0;
}

# bigfile.c:
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"

int main()
{
  char buf[BSIZE];
  int fd, i, blocks;

  fd = open("big.file", O_CREATE | O_WRONLY);
  if (fd < 0)
  {
    printf("bigfile: cannot open big.file for writing\n");
    exit(-1);
  }

  blocks = 0;
  while (1)
  {
    *(int *)buf = blocks;
    int cc = write(fd, buf, sizeof(buf));
    if (cc <= 0)
      break;
    blocks++;
    if (blocks % 100 == 0)
      printf(".");
  }

  printf("\nwrote %d blocks\n", blocks);
  if (blocks != 65803)
  {
    printf("bigfile: file is too small\n");
    exit(-1);
  }

  close(fd);
  fd = open("big.file", O_RDONLY);
  if (fd < 0)
  {
    printf("bigfile: cannot re-open big.file for reading\n");
    exit(-1);
  }
  for (i = 0; i < blocks; i++)
  {
    int cc = read(fd, buf, sizeof(buf));
    if (cc <= 0)
    {
      printf("bigfile: read error at block %d\n", i);
      exit(-1);
    }
    if (*(int *)buf != i)
    {
      printf("bigfile: read the wrong data (%d) for block %d\n",
             *(int *)buf, i);
      exit(-1);
    }
  }

  printf("bigfile done; ok\n");

  exit(0);
}

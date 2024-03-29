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

#define NBUCK 13

struct bucket {
  struct spinlock lock;
  struct buf head;
  char name[16];
};


struct {
  struct spinlock eviction_lock;
  struct buf buf[NBUF];
  struct bucket buck[NBUCK];
} bcache;

uint hash(uint dev, uint blockno) {
  return (dev + blockno) % NBUCK;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.eviction_lock, "bcache_eviction");

  for (int i = 0; i < NBUCK; i++) {
    snprintf(bcache.buck[i].name, sizeof(bcache.buck[i].name), "bcache_%d", i);
    initlock(&bcache.buck[i].lock, bcache.buck[i].name);
    bcache.buck[i].head.next = 0;
  }

  for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
    initsleeplock(&b->lock, "buffer");
    b->refcnt = 0;
    b->timestamp = 0;
    b->next = bcache.buck[0].head.next;
    bcache.buck[0].head.next = b;
  }

  // // Create linked list of buffers
  // bcache.head.prev = &bcache.head;
  // bcache.head.next = &bcache.head;
  // for(b = bcache.buf; b < bcache.buf+NBUF; b++){
  //   b->next = bcache.head.next;
  //   b->prev = &bcache.head;
  //   initsleeplock(&b->lock, "buffer");
  //   bcache.head.next->prev = b;
  //   bcache.head.next = b;
  // }
}


// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  uint key = hash(dev, blockno);

  // find buffer cahce in the key bucket
  acquire(&bcache.buck[key].lock);
  b = bcache.buck[key].head.next;
  while (b) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&bcache.buck[key].lock);
      acquiresleep(&b->lock);
      return b;
    }
    b = b->next;
  }
  // not cached
  release(&bcache.buck[key].lock);

  // serialize finding an unused buf
  acquire(&bcache.eviction_lock); 
  // when we've released the lock above, it's possible that there's an identical 
  // blockno cache buffer before we get key bucket lock again. so we need to make sure there's no
  // other cpu get into the eviction. 
  b = bcache.buck[key].head.next;
  while (b) {
    if (b->dev == dev && b->blockno == blockno) {
      // it's obvious that it'll be no other eviction operations for we've got eviction lock
      acquire(&bcache.buck[key].lock); 
      b->refcnt++;
      release(&bcache.buck[key].lock);
      release(&bcache.eviction_lock);
      acquiresleep(&b->lock);
      return b;
    }
    b = b->next;
  }
  // still no cached, steal from the other bucket
  struct buf *prelru = 0;
  int nbucket = -1;
  uint find;
  for (int i = 0; i < NBUCK; i++) {
    find = 0;
    // it will be no dead lock because we follow the rule that acquiring the 
    // second lock only after the bucket lock 
    acquire(&bcache.buck[i].lock); 
    b = &bcache.buck[i].head;
    while (b->next) {
      if (b->next->refcnt == 0 && (!prelru || (b->next->timestamp < prelru->next->timestamp))) {
        prelru = b;
        find = 1;
      }
      b = b->next;
    }
    if (find) {
      if (nbucket != -1) {
        release(&bcache.buck[nbucket].lock); // if find, release the previous held lock
      }
      nbucket = i; 
    } else {
      release(&bcache.buck[i].lock); // if not find, release the current iteration lock
    }
  }
  if (prelru == 0) {
    panic("bget: prelru");
  }
  struct buf *curlru = prelru->next;
  prelru->next = curlru->next;
  release(&bcache.buck[nbucket].lock); // here can be optimized

  curlru->dev = dev;
  curlru->blockno = blockno;
  curlru->valid = 0;
  curlru->refcnt = 1;
  acquire(&bcache.buck[key].lock);
  curlru->next = bcache.buck[key].head.next;
  bcache.buck[key].head.next = curlru;
  release(&bcache.buck[key].lock);

  release(&bcache.eviction_lock);
  acquiresleep(&curlru->lock);

  return curlru;
  // acquire(&bcache.lock);

  // // Is the block already cached?
  // for(b = bcache.head.next; b != &bcache.head; b = b->next){
  //   if(b->dev == dev && b->blockno == blockno){
  //     b->refcnt++;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }

  // // Not cached.
  // // Recycle the least recently used (LRU) unused buffer.
  // for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
  //   if(b->refcnt == 0) {
  //     b->dev = dev;
  //     b->blockno = blockno;
  //     b->valid = 0;
  //     b->refcnt = 1;
  //     release(&bcache.lock);
  //     acquiresleep(&b->lock);
  //     return b;
  //   }
  // }
  // panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  uint key = hash(b->dev, b->blockno);
  acquire(&bcache.buck[key].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    // b->next->prev = b->prev;
    // b->prev->next = b->next;
    // b->next = bcache.head.next;
    // b->prev = &bcache.head;
    // bcache.head.next->prev = b;
    // bcache.head.next = b;
    b->timestamp = ticks;
  }
  
  release(&bcache.buck[key].lock);
}

void
bpin(struct buf *b) {
  uint key = hash(b->dev, b->blockno);
  acquire(&bcache.buck[key].lock);
  b->refcnt++;
  release(&bcache.buck[key].lock);
}

void
bunpin(struct buf *b) {
  uint key = hash(b->dev, b->blockno);
  acquire(&bcache.buck[key].lock);
  b->refcnt--;
  release(&bcache.buck[key].lock);
}
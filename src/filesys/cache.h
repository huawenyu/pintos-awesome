#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include <list.h>

/* The number of ticks between cache_block count updates. */
#define CACHE_TIMER_FREQ 100
/* The number of ticks between periodically writing all 
 * dirty bits. */
#define CACHE_WRITE_ALL_FREQ 10000
/* Maximum number of cache_blocks in the cache. */
#define CACHE_SIZE 64

struct cache_block {
    /* In this implementation, the combination of inode
     * and sector_idx determine the uniqueness of a block. */
    /* In practice, if you only need sector_idx to determine
     * the uniqueness of a block, you can delete the following
     * inode struct and change the block_in_cache() function. */
    /* The inode the block belongs to. */
    struct inode *inode;
    /* The disk sector of the inode. */
    block_sector_t sector_idx;
    /* The block of memory allocated for the data. 
     * For some reason, this is defined as a uint8_t * in 
     * inode.c (see the bounce buffer), so I left it the 
     * same here. */
    uint8_t *block;
    /* How recently the block has been accessed. */
    int count;
    bool dirty;
    bool accessed;

    struct list_elem elem;
};

extern bool fs_buffer_cache_is_inited;

void buffer_cache_init(void);


/* Evicts a block currently in the cache. */
void evict_block(void);

uint8_t * cache_read(struct inode *inode, block_sector_t sector_idx);
uint8_t * cache_write(struct inode *inode, block_sector_t sector_idx);
/* Writes data from the cache to the disk. This should
 * happen in the following situations:
 * 1. When a dirty block is evicted
 * 2. When the timer calls it (periodically write all 
 *    dirty blocks back to disk. */
void cache_write_to_disk(struct cache_block *c);

void buffer_cache_tick(int64_t curr_ticks);

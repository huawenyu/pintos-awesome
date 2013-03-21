#include "filesys/cache.h"
#include <list.h>
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "devices/block.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"

static struct list cache_block_list;
bool fs_buffer_cache_is_inited = false;

/* TODO: Add synchronization. */

/* Initialize all necessary structures. */
void buffer_cache_init(void) {
    list_init(&cache_block_list);
    fs_buffer_cache_is_inited = true;
}

/* Returns the block if the desired block is currently in the
 * cache and null otherwise. */
struct cache_block * block_in_cache(struct inode *inode, block_sector_t sector_idx) {
    struct list_elem *e;
    struct cache_block *c;

    e = list_head(&cache_block_list);
    while (e != list_end(&cache_block_list) && e != NULL) {
        c = list_entry(e, struct cache_block, elem);
        if (c->inode == inode && c->sector_idx == sector_idx) {
            return c;
        }
        e = list_next(e);
    }
    return NULL;
}

// TODO: Implement read-ahead functionality. Should go in inode.c
/* Reads a block from the cache. If it's not currently there, it 
 * finds the correct block from memory and puts it in the cache, 
 * evicting another block if necessary.
 * Returns a pointer to a buffer that can be read from. */
uint8_t * cache_read(struct inode *inode, block_sector_t sector_idx) {
    struct cache_block *c;
    uint8_t *buffer = NULL;

    /* Check if the block is in the cache. */
    c = block_in_cache(inode, sector_idx);
    /* If it isn't, find the corresponding data in the filesystem
     * and allocate a new block for it and put it into the buffer, 
     * evicting an old block if necessary. */
    if (c == NULL) {
        buffer = malloc(BLOCK_SECTOR_SIZE);
        block_read(fs_device, sector_idx, buffer);
        c = malloc(sizeof(struct cache_block));
        c->inode = inode;
        c->sector_idx = sector_idx;
        c->block = buffer;
        c->count = 0;
        c->accessed = true;
        c->dirty = false;
        if (list_size(&cache_block_list) == CACHE_SIZE) {
            evict_block();
            list_push_back(&cache_block_list, &c->elem);
        }
    }
    else {
        c->accessed = true;
    }
    return c->block;
}

/* Writes to a block in the cache. If it's not currently there, it 
 * finds the correct block from memory and puts it in the cache,
 * evicting another block if necessary.
 * Returns a pointer to a buffer that can be written to. */
uint8_t * cache_write(struct inode *inode, block_sector_t sector_idx) {
    struct cache_block *c;
    uint8_t *buffer = NULL;

    /* Check if the block is in the cache. */
    c = block_in_cache(inode, sector_idx);
    /* If it isn't, find the corresponding data in the filesystem
     * and allocate a new block for it and put it into the buffer, 
     * evicting an old block if necessary. */
    if (c == NULL) {
        buffer = malloc(BLOCK_SECTOR_SIZE);
        /* Reads the data from the disk to the buffer.
         * Writes happen later. */
        block_read(fs_device, sector_idx, buffer);
        c = malloc(sizeof(struct cache_block));
        c->inode = inode;
        c->sector_idx = sector_idx;
        c->block = buffer;
        c->count = 0;
        c->accessed = true;
        c->dirty = true;
        if (list_size(&cache_block_list) == CACHE_SIZE) {
            evict_block();
            list_push_back(&cache_block_list, &c->elem);
        }
    }
    else {
        c->accessed = true;
        c->dirty = true;
    }
    return c->block;
}

/* Writes the data in a buffer to disk. This is called on two 
 * occasions:
 * 1. When a block is evicted (in evict_block()).
 * 2. Periodically when all dirty blocks are written back to disk
 *    (in buffer_cache_tick()). */
void cache_write_to_disk(struct cache_block *c) {
    // TODO: Lock around this.
    block_write(fs_device, c->sector_idx, c->block);
    c->dirty = false;
}

/* Uses an aging replacement policy to find the block to evict. */
void evict_block(void) {
    struct list_elem *e;
    struct cache_block *c;
    struct cache_block *evict = NULL;
    int lowest_count;

    e = list_head(&cache_block_list);
    ASSERT(e != NULL);
    c = list_entry(e, struct cache_block, elem);
    lowest_count = c->count;
    while (e != list_end(&cache_block_list)) {
        c = list_entry(e, struct cache_block, elem);
        if (c->count < lowest_count) {
            lowest_count = c->count;
            evict = c;
        }
        e = list_next(e);
    }

    ASSERT(evict != NULL);
    /* Write to filesystem if block was dirty */
    if (evict->dirty) {
        cache_write_to_disk(evict);
    }
    /* Free memory. */
    free(evict); // TODO: Does this free the malloc'd evict->block buffer as well?
}

/* 1. Updates the count of each block depending on whether or 
 *    not it's been accessed since the last timer tick. 
 * 2. Writes all dirty blocks back to disk. */
void buffer_cache_tick(int64_t cur_ticks) {
    if(!fs_buffer_cache_is_inited) { return; } // Haven't inited buffer cache yet

    struct list_elem *e;
    struct cache_block *c;
    bool accessed;

    if (cur_ticks % CACHE_TIMER_FREQ == 0) {
        e = list_head(&cache_block_list);
        while (e != list_end(&cache_block_list) && e != NULL) {
            c = list_entry(e, struct cache_block, elem);
            accessed = c->accessed;
            c->count = c->count >> 1;
            c->count &= (accessed << (sizeof(c->count) - 1));
            c->accessed = false;
            e = list_next(e);
        }
    }

    // TODO: Lock around this.
    if (cur_ticks % CACHE_WRITE_ALL_FREQ == 0) {
        e = list_head(&cache_block_list);
        while (e != list_end(&cache_block_list) && e != NULL) {
            c = list_entry(e, struct cache_block, elem);
            if (c->dirty) {
                cache_write_to_disk(c);
            }
            e = list_next(e);
        }
    }
}



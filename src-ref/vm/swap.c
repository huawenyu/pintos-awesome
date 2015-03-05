#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"

struct block *swap_block;
static struct bitmap *swap_free;

static const size_t NUM_SECTORS_PER_PAGE = PGSIZE / BLOCK_SECTOR_SIZE;

void vm_swap_init() {
  swap_block = block_get_role(BLOCK_SWAP);
  if(swap_block == NULL) { PANIC("Cannot get swap block"); }

  swap_free = bitmap_create( block_size(swap_block) / NUM_SECTORS_PER_PAGE);
  bitmap_set_all(swap_free, true);
}

void vm_swap_read(int swap_page, const void *uaddr) {
  if(!bitmap_test(swap_free, swap_page)) { 
    PANIC("Trying to read empty swap page!");
  }

  int i;
  for(i = 0; i < NUM_SECTORS_PER_PAGE; ++i) {
    block_read(swap_block, (swap_page * NUM_SECTORS_PER_PAGE) + i,
               uaddr + (i * BLOCK_SECTOR_SIZE));
  }

  bitmap_set(swap_free, swap_page, true);  
}

int vm_swap_write(const void *uaddr) {
  int swap_page = bitmap_scan(swap_free, 0, 1, true);

  int i;
  for(i = 0; i < NUM_SECTORS_PER_PAGE; ++i) {
     block_write(swap_block, (swap_page * NUM_SECTORS_PER_PAGE) + i,
                 uaddr + (i * BLOCK_SECTOR_SIZE));
  }  

  bitmap_set(swap_free, swap_page, false);
  return swap_page;
}

void vm_swap_remove(int swap_page) {
  bitmap_set(swap_free, swap_page, true);
}

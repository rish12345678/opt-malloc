#define _DEFAULT_SOURCE
#define _BSD_SOURCE 
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <sys/mman.h>

#include <debug.h> // definition of debug_printf


#define HEAPSIZE 4096
#define MAGIC_NUM 0x12345678

// Think of the block struct as the header, at the start of every chunk of memory, free or not.
typedef struct block {
  size_t size;        // How many bytes beyond this metadata have been
                      // allocated for this block
  bool is_alloc;      // false = free, true = allocated
  int magic_num;
  struct block *next; // Where is the next block in the free list
  struct block *prev; // Where is the last block in the free list
} block_t;

// Think of the footer struct as the footer, at the end of every chunk of memory, free or not.
typedef struct footer {
  size_t size;        // How many bytes beyond this metadata have been
                      // allocated for this block
  bool is_alloc;      // false = free, true = allocated
  int magic_num;
} footer_t;

block_t* head = NULL;
static size_t page_size = 0;


void insert_block(block_t *block) {
  block_t *curr = head;
  block_t *prev = NULL;

  while (curr && curr < block) {
    prev = curr;
    curr = curr->next;
  }

  block->next = curr;
  if (prev) {
    prev->next = block;
  } else {
    head = block;
  }

  // Coalesce with the NEXT block in memory
  // Added sizeof(footer_t) to check adjacency properly
  if (block->next && (char*)block + sizeof(block_t) + block->size + sizeof(footer_t) == (char*)block->next) {
    debug_printf("free: coalesce blocks of size %zu and %zu to new block of size %zu\n", block->size, block->next->size, block->size + sizeof(block_t) + sizeof(footer_t) + block->next->size);
    
    // Size must include the swallowed header and footer!
    block->size += sizeof(block_t) + sizeof(footer_t) + block->next->size;
    block->next = block->next->next;
    
    // Update the new combined block's footer with the new size
    footer_t *merged_footer = (footer_t*)((char*)block + sizeof(block_t) + block->size);
    merged_footer->size = block->size;
  }

  // Coalesce with the PREV block in memory
  // Added sizeof(footer_t) to check adjacency properly
  if (prev && (char*)prev + sizeof(block_t) + prev->size + sizeof(footer_t) == (char*)block) {
    debug_printf("free: coalesce blocks of size %zu and %zu to new block of size %zu\n", prev->size, block->size, prev->size + sizeof(block_t) + sizeof(footer_t) + block->size);

    prev->size += sizeof(block_t) + sizeof(footer_t) + block->size;
    prev->next = block->next;
    
    // Update the new combined block's footer with the new size
    footer_t *merged_footer = (footer_t*)((char*)prev + sizeof(block_t) + prev->size);
    merged_footer->size = prev->size;
  }
}

void *mymalloc(size_t s) {
  // Given the user requests zero bytes, just return null
  if (s == 0) {
    return NULL;
  }

  // If the page_size variable has not been set yet, set it to the systems page size
  if (page_size == 0) {
    page_size = sysconf(_SC_PAGE_SIZE);
  }

  // Here we are checking that the size of the memory chunk we ought to return is aligned 
  // to eight bytes on this 64-bit architecture.  We do this to minimize the number of 
  // memory reads that the CPU has to do.  Giving out < 8 extra bytes per mymalloc is no 
  // hassle especially when optimizing for latency.
  int remainder = s % 8;

  if (remainder != 0) {
    s = s + (8 - remainder);
  }

  // Check to see if the requested memory will even fit in the page we will get via mmap
  size_t small_block_limit = page_size - sizeof(block_t) - sizeof(footer_t);

  if (s > small_block_limit) {
    size_t needed = s + sizeof(block_t) + sizeof(footer_t);
    size_t num_pages = (needed + page_size - 1) / page_size;
    size_t alloc_size = num_pages * page_size;

    debug_printf("malloc: large block - mmap region of size %zu\n", alloc_size);

    void *mem_ptr = mmap( // can be int*, char*, or whatever you want
      NULL, // address "hint" where to map the new page(s)
      alloc_size, // size of region (pages) to allocate
      PROT_READ | PROT_WRITE, // protection - permission flags
      MAP_ANONYMOUS | MAP_PRIVATE, // flags of mapping anonymous
                                    // (no file) and private (not
                                    // shared with children)
      -1, // fd of file to be mapped into the allocated pages
          // -1 because we are not mapping any file
      0   // offset from which to map the given file
    );

    if (mem_ptr == (void*) -1) {
      return NULL;
    }

    block_t *block = (block_t*) mem_ptr;
    block->size = alloc_size - sizeof(block_t) - sizeof(footer_t);
    block->is_alloc = true; // Mark as allocated for large block
    block->magic_num = MAGIC_NUM;
    block->next = NULL;
    block->prev = NULL;

    footer_t *footer = (footer_t*) ((char*) mem_ptr + sizeof(block_t) + block->size);
    footer->size = alloc_size - sizeof(block_t) - sizeof(footer_t);
    footer->is_alloc = true; // Mark as allocated
    footer->magic_num = MAGIC_NUM;

    return (void*) (block + 1);
  }

  block_t *curr = head;
  block_t *prev = NULL;

  // Here we loop over the free list, checking all free chunks of memory headers
  while (curr) {
    // If we find a chunk of free memory that is the size requested or larger....
    if (curr->size >= s) {
      debug_printf("malloc: block of size %zu found\n", curr->size);

      // Removed prev = curr->prev; wass vausing seg faults
      // Local tracking variable prev is already the correct predecessor in the loop.
      if (prev) {
        prev->next = curr->next;
      } else {
        head = curr->next;
      }

      if (curr->size >= s + sizeof(block_t) + 8 + sizeof(footer_t)) {
        block_t *new_block = (block_t*)((char*)curr + sizeof(block_t) + s + sizeof(footer_t));
        footer_t *footer_for_return_memory = (footer_t*)((char*)curr + sizeof(block_t) + s);
        footer_t *footer_for_extraneous_memory = (footer_t*)((char*)curr + sizeof(block_t) + curr->size);

        footer_for_extraneous_memory->size = curr->size - s - sizeof(block_t) - sizeof(footer_t);
        footer_for_extraneous_memory->is_alloc = false;

        footer_for_return_memory->size = s;
        footer_for_return_memory->is_alloc = true;
        footer_for_return_memory->magic_num = MAGIC_NUM;

        new_block->size = curr->size - s - sizeof(block_t) - sizeof(footer_t);
        new_block->is_alloc = false;
        new_block->magic_num = MAGIC_NUM;
        new_block->prev = NULL; 
        new_block->next = NULL;

        debug_printf("malloc: splitting - blocks of size %zu and %zu created\n", s, new_block->size);
                     
        curr->size = s;
        curr->is_alloc = true;
        insert_block(new_block);
      } else {
        curr->is_alloc = true;
        footer_t *footer_for_return_memory = (footer_t*)((char*)curr + sizeof(block_t) + curr->size);
        footer_for_return_memory->is_alloc = true;
      }
      return (void*) (curr + 1);
    }
    prev = curr;
    curr = curr->next;
  }

  debug_printf("malloc: block of size %zu not found - calling mmap\n", s);

  void *new_heap_start = mmap( // can be int*, char*, or whatever you want
    NULL, // address "hint" where to map the new page(s)
    page_size, // size of region (pages) to allocate
    PROT_READ | PROT_WRITE, // protection - permission flags
    MAP_ANONYMOUS | MAP_PRIVATE, // flags of mapping anonymous
                                  // (no file) and private (not
                                  // shared with children)
    -1, // fd of file to be mapped into the allocated pages
        // -1 because we are not mapping any file
    0   // offset from which to map the given file
  );

  if (new_heap_start == (void*) -1) {
    return NULL;
  }

  block_t *block = (block_t*) new_heap_start;
  block->size = page_size - sizeof(block_t) - sizeof(footer_t);

  if (block->size >= s + sizeof(footer_t) + sizeof(block_t) + 8 + sizeof(footer_t)) {
    block_t *new_block = (block_t*)((char*)block + sizeof(block_t) + s + sizeof(footer_t));
    new_block->size = block->size - s - sizeof(block_t) - sizeof(footer_t);
    new_block->is_alloc = false;
    new_block->magic_num = MAGIC_NUM;

    footer_t *new_block_footer = (footer_t*)((char*)new_block + sizeof(block_t) + new_block->size);
    new_block_footer->size = new_block->size;
    new_block_footer->is_alloc = false;
    new_block_footer->magic_num = MAGIC_NUM;

    debug_printf("malloc: splitting - blocks of size %zu and %zu created\n", s, new_block->size);

    block->size = s;
    block->is_alloc = true;

    footer_t *block_footer = (footer_t*)((char*)block + sizeof(block_t) + block->size);
    block_footer->size = block->size;
    block_footer->is_alloc = true;
    block_footer->magic_num = MAGIC_NUM;

    insert_block(new_block);
  } else {
    block->is_alloc = true;
    footer_t *block_footer = (footer_t*)((char*)block + sizeof(block_t) + block->size);
    block_footer->size = block->size;
    block_footer->is_alloc = true;
    block_footer->magic_num = MAGIC_NUM;
  }
  
  return (void*)(block + 1);
}

void *mycalloc(size_t nmemb, size_t s) {
  size_t total = nmemb * s;
  void *ptr = mymalloc(total);
  if (!ptr) {
    return NULL;
  }

  // Just like the Calloc system call does
  memset(ptr, 0, total);
  return ptr;
}

void myfree(void *ptr) {
  if (!ptr) {
    return;
  }

  if (page_size == 0) {
    page_size = sysconf(_SC_PAGE_SIZE);
  }

  block_t *block = (block_t*)ptr - 1;

  // Set as unallocated right away
  block->is_alloc = false;
  footer_t *block_footer = (footer_t*)((char*)block + sizeof(block_t) + block->size);
  block_footer->is_alloc = false;

  // Fixed large block condition to include footer size
  if (block->size + sizeof(block_t) + sizeof(footer_t) > page_size) {
    size_t region_size = block->size + sizeof(block_t) + sizeof(footer_t);
    debug_printf("free: munmap region of size %zu\n", region_size);
    munmap(block, region_size);
  } else {
    insert_block(block);

    int empty_pages = 0;
    block_t *curr = head;
    block_t *prev = NULL;

    while (curr) {
      // Fixed empty page condition to include footer
      if (curr->size == page_size - sizeof(block_t) - sizeof(footer_t)) {
        empty_pages++;
        if (empty_pages > 2) {
          block_t *to_free = curr;
          if (prev) {
            prev->next = curr->next;
          } else {
            head = curr->next;
          }
          
          curr = curr->next;
          
          // Fixed munmap size to include footer
          size_t region_size = to_free->size + sizeof(block_t) + sizeof(footer_t);
          debug_printf("free: munmap region of size %zu\n", region_size);
          munmap(to_free, region_size);
          continue;
        }
      }
      prev = curr;
      curr = curr->next;
    }
  }
}
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <stdint.h>


#include <debug.h>

#ifdef USE_BEST_FIT
#define STRATEGY "BEST-FIT"
#else
#define STRATEGY "FIRST-FIT"
#endif

#define HEAPSIZE  4096
#define MAGIC_NUM 0x12345678

// Header placed at the start of every block, free or allocated.
typedef struct block {
	size_t size;         // Payload
	bool is_alloc;     	 // false = free, true = alloced
	int magic_num;    	 // Checking for illegal frees causing memory corruption
	struct block *next;
} block_t;

// Footer placed at the end of every block, whether free or allocated.
// Should mirror the header exactly(for specified values) easy to coalesce
// with direct left neighbor in memory.
typedef struct footer {		// block(header) mirror
	size_t size;
	bool is_alloc;
	int magic_num;
} footer_t;

block_t *head = NULL;
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

	// Forward coalesce, if next blc adjacent phsycially
	if (block->next && (char*)block + sizeof(block_t) + block->size + sizeof(footer_t) == (char*)block->next) {

		debug_printf("free: coalesce forward");

		block->size += sizeof(block_t) + sizeof(footer_t) + block->next->size;
		block->next  = block->next->next;

		footer_t *merged = (footer_t*)((char*)block + sizeof(block_t) + block->size);
		merged->size = block->size;
	}

	// Backward coalesce: merge with previous block if physically adjacent
	if (prev && (char*)prev + sizeof(block_t) + prev->size + sizeof(footer_t) == (char*)block) {

		debug_printf("free: coalesce backwards");

		prev->size += sizeof(block_t) + sizeof(footer_t) + block->size;
		prev->next  = block->next;

		footer_t *merged = (footer_t*)((char*)prev + sizeof(block_t) + prev->size);
		merged->size = prev->size;
	}
}

// Unlink 'chosen' from the free list, splits a remainder block if the block size is 
// big enough and returns payload.
static void *split_and_return(block_t *chosen, block_t *chosen_prev, size_t s) {
	// Unlink from the free list
	if (chosen_prev) {
		chosen_prev->next = chosen->next;
	} else {
		head = chosen->next;
	}

	if (chosen->size >= s + sizeof(block_t) + 8 + sizeof(footer_t)) {

		// Footer for allocated ppart returned
		footer_t *footer_alloc = (footer_t*)((char*)chosen + sizeof(block_t) + s);
		footer_alloc->size = s;
		footer_alloc->is_alloc = true;
		footer_alloc->magic_num = MAGIC_NUM;

		// New suprplus free block gotten from remainder
		block_t *remainder = (block_t*)((char*)chosen + sizeof(block_t) + s + sizeof(footer_t));
		remainder->size = chosen->size - s - sizeof(block_t) - sizeof(footer_t);
		remainder->is_alloc = false;
		remainder->magic_num = MAGIC_NUM;
		remainder->next = NULL;

		// Footer for surplus free block
		footer_t *footer_rem = (footer_t*)((char*)chosen + sizeof(block_t) + chosen->size);
		footer_rem->size = remainder->size;
		footer_rem->is_alloc = false;
		footer_rem->magic_num = MAGIC_NUM;

		debug_printf("Split + alloc + free surplus");

		chosen->size = s;
		chosen->is_alloc = true;
		insert_block(remainder);

	} else {
		// No splitting is reqed, just use the whole block
		chosen->is_alloc = true;
		footer_t *footer = (footer_t*)((char*)chosen + sizeof(block_t) + chosen->size);
		footer->is_alloc = true;
	}

	return (void*)(chosen + 1);
}


void *mymalloc(size_t s) {
	if (s == 0) return NULL;

	if (page_size == 0) page_size = sysconf(_SC_PAGE_SIZE);

	// Round up to 8-byte alignment for reduced CPU cycles
	int rem = s % 8;
	if (rem != 0) s += (8 - rem);


	size_t small_limit = page_size - sizeof(block_t) - sizeof(footer_t);

	// For 'large' blocks
	if (s > small_limit) {
		size_t needed = s + sizeof(block_t) + sizeof(footer_t);
		size_t num_pages = (needed + page_size - 1) / page_size;
		size_t alloc_size = num_pages * page_size;

		debug_printf("malloc: large block mmap %zu bytes\n", alloc_size);

		void *mem = mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (mem == (void*)-1) return NULL;

		block_t *block = (block_t*)mem;
		block->size = alloc_size - sizeof(block_t) - sizeof(footer_t);
		block->is_alloc = true;
		block->magic_num = MAGIC_NUM;
		block->next = NULL;

		footer_t *footer = (footer_t*)((char*)mem + sizeof(block_t) + block->size);
		footer->size = block->size;
		footer->is_alloc = true;
		footer->magic_num = MAGIC_NUM;

		return (void*)(block + 1);
	}


// Now we can search then free list for the desired block

#ifdef USE_BEST_FIT
	/*
	
		BEST-FIT STRAT:

		Scan the entire list and then choose the smallest block that is still
		large enough for the request size.
		This strategy minimises wasted space in the chosen block (minimizing  internal
		fragmentation on every alloc) and keeps 
		larger free blocks intact for future use (minimizing external 
		fragmentation over time).
		The cost is that every allocation takes O(n) time, n being the number 
		of blocks in the free list, regardless of where the first fit is.
	*/
	block_t *best = NULL;
	block_t *best_prev = NULL;
	block_t *curr = head;
	block_t *prev = NULL;

	while (curr) {
		if (curr->size >= s) {
			if (best == NULL || curr->size < best->size) {
				best = curr;
				best_prev = prev;
			}
		}
		prev = curr;
		curr = curr->next;
	}

	if (best) {
		debug_printf("B-F malloc: chose block size %zu for request %zu\n", best->size, s);
		return split_and_return(best, best_prev, s);
	}

#else

	/*
	
		FIRST-FIT STRAT:

		Take first block that is large enough.
		Although this is stil O(n) worst case, every allocation will not be worst case, like
		 the first one.  Instead it will be more like on 
		average (O(k), k = index of first fit)


	*/

	block_t *curr = head;
	block_t *prev = NULL;

	while (curr) {
		if (curr->size >= s) {
			debug_printf("F-F malloc: chose block size %zu for request %zu\n", curr->size, s);
			return split_and_return(curr, prev, s);
		}
		prev = curr;
		curr = curr->next;
	}
#endif

	// When there are no suitable free blocks, we will request a new page from OS
	debug_printf("malloc: no block found, mmaping new page\n");

	void *new_page = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (new_page == (void*)-1) return NULL;

	block_t *block = (block_t*)new_page;
	block->size = page_size - sizeof(block_t) - sizeof(footer_t);
	block->is_alloc = false;
	block->magic_num = MAGIC_NUM;
	block->next = NULL;

	footer_t *footer = (footer_t*)((char*)block + sizeof(block_t) + block->size);
	footer->size = block->size;
	footer->is_alloc = false;
	footer->magic_num = MAGIC_NUM;

	// Insert the whole page as a free block, then allocate from it
	insert_block(block);

	// Find this block in the list (it is the only fitting block)
	block_t *fit = head;
	block_t *fit_prev = NULL;
	while (fit && fit->size < s) {
		fit_prev = fit;
		fit = fit->next;
	}

	return split_and_return(fit, fit_prev, s);
}



void *mycalloc(size_t nmemb, size_t s) {
	// overflow guard!!
	if (nmemb != 0 && s > SIZE_MAX / nmemb) return NULL;

	size_t total = nmemb * s;
	void *ptr = mymalloc(total);
	if (!ptr) return NULL;

	memset(ptr, 0, total);
	return ptr;
}



void myfree(void *ptr) {
	if (!ptr) return;

	if (page_size == 0) page_size = sysconf(_SC_PAGE_SIZE);

	// The payload starts at one header size past the start of the header
	block_t *block = (block_t*)ptr - 1;

	block->is_alloc = false;
	footer_t *block_footer = (footer_t*)((char*)block + sizeof(block_t) + block->size);
	block_footer->is_alloc = false;

	// Large block — standalone mmap allocation, return directly to OS
	if (block->size + sizeof(block_t) + sizeof(footer_t) > page_size) {
		size_t region_size = block->size + sizeof(block_t) + sizeof(footer_t);
		debug_printf("free: munmap large block %zu bytes\n", region_size);
		munmap(block, region_size);
		return;
	}

	// Normal block — coalesce and return to free list
	insert_block(block);

	// Scan for completely empty pages; return any beyond ur two page cache
	int empty_pages = 0;
	block_t *curr = head;
	block_t *prev = NULL;

	while (curr) {
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

				size_t region_size = to_free->size + sizeof(block_t) + sizeof(footer_t);
				debug_printf("free: munmap excess empty page %zu bytes\n", region_size);
				munmap(to_free, region_size);
				continue;
			}
		}
		prev = curr;
		curr = curr->next;
	}
}

# Custom Memory Allocator in C

A heap allocator built from scratch in C on Linux. No `stdlib.h`. Memory comes directly from the kernel via `mmap` and is returned via `munmap`. Everything in between is implemented by hand.

---

## What It Does

Implements `mymalloc`, `myfree`, and `mycalloc` as drop-in replacements for the standard library equivalents. Supports two allocation strategies selectable at compile time.

---

## How It Works

Every block of memory, whether free or allocated, has a header and a footer:

```
[ block_t header | payload | footer_t ]
```

The header stores the block size, allocation state, a corruption sentinel, and a free list pointer. The footer mirrors the size and allocation state.

The footer is what makes backward coalescing O(1). When a block is freed, the left neighbor's footer sits directly before the current block's header in memory. No list traversal needed — the neighbor is found by arithmetic.

The free list is singly-linked and kept sorted by memory address. Address ordering is what makes the adjacency checks in coalescing work correctly.

---

## Allocation Strategies

Toggle at compile time with `-DUSE_BEST_FIT`. Default is First-Fit.

**First-Fit** stops at the first free block large enough to satisfy the request. Average case O(k) where k is how far into the list the first fit lands.

**Best-Fit** scans the entire free list and picks the smallest block that still fits. Always O(n). Tends to preserve larger contiguous regions for future requests.

---

## Implementation Details

- All returned pointers are 8-byte aligned
- Free blocks are split when the remainder is large enough to hold a complete new block (header + 8 bytes minimum payload + footer)
- Forward and backward coalescing runs on every free
- Requests larger than one page get a private `mmap` region and bypass the free list entirely, returned directly to the OS on free
- Up to 2 empty pages are kept cached to avoid `mmap`/`munmap` thrashing on high-frequency alloc/free patterns
- Every header and footer carries magic number `0x12345678` as a corruption sentinel
- `mycalloc` guards against silent integer overflow before multiplying `nmemb * size`

---

## Benchmark Results

10,000 randomized alloc/verify/free operations. Allocation sizes 1-2048 bytes, up to 500 pointers live simultaneously. Fixed random seed across both runs for a direct comparison.

| Metric | First-Fit | Best-Fit |
|---|---|---|
| Allocations | 5,122 | 5,122 |
| Frees | 4,878 | 4,878 |
| Corruption detected | None | None |
| Wall time | 0.0759 s | 0.0904 s |
| Avg time / op | 5,474 ns | 6,610 ns |
| Min op time | 60 ns | 80 ns |
| Max op time | 8,133,732 ns | 5,052,390 ns |
| Fragmentation mean | 0.2040 | 0.2040 |
| Fragmentation min | 0.1925 | 0.1925 |
| Fragmentation max | 0.2104 | 0.2104 |

**What the numbers say:**

First-Fit is 21% faster on average (5,474 ns vs 6,610 ns) because it stops scanning as soon as it finds a fit. Best-Fit always pays the full O(n) scan cost.

Fragmentation is identical across both strategies under this workload. That is expected. Aggressive coalescing consolidates free memory after every free regardless of which block was chosen, so the strategy difference only shows up under adversarial allocation patterns where block size distribution is heavily skewed.

The fragmentation measurement works by binary searching for the largest allocation that currently succeeds and comparing it to total live bytes. A ratio of 0.20 means roughly 20% of free memory is stranded in chunks too small to satisfy a large contiguous request. For a general-purpose allocator under a random workload, that is a healthy result.

---

## Files

| File | Description |
|---|---|
| `mymalloc.c` | Allocator implementation |
| `stress_test.c` | Correctness verification, timing, and fragmentation benchmarks |
| `Makefile` | Builds both strategies, separate debug targets |
| `debug.h` | `debug_printf` macro, silenced with `-DSHUSH` |

---

## Build and Run

```bash
make            # build both strategies
make run        # run both and compare output side by side
make run_ff     # first-fit only
make run_bf     # best-fit only
make debug      # build with debug_printf active (coalesce and split tracing)
make clean
```

---

## Possible Extensions

- Segregated size-class free lists to bring average allocation time closer to O(1)
- Mutex around `head` and `mmap` calls for thread safety
- Heap consistency checker that walks every block after each operation, verifying header/footer agreement, magic numbers, and no adjacent uncoalesced free blocks
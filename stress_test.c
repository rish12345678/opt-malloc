#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

// Testing these allcoator functions
void *mymalloc(size_t size);
void  myfree(void *ptr);
void *mycalloc(size_t nmemb, size_t size);

/*

Fragmentation Benchmarking

Measures how fragmented the heap is at a given point in the test.

Since we can't peek inside the allocator's free list from here, we
approximate by binary searching for the largest allocation that currently
succeeds. We then compare that to the total bytes held by live allocations
to get a ratio between 0 and 1.

A ratio near 0 means free memory is mostly in one big chunk, which is good.
Near 1 means free memory is scattered in tiny pieces, which is not so good for future 
allocations

*/

#define NUM_ITERATIONS  10000
#define MAX_PTRS        500
#define MAX_ALLOC_SIZE  2048

// Num iteratiosn to sample fragmentation
#define FRAG_SAMPLE_INTERVAL 1000

typedef struct {
    void *ptr;
    size_t size;
    uint8_t pattern;
} alloc_info_t;



/*

We are going to figure out how fragmented the heap is at this point. We binary search for the
largest allocation that currently succeeds, then compate it against the
total live bytes to produce a ratio, again, from 0 to 1.

*/


static double fragmentation_ratio(const alloc_info_t *allocations, size_t live_bytes) {
    // Binary search to find the largest block size that can satisfy mymalloc


    size_t lo = 1, hi = MAX_ALLOC_SIZE * MAX_PTRS, best = 0;

    while (lo <= hi) {
        size_t mid = lo + (hi - lo) / 2;
        void *p = mymalloc(mid);
        if (p) {
            best = mid;
            myfree(p);
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    // If the heap is empty
    if (best == 0 && live_bytes == 0) return 0.0;

    double total = (double)(best + live_bytes);
    return 1.0 - ((double)best / total);
}

/*

Run the actual stress test:

Send the allocator a bunch of random alloc + verify + free operations, and we check for
corruption on every free, sample fragmentation periodically, and print a
full timing and fragmentation report when completed.

*/
static void run_stress_test(const char *strategy_name) {
    alloc_info_t allocations[MAX_PTRS];
    memset(allocations, 0, sizeof(allocations));

    // Frag samples
    double frag_samples[NUM_ITERATIONS / FRAG_SAMPLE_INTERVAL + 1];
    int frag_count = 0;

    // Per-operation timing in nanoseconds
    long op_times[NUM_ITERATIONS];
    long total_allocs = 0;
    long total_frees = 0;

    struct timespec t0, t1, wall0, wall1;
    clock_gettime(CLOCK_MONOTONIC, &wall0);

    printf("----------------------------------------------------------\n");
    printf("  Strategy : %s\n", strategy_name);
    printf("  Iterations: %d   Max live ptrs: %d   Max alloc: %d bytes\n",
           NUM_ITERATIONS, MAX_PTRS, MAX_ALLOC_SIZE);
    printf("----------------------------------------------------------\n");

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        int index = rand() % MAX_PTRS;

        clock_gettime(CLOCK_MONOTONIC, &t0);

        if (allocations[index].ptr == NULL) {
            // Allocate
            size_t sz = (size_t)(rand() % MAX_ALLOC_SIZE) + 1;
            uint8_t pattern = (uint8_t)(rand() % 256);

            void *p = mymalloc(sz);
            clock_gettime(CLOCK_MONOTONIC, &t1);

            if (p) {
                allocations[index].ptr = p;
                allocations[index].size = sz;
                allocations[index].pattern = pattern;
                memset(p, pattern, sz);
                total_allocs++;
            }
        } else {
            // Verify and free
            uint8_t *p = (uint8_t *)allocations[index].ptr;
            for (size_t j = 0; j < allocations[index].size; j++) {
                if (p[j] != allocations[index].pattern) {
                    fprintf(stderr, "\n[FATAL] CORRUPTION at slot %d offset %zu (expected 0x%02X got 0x%02X)\n", index, j, allocations[index].pattern, p[j]);
                    exit(1);
                }
            }
            myfree(allocations[index].ptr);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            allocations[index].ptr = NULL;
            total_frees++;
        }

        op_times[i] = (long)(t1.tv_sec - t0.tv_sec) * 1000000000L + (t1.tv_nsec - t0.tv_nsec);

        // Frag Sample
        if ((i + 1) % FRAG_SAMPLE_INTERVAL == 0) {
            // Count live bytes
            size_t live = 0;
            for (int k = 0; k < MAX_PTRS; k++) {
                if (allocations[k].ptr) live += allocations[k].size;
            }

            double fr = fragmentation_ratio(allocations, live);
            frag_samples[frag_count++] = fr;
            printf("  [iter %5d]  frag ratio: %.4f  live bytes: %zu\n", i + 1, fr, live);
        }
    }

    // Clean up remaining live allocations
    for (int i = 0; i < MAX_PTRS; i++)
        if (allocations[i].ptr) myfree(allocations[i].ptr);

    clock_gettime(CLOCK_MONOTONIC, &wall1);

    // Statistics
    double wall_sec = (wall1.tv_sec  - wall0.tv_sec) + (wall1.tv_nsec - wall0.tv_nsec) / 1e9;

    // Compute min/max/mean op time
    long sum = 0, min_t = op_times[0], max_t = op_times[0];
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        sum += op_times[i];
        if (op_times[i] < min_t) min_t = op_times[i];
        if (op_times[i] > max_t) max_t = op_times[i];
    }
    double mean_ns = (double)sum / NUM_ITERATIONS;

    // Fragmentation stats
    double frag_sum = 0, frag_min = frag_samples[0], frag_max = frag_samples[0];
    for (int i = 0; i < frag_count; i++) {
        frag_sum += frag_samples[i];
        if (frag_samples[i] < frag_min) frag_min = frag_samples[i];
        if (frag_samples[i] > frag_max) frag_max = frag_samples[i];
    }
    double frag_mean = frag_count > 0 ? frag_sum / frag_count : 0.0;

    printf("\n                Results                      \n");
    printf("  Allocations performed : %ld\n",  total_allocs);
    printf("  Frees performed       : %ld\n",  total_frees);
    printf("  Corruption detected   : None \n");
    printf("\n");
    printf("  Wall time             : %.4f s\n",  wall_sec);
    printf("  Avg time / op         : %.2f ns  (%.4f ms)\n",
           mean_ns, mean_ns / 1e6);
    printf("  Min op time           : %ld ns\n",  min_t);
    printf("  Max op time           : %ld ns\n",  max_t);
    printf("\n");
    printf("  Fragmentation ratio   : mean=%.4f  min=%.4f  max=%.4f\n",
           frag_mean, frag_min, frag_max);
    printf("  (0.0 = no fragmentation, 1.0 = fully fragmented)\n");
    printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n");
}

int main(void) {
    srand(32);

#ifdef USE_BEST_FIT
    const char *strategy = "BEST-FIT";
#else
    const char *strategy = "FIRST-FIT";
#endif

    printf("\n--------------\n");
    printf("  Stress Test\n");
    printf("---------------\n\n");

    run_stress_test(strategy);

    return 0;
}
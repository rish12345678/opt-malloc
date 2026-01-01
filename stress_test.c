#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <string.h>

// Prototypes for your allocator
void *mymalloc(size_t size);
void myfree(void *ptr);
void *mycalloc(size_t nmemb, size_t size);

#define NUM_ITERATIONS 10000
#define MAX_PTRS 500
#define MAX_ALLOC_SIZE 2048

typedef struct {
    void *ptr;
    size_t size;
    uint8_t pattern;
} alloc_info_t;

int main() {
    alloc_info_t allocations[MAX_PTRS];
    memset(allocations, 0, sizeof(allocations));

    srand(time(NULL));
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    printf("Starting Stress Test: %d iterations...\n", NUM_ITERATIONS);

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        int index = rand() % MAX_PTRS;

        if (allocations[index].ptr == NULL) {
            // ALLOCATE
            size_t size = (rand() % MAX_ALLOC_SIZE) + 1;
            uint8_t pattern = rand() % 256;

            void *p = mymalloc(size);
            if (p) {
                allocations[index].ptr = p;
                allocations[index].size = size;
                allocations[index].pattern = pattern;
                // Fill with pattern to check for corruption later
                memset(p, pattern, size);
            }
        } else {
            // VERIFY AND FREE
            uint8_t *p = (uint8_t *)allocations[index].ptr;
            for (size_t j = 0; j < allocations[index].size; j++) {
                if (p[j] != allocations[index].pattern) {
                    fprintf(stderr, "CORRUPTION DETECTED at index %d!\n", index);
                    exit(1);
                }
            }
            myfree(allocations[index].ptr);
            allocations[index].ptr = NULL;
        }
    }

    // Clean up remaining
    for (int i = 0; i < MAX_PTRS; i++) {
        if (allocations[i].ptr) myfree(allocations[i].ptr);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double time_taken = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("Test Passed Successfully!\n");
    printf("Total Time: %.4f seconds\n", time_taken);
    printf("Avg Time per Op: %.6f ms\n", (time_taken / NUM_ITERATIONS) * 1000);

    return 0;
}

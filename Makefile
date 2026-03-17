CC     = gcc
CFLAGS = -g -Wall -Werror -std=gnu11 -I. -DSHUSH

DEBUG_FLAGS =

# Primary targets
#
#   make -> builds both first-fit and best-fit binaries
#   make run -> runs both and prints results back-to-back
#   make run_ff -> runs first-fit only
#   make run_bf -> runs best-fit only
#   make debug -> builds both with debug_printf enabled
#   make clean -> removes all generated files


all: stress_ff stress_bf


# Build First-Fit
mymalloc_ff.o: mymalloc.c
	$(CC) $(CFLAGS) -c mymalloc.c -o mymalloc_ff.o

stress_ff: stress_test.c mymalloc_ff.o
	$(CC) $(CFLAGS) stress_test.c mymalloc_ff.o -o stress_ff
	@echo "Built: stress_ff  (first-fit)"


# Build Best-Fit
mymalloc_bf.o: mymalloc.c
	$(CC) $(CFLAGS) -DUSE_BEST_FIT -c mymalloc.c -o mymalloc_bf.o

stress_bf: stress_test.c mymalloc_bf.o
	$(CC) $(CFLAGS) -DUSE_BEST_FIT stress_test.c mymalloc_bf.o -o stress_bf
	@echo "Built: stress_bf  (best-fit)"


debug: mymalloc.c stress_test.c
	$(CC) -g -Wall -Werror -std=gnu11 -I. -c mymalloc.c -o mymalloc_ff_dbg.o
	$(CC) -g -Wall -Werror -std=gnu11 -I. stress_test.c mymalloc_ff_dbg.o -o stress_ff_debug
	$(CC) -g -Wall -Werror -std=gnu11 -I. -DUSE_BEST_FIT -c mymalloc.c -o mymalloc_bf_dbg.o
	$(CC) -g -Wall -Werror -std=gnu11 -I. -DUSE_BEST_FIT stress_test.c mymalloc_bf_dbg.o -o stress_bf_debug
	@echo "Built: stress_ff_debug  stress_bf_debug  (with debug_printf)"


# Run targets
run: stress_ff stress_bf
	@echo "Running FIRST-FIT"
	./stress_ff
	@echo "Running BEST-FIT"
	./stress_bf

run_ff: stress_ff
	./stress_ff

run_bf: stress_bf
	./stress_bf



clean:
	rm -f *.o stress_ff stress_bf stress_ff_debug stress_bf_debug

.PHONY: all debug run run_ff run_bf clean
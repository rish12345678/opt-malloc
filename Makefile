CC = gcc
CFLAGS = -g -Wall -Werror -std=gnu11 -I.
# Remove -DDEBUG if you don't want your debug_printf to slow down the speed test
DEBUG_FLAGS = -DDEBUG_PRINT 

all: stress_test

# Compile the allocator object
mymalloc.o: mymalloc.c
	$(CC) $(CFLAGS) -c mymalloc.c -o mymalloc.o

# Compile the stress test and link with your allocator
stress_test: stress_test.c mymalloc.o
	$(CC) $(CFLAGS) stress_test.c mymalloc.o -o stress_test

# Run the test
run: stress_test
	./stress_test

clean:
	rm -f *.o stress_test
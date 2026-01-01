# Opt_Malloc

This is where I will optimize the my_malloc implemented in Assignment 6 of cs3650: Computer Systems.

The [Makefile](Makefile) contains the following targets:

- `make all` - compile [mymalloc.c](mymalloc.c) into the object file `mymalloc.o`
- `make test` - compile and run tests in the [tests](tests/) directory with `mymalloc.o`.
- `make demo` - compile and run tests in the tests directory with standard malloc.
- `make clean` - perform a minimal clean-up of the source tree
- `make help` - print available targets


Code explanation pre-optimizations:


Code explanation post-optimizations:

The problem: The regular class assignment code had a critical problem.  If the user tried to pass in a pointer for free that pointed to the middle of a block, then we would backtrack to what we think to be the header, and read what we think is the size of the block we presume follows.  This can make our free-list very messy corrupting data, which was not originally accounted for, as we assumed the users of mymalloc would only free exactly the pointer that was given to them when they called mymalloc.

The solution: To solve this I did some research and found the common use of magic numbers, which I implemented into my struct to assure the validity that they are actually trying to free a valid block of memory, with a pointer to exactly where in memory they received their chunk of memory through mymalloc.


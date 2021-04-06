#pragma once

#define OFFS_FREE_BLOCKS 0
extern void*mem;
extern int sz;

void buddy_init(void *space, int block_num); //initialize buddy allocator
void* buddy_alloc(int size); //allocate space of size*BLOCK_SIZE
void buddy_free(void*block, int size); //free space of size*BLOCKSIZE, from address block
void* slabAllocator(); //returns pointer to kernel space that is owned by slab
void* endOfSpace(); //returns address after the end of available space
int greqexp2(int s);
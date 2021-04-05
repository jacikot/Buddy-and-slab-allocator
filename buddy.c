#include "buddy.h"
#include "slab.h"
//#include <iostream>
//using namespace std;
void*mem;
int sz;
int leeqexp2(int s) {
	if (s <= 0) return -1;
	int j = 0;
	while ((1 << (j + 1)) <= s) j++;
	return j;
}

int greqexp2(int s) {
	if (s <= 0) return -1;
	int j = 0;
	while ((1 << j) < s) j++;
	return j;
}

void* kernelBlock() {
	return (char*)mem + sz * BLOCK_SIZE; //last block, controlled by kernel
}

void** freeBlockArray() {
	return (void**)((char*)kernelBlock() + OFFS_FREE_BLOCKS);
}

void* slabAllocator() {
	return (char*)freeBlockArray() + (leeqexp2(sz) + 1) * sizeof(void*);
}

unsigned blockNum(void*b) {
	return (unsigned)((char*)b - (char*)mem) / BLOCK_SIZE;
}

void* endOfSpace() {
	return (char*)mem + (sz + 1)*BLOCK_SIZE;
}

void buddy_init(void *space, int block_num) {
	mem = space;
	sz = block_num - 1;
	void**freeBlocks = freeBlockArray();
	int exp;
	int curExp = exp = leeqexp2(sz);
	int curSize = sz;
	void* curBlock = mem;
	for (int i = 0; i <= curExp; i++) {
		freeBlocks[i] = 0;
	}

	while (curSize > 0) {
		freeBlocks[curExp] = curBlock;
		*((void**)curBlock) = 0;
		curSize -= 1 << curExp;
		curBlock = (char*)curBlock + (1 << curExp)*BLOCK_SIZE;
		curExp = leeqexp2(curSize);
	}

}

void insert(void**freeBlocks, int exp, void*block) {
	*((void**)block) = freeBlocks[exp];
	freeBlocks[exp] = block;
}

void divideAndConnect(void *base, int partExp, void**freeBlocks) {
	void* part = (char*)base + (1 << partExp)*BLOCK_SIZE;
	insert(freeBlocks, partExp, part);
}

void* buddy_alloc(int size) {
	if (size <= 0) return 0;
	int exp = greqexp2(size);
	if ((1 << exp) > sz)return 0;
	void**freeBlocks = freeBlockArray();
	if (freeBlocks[exp] != 0) {
		void*ret = freeBlocks[exp];
		freeBlocks[exp] = *((void**)ret);
		return ret;
	}
	for (int i = exp + 1; ((1 << i) <= sz); i++) {
		if (freeBlocks[i] != 0) {
			void*ret = freeBlocks[i];
			freeBlocks[i] = *((void**)ret);
			int j = i - 1;
			while (j >= exp) {
				divideAndConnect(ret, j, freeBlocks);
				j--;
			}
			return ret;
		}
	}
	return 0;

}
void buddy_free(void*block, int size) {
	int exp = leeqexp2(size);
	void*buddyBlock = 0;
	while ((1 << exp) <= sz) {
		if (blockNum(block) % (1 << (exp + 1)) == 0) {
			buddyBlock = (char*)block + BLOCK_SIZE * (1 << exp);
		}
		else {
			buddyBlock = (char*)block - BLOCK_SIZE * (1 << exp);
		}

		void**freeBlocks = freeBlockArray();
		void*cur = freeBlocks[exp];
		void*prev = 0;
		while (cur != 0) {
			if (cur == buddyBlock) {

				if (prev != 0) *((void**)prev) = *((void**)cur);
				else freeBlocks[exp] = *((void**)cur);

				if (block > buddyBlock) block = buddyBlock;
				exp += 1;
				break;
			}
			prev = cur;
			cur = *((void**)cur);
		}
		if (cur == 0) {
			insert(freeBlocks, exp, block); return;
		}

	}
}
void buddy_free_r(void*block, int size) {
	int exp = leeqexp2(size);
	void*buddyBlock = 0;
	if (blockNum(block) % (1 << (exp + 1)) == 0) {
		buddyBlock = (char*)block + BLOCK_SIZE * (1 << exp);
	}
	else {
		buddyBlock = (char*)block - BLOCK_SIZE * (1 << exp);
	}

	void**freeBlocks = freeBlockArray();
	void*cur = freeBlocks[exp];
	void*prev = 0;
	while (cur != 0) {
		if (cur == buddyBlock) {

			if (prev != 0) *((void**)prev) = *((void**)cur);
			else freeBlocks[exp] = *((void**)cur);

			/*if (block < buddyBlock) insert(freeBlocks, exp+1, block);
			else insert(freeBlocks, exp + 1, buddyBlock);*/
			if (block < buddyBlock) buddy_free(block, 1 << (exp + 1));
			else buddy_free(buddyBlock, 1 << (exp + 1));
			return;
		}
		prev = cur;
		cur = *((void**)cur);
	}
	insert(freeBlocks, exp, block);

}
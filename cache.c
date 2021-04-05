#include "slab.h"
#include "buddy.h"
#include "cache.h"
#include "lock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


unsigned maxNumOfCaches(void*h, void*e) {
	unsigned max = (unsigned)((char*)e - ((char*)h + sizeof(cache_head))) / sizeof(kmem_cache_descriptor);
	return max;
}

cache_head*cacheHeadersList() {
	void*slab = slabAllocator();
	return *((cache_head**)((char*)slab + sizeof(HANDLE)));
}

kmem_cache_descriptor**bufferCaches() {
	return (kmem_cache_descriptor**)((char*)slabAllocator() + sizeof(HANDLE) + sizeof(cache_head*));
}





void cacheHeaderBlockInit(cache_head*head, void*end) {
	head->cacheHeaderArray = (kmem_cache_descriptor*)((char*)head + sizeof(cache_head));
	head->alloc = -1;
	head->free = 0;
	head->next = 0;
	head->max = maxNumOfCaches(head, end);
	for (int i = 0; i < head->max - 1; i++) {
		head->cacheHeaderArray[i].next = i + 1;
	}
	head->cacheHeaderArray[head->max - 1].next = -1;
}
int optimizeSlabSize(unsigned size) {

	int sizeInBlk = (size * MINIMUM_OBJ_IN_SLAB) / BLOCK_SIZE + ((((size*MINIMUM_OBJ_IN_SLAB) % BLOCK_SIZE) > 0) ? 1 : 0);
	int exp = greqexp2(sizeInBlk);
	int bestExp = exp;
	int leastUnusable;
	int slabSize = (1 << exp)*BLOCK_SIZE - sizeof(kmem_slab);
	int unusable = leastUnusable = slabSize - slabSize / size * size;
	while ((slabSize / size) < 16) {
		if (leastUnusable > unusable) {
			bestExp = exp;
			leastUnusable = unusable;
		}
		exp++;
		slabSize = (1 << exp)*BLOCK_SIZE - sizeof(kmem_slab);
		unusable = slabSize - slabSize / size * size;
	}
	slabSize = (1 << bestExp)*BLOCK_SIZE - sizeof(kmem_slab);
	return slabSize;

}
void cacheInit(kmem_cache_descriptor*newCache, const char *name, unsigned size, void(*ctor)(void *), void(*dtor)(void *)) {
	newCache->name = name;
	newCache->dataSize = size;
	newCache->ctor = ctor;
	newCache->dtor = dtor;
	newCache->slabSize = optimizeSlabSize(size);
	newCache->slabNum = 0;
	newCache->objInSlab = newCache->slabSize / size;
	
	newCache->numOfIntForBitVector = (newCache->objInSlab / 32 + ((newCache->objInSlab % 32) ? 1 : 0));
	int unused = newCache->slabSize - newCache->objInSlab *size;
	while (unused < newCache->numOfIntForBitVector * sizeof(int)) {
		newCache->objInSlab--;
		unused = newCache->slabSize - newCache->objInSlab *size;
	}
	
	newCache->numOfFilled = 0;
	newCache->errCode = 0;
	newCache->prevCacheLineOffset = -64;
	newCache->acceptShrink = 1; //postavlja se na 0 kad se povecava broj ploca u kesu;
	newCache->headEmpty = newCache->headFilled = newCache->headPartiallyFilled = 0;
	newCache->magicNumber = MAGIC;
}

kmem_cache_descriptor* initializeCache(cache_head*slab, const char *name, unsigned size, void(*ctor)(void *), void(*dtor)(void *)) {

	kmem_cache_descriptor*newCache = &slab->cacheHeaderArray[slab->free];

	int nextFree = newCache->next;
	newCache->next = slab->alloc;
	slab->alloc = slab->free;
	slab->free = nextFree;
	cacheInit(newCache, name, size, ctor, dtor);
	return newCache;

}

kmem_cache_descriptor* initializeCacheForBuffers(cache_head*slab, unsigned size) {


	kmem_cache_descriptor*newCache = &slab->cacheHeaderArray[slab->free];

	int nextFree = newCache->next;
	newCache->next = 0;
	slab->free = nextFree;
	cacheInit(newCache, "size-", size, 0, 0);
	return newCache;

}


void initializeBufferCaches() {
	kmem_cache_descriptor**bufCaches = bufferCaches();
	for (int i = 0; i < 13; i++) {
		bufCaches[i] = 0;
	}
}

kmem_cache_descriptor* findSameNamedCache(const char *name) {
	cache_head*slab = cacheHeadersList();
	while (slab != 0) {
		for (int i = slab->alloc; i != -1; i = slab->cacheHeaderArray[i].next) {
			if (strcmp(slab->cacheHeaderArray[i].name, name) == 0) {
				slab->cacheHeaderArray[i].errCode = 1;
				return &slab->cacheHeaderArray[i];
			}
		}
		slab = slab->next;
	}
	return 0;
}

int isInCacheHeaderBlock(kmem_cache_descriptor*cachep, cache_head*header) {

	return ((char*)cachep >= (char*)header->cacheHeaderArray) &&
		(((char*)cachep) < ((char*)header->cacheHeaderArray + header->max * sizeof(kmem_cache_descriptor)));
}

double currOccupancy(kmem_cache_descriptor *cachep) {
	double sum = 0;
	kmem_slab* cur = cachep->headPartiallyFilled;
	while (cur != 0) {
		sum += cur->numOfObjects;
		cur = cur->next;
	}
	sum += cachep->numOfFilled*cachep->objInSlab;
	if (cachep->slabNum*cachep->objInSlab == 0) return 0;
	return sum / (cachep->slabNum*cachep->objInSlab);

}

void destroySlabList(kmem_cache_descriptor *cachep, kmem_slab*p) {
	while (p) {
		kmem_slab*t = p;
		p = p->next;
		buddy_free(t, cachep->slabSize + sizeof(kmem_slab));
	}
}
void destroyAllSlabs(kmem_cache_descriptor *cachep) {
	destroySlabList(cachep, cachep->headEmpty);
	cachep->headEmpty = 0;
	destroySlabList(cachep, cachep->headFilled);
	cachep->headFilled = 0;
	destroySlabList(cachep, cachep->headPartiallyFilled);
	cachep->headPartiallyFilled = 0;
	cachep->slabNum = 0;
	cachep->numOfFilled = 0;
}


unsigned int* getFlags(kmem_cache_descriptor *cachep, kmem_slab*empty) { 
	return (int*)((char*)empty + sizeof(kmem_slab));
}
void* getObj(kmem_cache_descriptor *cachep, kmem_slab*parFilled, int i) {
	return (char*)parFilled->objects + cachep->dataSize*i;
}

void initEmptySlab(kmem_cache_descriptor *cachep, kmem_slab*empty) {
	int*flags = getFlags(cachep, empty);
	for (int i = 0; i < cachep->numOfIntForBitVector; i++) {
		flags[i] = 0;
	}
}
kmem_slab*allocateNewSlab(kmem_cache_descriptor *cachep) {
	kmem_slab* newSlab = (kmem_slab*)buddy_alloc((cachep->slabSize + sizeof(kmem_slab)) / BLOCK_SIZE);
	if (newSlab == 0)return newSlab;
	int cacheOff = cachep->prevCacheLineOffset + 64;
	if (cachep->slabSize - cachep->objInSlab*cachep->dataSize - cachep->numOfIntForBitVector * sizeof(int) <= cacheOff) {
		
		cacheOff = 0;
	}
	cachep->prevCacheLineOffset = cacheOff;
	newSlab->maxObjects = cachep->objInSlab;
	newSlab->objects = (char*)newSlab + sizeof(kmem_slab) + cacheOff + cachep->numOfIntForBitVector * sizeof(int);


	newSlab->firstFree = 0;
	newSlab->next = 0;
	newSlab->numOfObjects = 0;
	initEmptySlab(cachep, newSlab);
	return newSlab;

}
int setNewFirstFree(kmem_cache_descriptor* cachep, kmem_slab*slab) {
	int bb = 0;
	unsigned int*flags = getFlags(cachep, slab);
	while (((flags[bb] ^ (~0)) == 0) && bb < cachep->numOfIntForBitVector) bb++;
	if (bb == cachep->numOfIntForBitVector) return -1;
	unsigned int mask = 1;
	int pos = 0;
	while ((flags[bb] & mask) != 0) {
		mask <<= 1;
		pos++;
	}
	slab->firstFree = bb * 32 + pos;
}

void allocFirstFree(kmem_cache_descriptor* cache, kmem_slab*slab) { 
	unsigned int* flags = getFlags(cache, slab);
	flags[slab->firstFree / 32] |= 1 << (slab->firstFree % 32);
}

void deallocInd(kmem_cache_descriptor* cache, kmem_slab*slab, int ind) { 
	if (slab->maxObjects <= ind)return;
	unsigned int* flags = getFlags(cache, slab);
	flags[ind / 32] &= ~(1 << (ind % 32));
}

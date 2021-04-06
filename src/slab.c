#include "slab.h"
#include "buddy.h"
#include "cache.h"
#include "lock.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void kmem_init(void *space, int block_num) {
	
	buddy_init(space, block_num);
	void*slab = slabAllocator();
	*(HANDLE*)slab = createMutex();
	slab = (char*)slab + sizeof(HANDLE);
	
	*((cache_head**)slab) = (cache_head*)((char*)slab + sizeof(cache_head*)+13*sizeof(kmem_cache_descriptor*));
	initializeBufferCaches();
	//head of the list of blocks in which cache heads are stored
	cache_head*head = cacheHeadersList();
	cacheHeaderBlockInit(head, endOfSpace());
}



kmem_cache_t *kmem_cache_create(const char *name, size_t size, void(*ctor)(void *), void(*dtor)(void *)) {
	lock();
	kmem_cache_descriptor*sameNamed = findSameNamedCache(name);
	if (sameNamed != 0) { sameNamed->errCode = 4; unlock(); return sameNamed; }
	cache_head*slab = cacheHeadersList();
	while (slab->next != 0 && slab->free == -1) {
		slab = slab->next;
	}
	kmem_cache_descriptor*newCache;
	if (slab->free == -1) { //ako nema slobodnih blokova za slabove
		cache_head*newch = (cache_head*)buddy_alloc(1);
		if (newch == 0) { unlock(); return 0; }

		cacheHeaderBlockInit(newch, (char*)newch + BLOCK_SIZE);
		slab->next = newch;
		slab = newch;
	}
	newCache = initializeCache(slab, name, size, ctor, dtor);
	unlock();
	return (kmem_cache_t*)newCache;
}



int kmem_cache_shrink(kmem_cache_t *c) {
	if (c == 0) return -1;
	lock();
	kmem_cache_descriptor* cachep = (kmem_cache_descriptor*)c;
	if (cachep->magicNumber != MAGIC) {
		cachep->errCode = 5; unlock(); return -1;
	}
	int count = 0;
	if (cachep->acceptShrink) {
		while (cachep->headEmpty) {
			kmem_slab*cur = cachep->headEmpty;
			cachep->headEmpty = cur->next;
			buddy_free(cur, cachep->slabSize+sizeof(kmem_slab));
			count++;
		}
	}
	cachep->acceptShrink = 1;
	cachep->slabNum -= count;
	unlock();
	return count;
}

void kmem_cache_destroy(kmem_cache_t *c) {
	
	if (c == 0) return;
	
	lock();
	kmem_cache_descriptor* cachep = (kmem_cache_descriptor*)c;
	if (cachep->magicNumber != MAGIC) {
		cachep->errCode = 5; unlock(); return;
	}
	
	cache_head*slab = cacheHeadersList();
	while (slab != 0) {
		int cur = slab->alloc;
		int prev = -1;
		if (isInCacheHeaderBlock(cachep, slab)) {
			while (cur != -1) {
				if (&slab->cacheHeaderArray[cur] == cachep) {
					destroyAllSlabs(cachep);
					if (prev != -1) slab->cacheHeaderArray[prev].next = cachep->next;
					else slab->alloc = cachep->next;
					cachep->next = slab->free;
					slab->free = cur;
					unlock();
					return;
				}
				prev = cur;
				cur = slab->cacheHeaderArray[cur].next;
			}
			cachep->errCode = 2;
			unlock();
			return;
		}
		slab = slab->next;
	}
	cachep->errCode = 2;
	unlock();
}


void *kmem_cache_alloc(kmem_cache_t *c) {
	if (c == 0) return;
	
	lock();
	kmem_cache_descriptor* cachep = (kmem_cache_descriptor*)c;
	if (cachep->magicNumber!=MAGIC) {
		 unlock(); return;
	}
	if (!cachep->headPartiallyFilled && !cachep->headEmpty) {
		kmem_slab*newSlab = allocateNewSlab(cachep);
		//if (newSlab == 0)return 0;
		cachep->slabNum++;
		cachep->headEmpty = newSlab;
		cachep->acceptShrink = 0;
	}
	kmem_slab*parFilled = cachep->headPartiallyFilled;
	void*newObj = 0;
	if (parFilled != 0) {
		newObj = getObj(cachep, parFilled, parFilled->firstFree);
		allocFirstFree(cachep,parFilled);
		parFilled->numOfObjects++;
		if (parFilled->maxObjects == parFilled->numOfObjects) {
			cachep->headPartiallyFilled = parFilled->next;
			parFilled->next = cachep->headFilled;
			cachep->headFilled = parFilled;
			cachep->numOfFilled++;
		}
		setNewFirstFree(cachep,parFilled);//!!!!!!!
	}
	else {
		kmem_slab*empty = cachep->headEmpty;
		if (empty != 0) {
			//initEmptySlab(cachep, empty);
			newObj = getObj(cachep, empty, 0);
			empty->numOfObjects++;
			allocFirstFree(cachep,empty);
			setNewFirstFree(cachep,empty); //!!!!
			cachep->headEmpty = empty->next;
			empty->next = cachep->headPartiallyFilled;
			cachep->headPartiallyFilled = empty;
		}
		else {
			cachep->errCode = 3;
			return 0;
		}
	}
	if (cachep->ctor)cachep->ctor(newObj);
	unlock();
	return newObj;

}
void *kmalloc(size_t size) {
	if (size<(1 << 5) || size>(1 << 17))return 0;
	for (int i = 5; i < 18; i++) {
		if ((1 << i) >= size) {
			lock();
			kmem_cache_descriptor**bufCache = bufferCaches();
			if (bufCache[i-5] == 0) {
				cache_head*slab = cacheHeadersList();
				while (slab->next != 0 && slab->free == -1) {
					slab = slab->next;
				}
				if (slab->free == -1) { //ako nema slobodnih blokova za slabove
					cache_head*newch = (cache_head*)buddy_alloc(1);
					if (newch == 0) { unlock(); return 0; }

					cacheHeaderBlockInit(newch, (char*)newch + BLOCK_SIZE);
					
				}
				bufCache[i-5] = initializeCacheForBuffers(slab, 1<<i);
			}
			unlock();
			void*ret= kmem_cache_alloc(bufCache[i-5]);
			return ret;
		}
	}
	return 0;
}
void free_cache(kmem_cache_descriptor*cachep, void *objp) {
	kmem_slab*prev = 0;
	kmem_slab*cur = cachep->headFilled;
	while (cur) {
		if ((char*)cur->objects <= (char*)objp && (((char*)cur->objects + cur->maxObjects*cachep->dataSize) > (char*)objp)) {
			cur->numOfObjects--;
			unsigned ind = (unsigned)(((char*)objp- (char*)cur->objects) / cachep->dataSize);
			if (cur->firstFree > ind) cur->firstFree = ind;
			deallocInd(cachep,cur, ind);
			if (prev)prev->next = cur->next;
			else cachep->headFilled = cur->next;
			cur->next = cachep->headPartiallyFilled;
			cachep->headPartiallyFilled = cur;
			cachep->numOfFilled--;
			if (cachep->dtor)cachep->dtor(objp);
			return;
		}
		prev = cur;
		cur = cur->next;
	}
	prev = 0;
	cur = cachep->headPartiallyFilled;
	while (cur) {
		if ((char*)cur->objects <= (char*)objp && (((char*)cur->objects + cur->maxObjects*cachep->dataSize) > (char*)objp)) {
			cur->numOfObjects--;
			unsigned ind = (unsigned)(((char*)objp- (char*)cur->objects) / cachep->dataSize);
			if (cur->firstFree > ind) cur->firstFree = ind;
			deallocInd(cachep,cur, ind);
			if (cur->numOfObjects == 0) {
				if (prev)prev->next = cur->next;
				else cachep->headPartiallyFilled = cur->next;
				cur->next = cachep->headEmpty;
				cachep->headEmpty = cur;
				
			}
			if (cachep->dtor)cachep->dtor(objp);
			return;
		}
		prev = cur;
		cur = cur->next;
	}
	cachep->errCode = 7;
}
void kmem_cache_free(kmem_cache_t *c, void *objp) {
	if (!c || !objp)return;
	
	lock();
	kmem_cache_descriptor* cachep = (kmem_cache_descriptor*)c;
	if (cachep->magicNumber != MAGIC) {
		cachep->errCode = 5; unlock(); return;
	}
	free_cache(cachep, objp);
	unlock();
	return;

}
void kfree(const void *objp) {
	if (objp == 0)return;
	lock();
	kmem_cache_descriptor**bufCaches = bufferCaches();
	for (int i = 0; i < 13; i++) {
		if (bufCaches[i] == 0)continue;
		free_cache(bufCaches[i], objp);
		if (bufCaches[i]->errCode == 7) {
			bufCaches[i]->errCode = 0;
		}
		else { 
			if (bufCaches[i]->headEmpty != 0 ) {
				kmem_cache_shrink(bufCaches[i]);
			}
			
			unlock(); 
			return; 
		}
	}

	unlock();
	return;

}
void kmem_cache_info(kmem_cache_t *c) {
	if (c == 0) return;
	
	lock();
	kmem_cache_descriptor* cachep = (kmem_cache_descriptor*)c;
	if (cachep->magicNumber != MAGIC) {
		cachep->errCode = 5; unlock(); return;
	}
	char n[1024];
	n[0] = '\0';
	strcpy_s(n,1024, cachep->name);
	if (strcmp(cachep->name, "size-") == 0) {  sprintf_s(n, "%d", cachep->dataSize); }
	
	printf("-----Info-----\n");
	printf("Name: %s\n", n);
	printf("Data size (in bytes): %d\n", cachep->dataSize);
	printf("Cache size (in blocks-%d B) %d \n", BLOCK_SIZE, ((cachep->slabSize+sizeof(kmem_slab))/BLOCK_SIZE)*cachep->slabNum);
	printf("Number of slabs: %d \n", cachep->slabNum);
	printf("Number of objects/buffers in a slab: %d \n", cachep->objInSlab);
	printf("Occupancy of a slab: %.2f %% \n", currOccupancy(cachep));
	printf("-----Info-----\n");
	unlock();
	
}
  
int kmem_cache_error(kmem_cache_t *c) {
	if (c == 0) return -1;
	
	lock();
	kmem_cache_descriptor* cachep = (kmem_cache_descriptor*)c;
	if (cachep->magicNumber != MAGIC) {
		cachep->errCode = 5; unlock(); return;
	}
	printf("-----Error-----\n");
	switch (cachep->errCode) {
	case 0: printf("No error happened\n"); break;
	case 1: printf("Multiple creation of the same named cache"); break;
	case 2: printf("Destruction failed: Cache not allocated"); break;
	case 3: printf("Allocation failed: Out of momory error"); break;
	case 4: printf("Same named cache creation attempted"); break;
	case 7: printf("Deallocation failed: Object not allocated by allocator"); break;
	}
	int ret = cachep->errCode;
	cachep->errCode = 0;
	unlock();
	return ret;
}




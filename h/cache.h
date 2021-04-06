#pragma once
#define MINIMUM_OBJ_IN_SLAB 8
#define MAGIC 7777777


struct kmem_slab;
struct cache_head;
struct kmem_cache_s;
typedef struct kmem_slab kmem_slab;
typedef struct cache_head cache_head;
typedef struct kmem_cache_s kmem_cache_s;
struct kmem_cache_descriptor;
typedef struct kmem_cache_descriptor kmem_cache_descriptor;

struct cache_head {
	kmem_cache_descriptor*cacheHeaderArray;
	int alloc;
	int free;
	int max;
	cache_head*next;
};


struct kmem_slab {
	kmem_slab*next;
	int maxObjects;
	int numOfObjects;
	int firstFree;
	void*objects;
};

struct kmem_cache_descriptor {
	
	const char*name; //cache name
	kmem_slab*headFilled;
	kmem_slab*headPartiallyFilled;
	kmem_slab*headEmpty;
	int acceptShrink;
	
	size_t dataSize; //size of the objects in cache
	size_t slabSize;
	int slabNum;
	int objInSlab;
	int numOfFilled;
	int prevCacheLineOffset;
	void(*ctor)(void *); //constructor of the objects in cache
	void(*dtor)(void *); //destructor of the objects in cache

	int errCode;//indicates which error happened
	int next; //index of next cache in array

	int magicNumber;
	int numOfIntForBitVector;
};

struct kmem_cache_s {
	const char*name;
};



//pomocne fje

unsigned maxNumOfCaches(void*h, void*e);
cache_head*cacheHeadersList();
kmem_cache_descriptor**bufferCaches();


void cacheInit(kmem_cache_descriptor*newCache, const char *name, unsigned size, void(*ctor)(void *), void(*dtor)(void *));
void cacheHeaderBlockInit(cache_head*head, void*end);
void initializeBufferCaches();
int optimizeSlabSize(unsigned size);
kmem_cache_descriptor* initializeCacheForBuffers(cache_head*slab, unsigned size);
kmem_cache_descriptor* initializeCache(cache_head*slab, const char *name, unsigned size, void(*ctor)(void *), void(*dtor)(void *));

kmem_cache_descriptor* findSameNamedCache(const char *name);
int isInCacheHeaderBlock(kmem_cache_descriptor*cachep, cache_head*header);


void destroySlabList(kmem_cache_descriptor *cachep, kmem_slab*p);
void destroyAllSlabs(kmem_cache_descriptor *cachep);

double currOccupancy(kmem_cache_descriptor *cachep);

//slabovi

unsigned int* getFlags(kmem_cache_descriptor *cachep, kmem_slab*empty);
void* getObj(kmem_cache_descriptor *cachep, kmem_slab*parFilled, int i);

void initEmptySlab(kmem_cache_descriptor *cachep, kmem_slab*empty);
kmem_slab*allocateNewSlab(kmem_cache_descriptor *cachep);

int setNewFirstFree(kmem_cache_descriptor* cachep, kmem_slab*slab);
void allocFirstFree(kmem_cache_descriptor* cache, kmem_slab*slab);
void deallocInd(kmem_cache_descriptor* cache, kmem_slab*slab, int ind);
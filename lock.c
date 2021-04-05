#include "lock.h"
#include <windows.h>

HANDLE*lockFlag() {
	void*slab = slabAllocator();
	return (HANDLE*)slab;
}

void lock() {
	WaitForSingleObject(*lockFlag(), INFINITE);
}
void unlock() {
	ReleaseMutex(*lockFlag());
}

HANDLE createMutex() {
	return CreateMutexA(NULL, FALSE, "mutex");
}
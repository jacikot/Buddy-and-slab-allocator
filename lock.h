#pragma once

typedef void* HANDLE;

HANDLE*lockFlag();

HANDLE createMutex();

void lock();

void unlock();
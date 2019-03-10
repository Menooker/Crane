#pragma once
#include <Crane.h>
#include <stdint.h>
#define FDALLOC_COUNT (4096*64)


namespace Crane {
	template<typename T, unsigned _SIZE>
	struct PooledAllocator
	{
		int fd_pool_hint = 0;
		T fd_pool[_SIZE];

		int GetIndex(const T*) const;
		T* Alloc();
		T* AllocMayFail();
		void Free(T* file);
		void GlobalInitPool();
	};

	struct DeviceRegisteryEntry
	{
		union {
			int path_size;
			std::atomic<int> atomic_lock;
		};
		char buffer[128];
		char* GetPath();
		char* GetFunctionName();
	};
}
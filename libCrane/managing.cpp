#include <atomic>
#include <FDAlloc.h>
#include <algorithm>
#include <fcntl.h>
#include <sys/mman.h>
#include <SharedData.h>
#include <cassert>
#include <util.h>
namespace Crane
{
	SharedMemoryStruct* pshared_data = nullptr;
	template struct PooledAllocator<CraneFile, FDALLOC_COUNT>;
	template struct PooledAllocator<DeviceRegisteryEntry, 128>;

	void InitSharedMemorySpace(int is_server)
	{
		int fd = open("/home/menooker/crane", O_CLOEXEC | O_RDWR);
		if (fd == -1)
		{
			perror("Cannot open shared memory mapping file");
			exit(1);
		}
		pshared_data = (SharedMemoryStruct*)mmap(nullptr, DivideAndCeil(sizeof(SharedMemoryStruct), 4096) * 4096 , PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if ((intptr_t)pshared_data == -1)
		{
			perror("Unable to mmap the file");
			exit(1);
		}
		if (is_server)
		{
			pshared_data->fd_allocator.GlobalInitPool();
			pshared_data->dev_allocator.GlobalInitPool();
		}
	}

	char * DeviceRegisteryEntry::GetPath()
	{
		return buffer;
	}
	char* DeviceRegisteryEntry::GetFunctionName()
	{
		return buffer + path_size;
	}

	template<typename T, unsigned _SIZE>
	void PooledAllocator<T,_SIZE>::GlobalInitPool()
	{
		for (unsigned i = 0; i < _SIZE; i++)
		{
			fd_pool[i].atomic_lock.store(-1);
		}
	}

	template<typename T, unsigned _SIZE>
	int PooledAllocator<T, _SIZE>::GetIndex(const T * ptr) const
	{
		int ret = ptr - fd_pool;
		assert(ret >= 0 && ret < _SIZE);
		return ret;
	}

	template<typename T, unsigned _SIZE>
	T* PooledAllocator<T, _SIZE>::Alloc()
	{
		int idx = fd_pool_hint;
		for (int i = 0; i < _SIZE; i++)
		{
			auto pfile = &fd_pool[idx];
			int expect = -1;
			if (pfile->atomic_lock.compare_exchange_strong(expect, -2))
			{
				fd_pool_hint = idx + 1;
				return pfile;
			}
			idx = (idx + 1) % _SIZE;
		}
		return nullptr;
	}

	template<typename T, unsigned _SIZE>
	T* PooledAllocator<T, _SIZE>::AllocMayFail()
	{
		auto ret = Alloc();
		if (!ret)
		{
			char buf[] = "Cannot allocate file structure";
			WriteByPass(2, buf, sizeof(buf));
		}
		return ret;
	}

	template<typename T, unsigned _SIZE>
	void PooledAllocator<T, _SIZE>::Free(T* file)
	{
		fd_pool_hint = std::min(fd_pool_hint, GetIndex(file));
		file->atomic_lock.store(-1);
	}

}


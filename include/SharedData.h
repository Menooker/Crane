#pragma once
#include <FDAlloc.h>

namespace Crane
{
	struct SharedMemoryStruct
	{
		PooledAllocator<CraneFile, FDALLOC_COUNT> fd_allocator;
		PooledAllocator<DeviceRegisteryEntry,128> dev_allocator;
	};

	extern int WriteByPass(int fd, const char* buf, size_t len);
	extern SharedMemoryStruct* pshared_data;


	class FileSharedPtr
	{
		typedef CraneFile* PtrType;
		CraneFile* p;
	public:
		CraneFile* operator -> ()
		{
			return p;
		}
		operator PtrType()
		{
			return p;
		}
		void Reset()
		{
			if (p)
			{
				auto cnt = --(p->ref_count);
				if (cnt <= 0)
					pshared_data->fd_allocator.Free(p);
			}
			p = nullptr;
		}
		CraneFile* Get()
		{
			return p;
		}
		FileSharedPtr& operator = (const FileSharedPtr& other)
		{
			Reset();
			++other.p->ref_count;
			p = other.p;
			return *this;
		}
		FileSharedPtr& operator = (FileSharedPtr&& other)
		{
			Reset();
			p = other.p;
			other.p = nullptr;
			return *this;
		}
		FileSharedPtr(const FileSharedPtr& other)
		{
			++other.p->ref_count;
			p = other.p;
		}
		FileSharedPtr(FileSharedPtr&& other)
		{
			p = other.p;
			other.p = nullptr;
		}

		//Copy the reference
		explicit FileSharedPtr(CraneFile* f)
		{
			++f->ref_count;
			p = f;
		}

		//Steal the reference
		FileSharedPtr(CraneFile* f, int dummy)
		{
			p = f;
		}

		~FileSharedPtr()
		{
			Reset();
		}
	};
}
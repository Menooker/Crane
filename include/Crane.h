#pragma once
#include <sys/types.h>
#include <atomic>
struct CraneFile;
struct DeviceInfo
{
	uint32_t idx;
	int(*open)(CraneFile* fd, char* path, int flags, mode_t mode);
	ssize_t(*read) (CraneFile* fd, char* buf, size_t size);
	ssize_t(*write) (CraneFile* fd, char* buf, size_t size);
	void(*close)(CraneFile* fd);
	loff_t(*llseek) (CraneFile* fd, loff_t offset, int whence);
	size_t(*get_size)(CraneFile* fd);
	int(*truncate)(loff_t off);
};

struct CraneFile
{
	union {
		std::atomic<int> atomic_lock;
		int device_idx;
	};
	int flags;
	size_t offset;
	std::atomic<int> ref_count;
};

extern "C"
{
	int CraneIsServer();
	void SendToLinuxServer(void* buf, size_t sz);
	void SendToWinServer(void* buf, size_t sz);
	void RecvFromLinuxServer(void* buf, size_t sz);
	void RecvFromWinServer(void* buf, size_t sz);
	int CraneRegisterDeviceLocal(const char* path, DeviceInfo* dev);
	int CraneRegisterDeviceGlobal(const char* dllpath, const char* fnname);
	void CraneDefaultClose(CraneFile*);
	loff_t CraneDefaultLSeek(CraneFile* fd, loff_t offset, int whence);
	int ConnectToServer(int port);
	void SendToServer(int sockfd, void* buf, size_t sz);
	int CraneCloseBypass(int fd);
	void RecvFromServer(int sockfd, void* buf, size_t sz);

	typedef int(*CraneDeviceCallback)(int devidx, char* dllpath, char* funcname);
	void CraneIterateDevices(CraneDeviceCallback fn);

	typedef int(*CraneFdCallback)(int fdpos, CraneFile* dev);
	void CraneIterateFd(CraneFdCallback fn);
}
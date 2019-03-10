#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <arpa/inet.h>
#include <unordered_map>
#include <string>
#include <errno.h>
#include <asm-generic/errno.h>
#include <assert.h>
#include <string.h>
#include "Devices.h"
#include "HookFunction.h"
#include <memory>
#include <unistd.h>
#include <dirent.h>
#include <linux/limits.h>
#include "CompilerHappy.h"
#include <Crane.h>
#include <SharedData.h>
#include <vector>
#include <util.h>
using namespace Crane;

bool init_called = false;

//fix-me: impl dup & dup3 - close-on-exec flag may not correct

static int linux_server_fd;
static int win_server_fd;

def_name(open, int, char*, int, mode_t);
def_name(close, int, int);
def_name(read, ssize_t, int, char*, size_t);
def_name(write, ssize_t, int, char*, size_t);
def_name(lseek64, loff_t, int, loff_t, int);
def_name(dup2, int, int, int);
def_name(ftruncate64, int, int, loff_t);
def_name(truncate64, int, char*, loff_t);
def_name(execve, int, const char *, char *const[], char *const[]);

struct CraneContext
{
	std::unordered_map<int, FileSharedPtr> _fd2file;
	std::unordered_map<std::string, DeviceInfo*> _name2dev;
	std::vector< DeviceInfo*> _id2dev;
};

static CraneContext& GetCraneContext()
{
	static CraneContext con;
	return con;
}

#define id2dev (GetCraneContext()._id2dev)
#define fd2file (GetCraneContext()._fd2file)
#define name2dev (GetCraneContext()._name2dev)


namespace Crane {
	int main_fd;
	extern void InitSharedMemorySpace(int is_server);
	int WriteByPass(int fd, const char* buf, size_t len)
	{
		return CallOld<Name_write>(fd, (char*)buf, len);
	}
}


static FileSharedPtr CreateCraneFile(int dev_idx,int flags)
{
	auto ret = pshared_data->fd_allocator.AllocMayFail();
	ret->device_idx = dev_idx;
	ret->flags = flags;
	ret->offset = 0;
	return FileSharedPtr(ret);
}

static int CraneTruncate64(char* path, loff_t off)
{
	char p[PATH_MAX];
	int ret = CallOld<Name_truncate64>(path, off);
	if (ret == -1)
		return ret;
	auto itr = name2dev.find(realpath(path, p));
	if (itr != name2dev.end())
	{
		if (!itr->second->truncate)
		{
			errno = EINVAL;
			return -1;
		}
		itr->second->truncate(off);
	}
	return ret;
}

static int CraneFTruncate64(int fd, loff_t off)
{
	int ret = CallOld<Name_ftruncate64>(fd, off);
	if (ret == -1)
		return ret;
	auto itr = fd2file.find(fd);
	if (itr != fd2file.end())
	{
		if (! id2dev[itr->second->device_idx]->truncate)
		{
			errno = EINVAL;
			return -1;
		}
		return id2dev[itr->second->device_idx]->truncate(off);
	}
	return ret;
}


static int CraneOpen(char* path, int flags, mode_t mode)
{
	char p[PATH_MAX];
	int ret = CallOld<Name_open>(path, flags, mode);
	if (ret == -1)
		return ret;
	auto itr = name2dev.find(realpath(path, p));
	if (itr != name2dev.end())
	{
		FileSharedPtr pfile = CreateCraneFile(itr->second->idx, flags);
		if (id2dev[pfile->device_idx]->open(pfile, path, flags, mode)==-1)
		{
			CallOld<Name_close>(ret);
			errno = EACCES;
			return -1;
		}
		fd2file.insert(std::make_pair(ret, std::move(pfile)));
	}
	return ret;
}

static int CraneClose(int fd)
{
	auto itr = fd2file.find(fd);
	if (itr != fd2file.end())
	{
		id2dev[itr->second->device_idx]->close(itr->second);
		fd2file.erase(itr);
	}
	int ret = CallOld<Name_close>(fd);
	return ret;
}

void CraneDefaultClose(CraneFile*)
{
	
}

static ssize_t CraneRead(int fd, char* buf, size_t sz)
{
	auto itr = fd2file.find(fd);
	if (itr != fd2file.end())
	{
		if (!id2dev[itr->second->device_idx]->read)
		{
			errno = EINVAL;
			return -1;
		}
		return id2dev[itr->second->device_idx]->read(itr->second, buf, sz);
	}
	ssize_t ret = CallOld<Name_read>(fd, buf, sz);
	return ret;
}

static ssize_t CraneWrite(int fd, char* buf, size_t sz)
{
	auto itr = fd2file.find(fd);
	if (itr != fd2file.end())
	{
		if (!id2dev[itr->second->device_idx]->write)
		{
			errno = EINVAL;
			return -1;
		}
		return id2dev[itr->second->device_idx]->write(itr->second, buf, sz);
	}
	ssize_t ret = CallOld<Name_write>(fd, buf, sz);
	return ret;
}

static int CraneDup2(int oldfd, int newfd)
{
	int ret = CallOld<Name_dup2>(oldfd, newfd);
	if (ret < 0)
		return ret;
	auto itr = fd2file.find(oldfd);
	if (itr != fd2file.end())
	{
		auto itr2 = fd2file.find(newfd);
		if (itr2 != fd2file.end())
		{
			id2dev[itr2->second->device_idx]->close(itr2->second);
			itr2->second = itr->second;
		}
		else
		{
			fd2file.insert(std::make_pair(newfd, itr->second));
		}
	}
	else
	{
		auto itr2 = fd2file.find(newfd);
		if (itr2 != fd2file.end())
		{
			id2dev[itr2->second->device_idx]->close(itr2->second);
			fd2file.erase(itr2);
		}
	}
	return ret;
}


static loff_t CraneLSeek(int fd, loff_t offset, int whence)
{
	auto itr = fd2file.find(fd);
	if (itr != fd2file.end())
	{
		if (!id2dev[itr->second->device_idx]->llseek)
		{
			errno = ESPIPE;
			return -1;
		}
		CallOld<Name_lseek64>(fd, offset, whence);
		return id2dev[itr->second->device_idx]->llseek(itr->second, offset, whence);
	}
	int ret = CallOld<Name_lseek64>(fd, offset, whence);
	return ret;
}

loff_t CraneDefaultLSeek(CraneFile* fd, loff_t offset, int whence)
{
	if (whence == SEEK_END || (fd->flags & O_APPEND) )
	{
		fd->offset = id2dev[fd->device_idx]->get_size(fd) + offset;
		return offset;
	}
	else if (whence == SEEK_SET)
	{
		fd->offset = offset;
		return offset;
	}
	else if (whence == SEEK_CUR)
	{
		fd->offset += offset;
		return offset;
	}
	else
	{
		errno = EINVAL;
		return -1;
	}
}

void PerrorSafe(const char* str)
{
	char* errstr = strerror(errno);
	int cnt = snprintf(nullptr, 0, "%s: %s\n", str, errstr);
	char* buf = new char[cnt + 1];
	snprintf(buf, cnt + 1, "%s: %s\n", str, errstr);
	CallOld<Name_write>(2, buf, cnt);
	delete[]buf;
}


int CraneExecve(const char *filename, char *const argv[], char *const envp[])
{
	//first, dump all Crane file descriptors in current process
	if (!fd2file.empty())
	{
		char buffer[PATH_MAX];
		snprintf(buffer, PATH_MAX, "/tmp/crane_process_%d", getpid());
		int fd = CallOld<Name_open>(buffer, O_TRUNC | O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
		if (fd == -1)
		{
			PerrorSafe("Cannot create fd dump file");
			exit(1);
		}
		for (auto& itm : fd2file)
		{
			CallOld<Name_write>(fd, (char*)&itm.first, sizeof(itm.first));
			int fd_idx = pshared_data->fd_allocator.GetIndex(itm.second);
			CallOld<Name_write>(fd, (char*)&fd_idx, sizeof(fd_idx));
		}
		CallOld<Name_close>(fd);
		int ret = CallOld<Name_execve>(filename, argv, envp);
		if (ret == -1)
		{
			remove(buffer);
		}
	}
	return CallOld<Name_execve>(filename, argv, envp);
}

int CraneIsServer()
{
	return main_fd != -1;
}



int ConnectToServer(int port)
{
	struct sockaddr_in address;
	int sock;
	struct sockaddr_in serv_addr;
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		printf("\n Socket creation error \n");
		return -1;
	}

	memset(&serv_addr, 0, sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	// Convert IPv4 and IPv6 addresses from text to binary form 
	if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0)
	{
		PerrorSafe("Address error");
		return -1;
	}

	if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		PerrorSafe("Connection Failed");
		return -1;
	}
	return sock;
}

void SendToServer(int sockfd, void* buf, size_t sz)
{
	if(CallOld<Name_write>(sockfd, (char*)buf, sz) != sz)
	{
		PerrorSafe("Send error!");
		assert(0);
	}
}

void RecvFromServer(int sockfd, void* buf, size_t sz)
{
	if(CallOld<Name_read>(sockfd, (char*)buf, sz)!=sz)
	{
		PerrorSafe("Recv error!");
		assert(0);
	}
}


void SendToLinuxServer(void* buf, size_t sz)
{
	SendToServer(linux_server_fd,buf,sz);
}

void SendToWinServer( void* buf, size_t sz)
{
	SendToServer(win_server_fd,buf,sz);
}

void RecvFromLinuxServer(void* buf, size_t sz)
{
	RecvFromServer(linux_server_fd,buf,sz);
}

void RecvFromWinServer(void* buf, size_t sz)
{
	RecvFromServer(win_server_fd,buf,sz);
}






int CraneRegisterDeviceLocal(const char* path, DeviceInfo* dev)
{
	name2dev.insert(std::make_pair(std::string(path), dev));
	id2dev.push_back(dev);
}

int CraneRegisterDeviceGlobal(const char* dllpath, const char* fnname)
{
	if (!CraneIsServer())
		return -3;
	size_t size1 = (dllpath ? strlen(dllpath) : 0) + 1;
	size_t size2 = strlen(fnname) + 1;
	if (size1 + size2 > sizeof(DeviceRegisteryEntry::buffer))
		return -2;
	auto pdev = pshared_data->dev_allocator.Alloc();
	if (!pdev)
		return -1;
	if (!dllpath)
		pdev->path_size = 0;
	else
	{
		memcpy(pdev->GetPath(), dllpath, size1);
		pdev->path_size = size1;
	}
	memcpy(pdev->GetFunctionName(), fnname, size2);
	return pshared_data->dev_allocator.GetIndex(pdev);
}

int CraneCloseBypass(int fd)
{
	return CallOld<Name_close>(fd);
}

static void HookMe()
{
	//linux_server_fd = ConnectToServer(13579);
//win_server_fd = ConnectToServer(13578);
	void* handle = dlopen("libpthread.so.0", RTLD_LAZY);
	if (!handle) {
		fputs(dlerror(), stderr);
		exit(1);
	}
	void* handlec = dlopen("libc.so.6", RTLD_LAZY);
	if (!handle) {
		fputs(dlerror(), stderr);
		exit(1);
	}
	DoHookInLibAndLibC<Name_open>(handlec, handle, CraneOpen);
	DoHookInLibAndLibC<Name_close>(handlec, handle, CraneClose);
	DoHookInLibAndLibC<Name_read>(handlec, handle, CraneRead);
	DoHookInLibAndLibC<Name_write>(handlec, handle, CraneWrite);
	DoHookInLibAndLibC<Name_lseek64>(handlec, handle, CraneLSeek);
	DoHookInLibAndLibC<Name_dup2>(handlec, handle, CraneDup2);
	DoHookInLibAndLibC<Name_ftruncate64>(handlec, handle, CraneFTruncate64);
	DoHookInLibAndLibC<Name_truncate64>(handlec, handle, CraneTruncate64);
	DoHookInLibAndLibC<Name_execve>(handlec, handle, CraneExecve);
}


void CraneIterateDevices(CraneDeviceCallback fn)
{
	for (int i = 0; i < pshared_data->dev_allocator.fd_pool_hint; i++)
	{
		void* handle;
		auto& cur = pshared_data->dev_allocator.fd_pool[i];
		char* dllname;
		if (cur.path_size)
			dllname = cur.GetPath();
		else
			dllname = nullptr;
		if (fn(i, dllname, cur.GetFunctionName()))
			break;
	}
}

void CraneIterateFd(CraneFdCallback fn)
{
	for (int i = 0; i < FDALLOC_COUNT; i++)
	{
		auto& cur = pshared_data->fd_allocator.fd_pool[i];
		if (cur.device_idx >= 0)
		{
			if (fn(i, &cur))
				break;
		}
	}
}

static void InitDevicesLocal()
{
	typedef DeviceInfo* (*ptrFunc)();
	for (int i = 0; i < pshared_data->dev_allocator.fd_pool_hint; i++)
	{
		void* handle;
		auto& cur = pshared_data->dev_allocator.fd_pool[i];
		if (cur.path_size)
		{
			handle = dlopen(cur.GetPath(), RTLD_NOW);
			if (!handle) {
				fputs(dlerror(), stderr);
				exit(1);
			}
		}
		else
		{
			handle = dlopen(0, RTLD_NOW | RTLD_GLOBAL);
			if (!handle) {
				fputs(dlerror(), stderr);
				exit(1);
			}
		}
		ptrFunc func = (ptrFunc) dlsym(handle, cur.GetFunctionName());
		if (!func) {
			fputs(dlerror(), stderr);
			exit(1);
		}
		func()->idx = i;
	}
}

void CopyParentFd()
{
	char buffer[PATH_MAX];
	snprintf(buffer, PATH_MAX, "/tmp/crane_process_%d", getpid());
	int fd = open(buffer, O_RDWR, 0);
	if (fd == -1)
		return;
	for (;;)
	{
		int buf;
		int bytes = read(fd, (char*)&buf, sizeof(buf));
		if (bytes == 0)
			break;
		if (bytes != sizeof(buf))
		{
			fputs("fd dump file is corrupted\n", stderr);
			exit(1);
		}
		int fd_idx;
		bytes = read(fd, (char*)&fd_idx, sizeof(fd_idx));
		if (bytes != sizeof(fd_idx) || fd_idx<0 || fd_idx>= FDALLOC_COUNT)
		{
			fputs("fd dump file is corrupted\n", stderr);
			exit(1);
		}
		FileSharedPtr ptr = FileSharedPtr (&pshared_data->fd_allocator.fd_pool[fd_idx],0);
		if (ptr->flags & O_CLOEXEC)
			id2dev[ptr->device_idx]->close(ptr);
		else
			fd2file.insert(std::make_pair(buf, std::move(ptr)));
		//all fd with O_CLOEXEC will be deref-ed
	}
	close(fd);
	remove(buffer);
	
}

__attribute__((constructor)) void CraneInit()
{
	main_fd = open("/home/menooker/crane", O_EXCL | O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
	if (main_fd == -1)
	{
		InitSharedMemorySpace(false);
		InitDevicesLocal();
		CopyParentFd();
		HookMe();
	}
	else
	{
		if (ftruncate(main_fd, DivideAndCeil(sizeof(SharedMemoryStruct), 4096) * 4096)==-1)
		{
			perror("ftruncate failed");
			exit(1);
		}
		InitSharedMemorySpace(true);
		CraneRegisterDeviceGlobal(nullptr,"RegisterClipboardLocal");
		CraneRegisterDeviceGlobal(nullptr, "RegisterCraneStatusLocal");
	}
	//fprintf(stderr, "Init %d\n", getpid());
	init_called = true;
}

__attribute__((destructor)) void CraneExit()
{
	if (main_fd != -1)
	{
		close(main_fd);
		if (remove("/home/menooker/crane"))
		{
			PerrorSafe("deleting /home/menooker/crane failed. ");
		}
	}
	//CallOld<Name_write>(2, (char*)"Closing\n", 8);
}
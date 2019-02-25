#include <fcntl.h>
#include <sys/types.h>
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
#include "ClipboardDevice.h"
#include "HookFunction.h"
#include <memory>

using namespace Crane;

//fix-me: add O_APPEND seek fix

struct CraneFile;

struct DeviceInfo
{
	int idx;
	int (*open)(CraneFile* fd, char* path, int flags, mode_t mode);
	ssize_t(*read) (CraneFile* fd, char* buf, size_t size);
	ssize_t(*write) (CraneFile* fd, char* buf, size_t size);
	void(*close)(CraneFile* fd);
	loff_t(*llseek) (CraneFile* fd, loff_t offset, int whence);
	size_t(*get_size)(CraneFile* fd);
	int (*truncate)(loff_t off);
};

struct CraneFile
{
	DeviceInfo* vtable;
	int fd;
	size_t offset;
};

#pragma pack(push)
#pragma pack(4)
struct CraneRequest
{
	int dev_idx;
	uint32_t additional_sz;
	char data[0];
};
#pragma pack(pop)

static int main_fd;
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

struct CraneContext
{
	std::unordered_map<int, std::shared_ptr<CraneFile>> _fd2file;
	std::unordered_map<std::string, DeviceInfo*> _name2dev;
};

static CraneContext& GetCraneContext()
{
	static CraneContext con;
	return con;
}

#define fd2file (GetCraneContext()._fd2file)
#define name2dev (GetCraneContext()._name2dev)


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
		if (!itr->second->vtable->truncate)
		{
			errno = EINVAL;
			return -1;
		}
		return itr->second->vtable->truncate(off);
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
		CraneFile* pfile = new CraneFile{ itr->second, /*fd*/ret, /*offset*/0};
		if (pfile->vtable->open(pfile, path, flags, mode)==-1)
		{
			delete pfile;
			CallOld<Name_close>(ret);
			errno = EACCES;
			return -1;
		}
		fd2file.insert(std::make_pair(ret, std::shared_ptr<CraneFile>(pfile)));
	}
	return ret;
}

static int CraneClose(int fd)
{
	auto itr = fd2file.find(fd);
	if (itr != fd2file.end())
	{
		itr->second->vtable->close(itr->second.get());
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
		if (!itr->second->vtable->read)
		{
			errno = EINVAL;
			return -1;
		}
		return itr->second->vtable->read(itr->second.get(), buf, sz);
	}
	ssize_t ret = CallOld<Name_read>(fd, buf, sz);
	return ret;
}

static ssize_t CraneWrite(int fd, char* buf, size_t sz)
{
	auto itr = fd2file.find(fd);
	if (itr != fd2file.end())
	{
		if (!itr->second->vtable->write)
		{
			errno = EINVAL;
			return -1;
		}
		return itr->second->vtable->write(itr->second.get(), buf, sz);
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
			itr2->second->vtable->close(itr2->second.get());
			itr2->second = itr->second;
		}
		else
		{
			fd2file.insert(std::make_pair(newfd, itr->second));
		}
	}
	return ret;
}


static loff_t CraneLSeek(int fd, loff_t offset, int whence)
{
	auto itr = fd2file.find(fd);
	if (itr != fd2file.end())
	{
		if (!itr->second->vtable->llseek)
		{
			errno = ESPIPE;
			return -1;
		}
		return itr->second->vtable->llseek(itr->second.get(), offset, whence);
	}
	int ret = CallOld<Name_lseek64>(fd, offset, whence);
	return ret;
}

loff_t CraneDefaultLSeek(CraneFile* fd, loff_t offset, int whence)
{
	if (whence == SEEK_SET)
	{
		fd->offset = offset;
		return offset;
	}
	else if (whence == SEEK_CUR)
	{
		fd->offset += offset;
		return offset;
	}
	else if (whence == SEEK_END)
	{
		fd->offset = fd->vtable->get_size(fd) + offset;
		return offset;
	}
	else
	{
		errno = EINVAL;
		return -1;
	}
}

int CraneIsServer()
{
	return main_fd != -1;
}




static int ConnectToServer(int port)
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
		perror("Address error");
		return -1;
	}

	if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		perror("Connection Failed");
		return -1;
	}
	return sock;
}

static void SendToServer(int sockfd, DeviceInfo* dev, void* buf, size_t sz)
{
	/*char buffer[64];
	CraneRequest* req = (CraneRequest*)buffer;
	req->idx=dev->idx;
	req->additional_sz=sz;
	assert(sz+sizeof(req)<64);
	memcpy(req.data,buf,sz);*/
	//if(send(sockfd, req, sizeof(req) + sz, 0) != sizeof(req) + sz)
	if(CallOld<Name_write>(sockfd, (char*)buf, sz) != sz)
	{
		perror("Send error!");
		assert(0);
	}
}

static void RecvFromServer(int sockfd, void* buf, size_t sz)
{
	if(recv(sockfd, buf, sz, 0)!=sz)
	{
		perror("recv error!");
		assert(0);
	}
}


void SendToLinuxServer(DeviceInfo* dev, void* buf, size_t sz)
{
	SendToServer(linux_server_fd,dev,buf,sz);
}

void SendToWinServer(DeviceInfo* dev, void* buf, size_t sz)
{
	SendToServer(win_server_fd,dev,buf,sz);
}

void RecvFromLinuxServer(void* buf, size_t sz)
{
	RecvFromServer(linux_server_fd,buf,sz);
}

void RecvFromWinServer(void* buf, size_t sz)
{
	RecvFromServer(win_server_fd,buf,sz);
}


static DeviceInfo clipboard_dev = {
	.idx = 0,
	.open = [](CraneFile* fd,char*, int flags, mode_t mode) {
		if(flags & O_TRUNC)
		{
			win_server_fd = ConnectToServer(13578);
			ClipboardRequest req {CB_TRUNC,0};
			SendToWinServer(fd->vtable,&req,sizeof(req));
			uint64_t ret_size;
			RecvFromWinServer(&ret_size,sizeof(ret_size));
			close(win_server_fd);			
		}
		return 0;
	},
	.read = [](CraneFile* fd, char* buf, size_t sz)->ssize_t {
		win_server_fd = ConnectToServer(13578);
		ClipboardRequest req {CB_GET,fd->offset,sz};
		SendToWinServer(fd->vtable,&req,sizeof(req));
		uint64_t ret_size;
		RecvFromWinServer(&ret_size,sizeof(ret_size));
		RecvFromWinServer(buf,ret_size);
		fd->offset += ret_size;
		close(win_server_fd);
		return ret_size;
	},
	.write = [](CraneFile* fd, char* buf, size_t sz)->ssize_t {
		win_server_fd = ConnectToServer(13578);
		ClipboardRequest req {CB_SET,fd->offset,sz};
		SendToWinServer(fd->vtable,&req,sizeof(req));
		SendToWinServer(fd->vtable,buf, sz);
		uint64_t ret_size;
		RecvFromWinServer(&ret_size,sizeof(ret_size));
		fd->offset += ret_size;
		close(win_server_fd);
		return ret_size;
	},
	.close = CraneDefaultClose,
	.llseek = CraneDefaultLSeek,
	.get_size = [](CraneFile* fd) -> size_t{
		win_server_fd = ConnectToServer(13578);
		ClipboardRequest req {CB_SIZE};
		SendToWinServer(fd->vtable,&req,sizeof(req));
		uint64_t ret_size;
		RecvFromWinServer(&ret_size,sizeof(ret_size));
		close(win_server_fd);
		return ret_size;
	},
	.truncate = [](loff_t size) -> int {
		win_server_fd = ConnectToServer(13578);
		ClipboardRequest req {CB_TRUNC,size};
		SendToWinServer(nullptr,&req,sizeof(req));
		uint64_t ret_size;
		RecvFromWinServer(&ret_size,sizeof(ret_size));
		close(win_server_fd);	
	}
};



int CraneRegisterDevice(const char* path, DeviceInfo* dev)
{
	name2dev.insert(std::make_pair(std::string(path),dev));
}

void HookMe()
{
	//linux_server_fd = ConnectToServer(13579);
//win_server_fd = ConnectToServer(13578);
	CraneRegisterDevice("/dev/clipboard", &clipboard_dev);

	DoHook<Name_open>(CraneOpen);
	DoHook<Name_close>(CraneClose);
	DoHook<Name_read>(CraneRead);
	DoHook<Name_write>(CraneWrite);
	DoHook<Name_lseek64>(CraneLSeek);
	DoHook<Name_dup2>(CraneDup2);
	DoHook<Name_ftruncate64>(CraneFTruncate64);
	DoHook<Name_truncate64>(CraneTruncate64);
}
__attribute__((constructor)) void CraneInit()
{
	main_fd = open("/home/menooker/crane", O_EXCL | O_CREAT,  S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
	if (main_fd == -1)
	{
		HookMe();
	}
}

__attribute__((destructor)) void CraneExit()
{
	if (main_fd != -1)
	{
		close(main_fd);
		if (remove("/home/menooker/crane"))
		{
			perror("deleting /home/menooker/crane failed. ");
		}
	}
}
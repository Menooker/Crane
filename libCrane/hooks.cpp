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
#include "Devices.h"
#include "HookFunction.h"
#include <memory>
#include <unistd.h>
#include <dirent.h>
#include <linux/limits.h>
#include "CompilerHappy.h"
using namespace Crane;

struct CraneFile;
bool init_called = false;


struct DeviceInfo
{
	uint32_t idx;
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
	//offset is SIZE_MAX if O_APPEND
	size_t offset;
};

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
	else
	{
		auto itr2 = fd2file.find(newfd);
		if (itr2 != fd2file.end())
		{
			itr2->second->vtable->close(itr2->second.get());
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
		if (!itr->second->vtable->llseek)
		{
			errno = ESPIPE;
			return -1;
		}
		CallOld<Name_lseek64>(fd, offset, whence);
		return itr->second->vtable->llseek(itr->second.get(), offset, whence);
	}
	int ret = CallOld<Name_lseek64>(fd, offset, whence);
	return ret;
}

loff_t CraneDefaultLSeek(CraneFile* fd, loff_t offset, int whence)
{
	if (whence == SEEK_END || fd->offset == SIZE_MAX)
	{
		fd->offset = fd->vtable->get_size(fd) + offset;
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

int CraneIsServer()
{
	return main_fd != -1;
}

void PerrorSafe(const char* str)
{
	char* errstr = strerror(errno);
	int cnt = snprintf(nullptr, 0, "%s: %s\n", str, errstr);
	char* buf = new char[cnt+1];
	snprintf(buf, cnt + 1, "%s: %s\n", str, errstr);
	CallOld<Name_write>(2, buf, cnt);
	delete[]buf;
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

static void SendToServer(int sockfd, void* buf, size_t sz)
{
	if(CallOld<Name_write>(sockfd, (char*)buf, sz) != sz)
	{
		PerrorSafe("Send error!");
		assert(0);
	}
}

static void RecvFromServer(int sockfd, void* buf, size_t sz)
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


static DeviceInfo clipboard_dev = {
	/*idx*/ 0,
	/*open*/ [](CraneFile* fd,char*, int flags, mode_t mode) {
		if(flags & O_TRUNC)
		{
			win_server_fd = ConnectToServer(13578);
			RemoteRequest req {clipboard_dev.idx, CB_TRUNC,0};
			SendToWinServer(&req,sizeof(req));
			uint64_t ret_size;
			RecvFromWinServer(&ret_size,sizeof(ret_size));
			close(win_server_fd);			
		}
		if (flags & O_APPEND)
		{
			fd->offset = SIZE_MAX;
		}
		return 0;
	},
	/*read*/ [](CraneFile* fd, char* buf, size_t sz)->ssize_t {
		win_server_fd = ConnectToServer(13578);
		RemoteRequest req { clipboard_dev.idx, CB_GET,fd->offset,sz};
		SendToWinServer(&req,sizeof(req));
		uint64_t ret_size;
		RecvFromWinServer(&ret_size,sizeof(ret_size));
		RecvFromWinServer(buf,ret_size);
		if (fd->offset != SIZE_MAX)
			fd->offset += ret_size;
		close(win_server_fd);
		return ret_size;
	},
	/*write*/ [](CraneFile* fd, char* buf, size_t sz)->ssize_t {
		win_server_fd = ConnectToServer(13578);
		RemoteRequest req { clipboard_dev.idx, CB_SET,fd->offset,sz};
		SendToWinServer(&req,sizeof(req));
		SendToWinServer(buf, sz);
		uint64_t ret_size;
		RecvFromWinServer(&ret_size,sizeof(ret_size));
		if(fd->offset!=SIZE_MAX)
			fd->offset += ret_size;
		close(win_server_fd);
		return ret_size;
	},
	/*close*/ CraneDefaultClose,
	/*llseek*/ CraneDefaultLSeek,
	/*get_size*/ [](CraneFile* fd) -> size_t{
		win_server_fd = ConnectToServer(13578);
		RemoteRequest req { clipboard_dev.idx, CB_SIZE};
		SendToWinServer(&req,sizeof(req));
		uint64_t ret_size;
		RecvFromWinServer(&ret_size,sizeof(ret_size));
		close(win_server_fd);
		return ret_size;
	},
	/*truncate*/ [](loff_t size) -> int {
		win_server_fd = ConnectToServer(13578);
		RemoteRequest req { clipboard_dev.idx, CB_TRUNC,size};
		SendToWinServer(&req,sizeof(req));
		uint64_t ret_size;
		RecvFromWinServer(&ret_size,sizeof(ret_size));
		close(win_server_fd);
		return ret_size;
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
	DIR *dir;
	struct dirent *ent;
	char pathbuf[PATH_MAX];
	char pathlinkbuf[PATH_MAX];
	if ((dir = opendir("/proc/self/fd")) != NULL) {
		/* print all the files and directories within directory */
		while ((ent = readdir(dir)) != NULL) {
			snprintf(pathlinkbuf, PATH_MAX, "/proc/self/fd/%s", ent->d_name);
			if (readlink(pathlinkbuf, pathbuf, PATH_MAX)!=-1)
			{
				auto itr = name2dev.find(pathbuf);
				if (itr != name2dev.end())
				{
					int fd = atoi(ent->d_name);
					int flags = fcntl(fd, F_GETFL);
					size_t off;
					if (flags & O_APPEND)
						off = SIZE_MAX;
					else
						off = CallOld<Name_lseek64>(fd, 0, SEEK_CUR);
					CraneFile* pfile = new CraneFile{ itr->second, /*fd*/fd, /*offset*/ off };
					fd2file.insert(std::make_pair(fd, std::shared_ptr< CraneFile>(pfile)));
					//fprintf(stderr, "Found opened %d\n", fd);
				}
			}
		}
		closedir(dir);
	}
	else {
		/* could not open directory */
		PerrorSafe("cannot parse /proc/self/fd");
		return;
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
}
#include <atomic>
#include <FDAlloc.h>
#include <algorithm>
#include <fcntl.h>
#include <sys/mman.h>
#include <SharedData.h>
#include <cassert>
#include <util.h>
#include <CompilerHappy.h>
#include <unistd.h>
#include <fcntl.h>
#include <error.h>
#include <sys/epoll.h>
#include <sys/socket.h> 
#include <string.h>
#include <netinet/in.h> 
#include <sys/un.h>
#include <linux/limits.h>
#include <unistd.h>
#include <Devices.h>


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

#define UNIX_SOCKET_PATH "/home/menooker/crane.sock"

	int server_fd = -1;
	inline void SendCheck(int fd, void* buf, size_t len)
	{
		//assert(fd != -1);
		if (fd == -1)
		{
			fprintf(stderr, "fd=1, pid=%d\n", getpid());
			sleep(-1);
		}
		int ret = send(fd, buf, len, 0);
		assert(ret == len);
	}

	//https://www.ibm.com/support/knowledgecenter/en/SSB23S_1.1.0.15/gtpc1/unixsock.html
	int ConnectToLinuxServer()
	{

		int client_sock, rc, len;
		struct sockaddr_un server_sockaddr;
		struct sockaddr_un client_sockaddr;
		char buf[256];
		memset(&server_sockaddr, 0, sizeof(struct sockaddr_un));
		memset(&client_sockaddr, 0, sizeof(struct sockaddr_un));

		client_sock = socket(AF_UNIX, SOCK_STREAM | O_CLOEXEC, 0);
		if (client_sock == -1) {
			perror("SOCKET ERROR");
			exit(1);
		}
		client_sockaddr.sun_family = AF_UNIX;
		snprintf(client_sockaddr.sun_path, sizeof(client_sockaddr.sun_path), "/tmp/crane_process_%d.sock", getpid());
		len = sizeof(client_sockaddr);
		unlink(client_sockaddr.sun_path);

		rc = bind(client_sock, (struct sockaddr *) &client_sockaddr, len);
		if (rc == -1) {
			fprintf(stderr, "Pid %d:", getpid());
			perror("BIND ERROR");
			close(client_sock);
			exit(1);
		}
		server_sockaddr.sun_family = AF_UNIX;
		memcpy(server_sockaddr.sun_path, UNIX_SOCKET_PATH, sizeof(UNIX_SOCKET_PATH));
		rc = connect(client_sock, (struct sockaddr *) &server_sockaddr, len);
		if (rc == -1) {
			perror("CONNECT ERROR");
			close(client_sock);
			exit(1);
		}
		
		//fcntl(client_sock, F_SETFD, FD_CLOEXEC);
		server_fd = client_sock;
		//fprintf(stderr, "serverfd%d\n", server_fd);
		return rc;

	}

	void ServerNotifyOpen(int fd, int pos_in_arr)
	{
		FDRequest data{ FDCMD_OPEN, fd };
		SendCheck(server_fd, &data, sizeof(data));
		SendCheck(server_fd, &pos_in_arr, sizeof(pos_in_arr));
	}

	void ServerNotifyClose(int fd)
	{
		FDRequest data{ FDCMD_CLOSE, fd };
		SendCheck(server_fd, &data, sizeof(data));
	}
	void ServerGoodbye()
	{
		//fprintf(stderr, "bye%d\n", server_fd);
		if (server_fd == -1)
			return;
		FDRequest data{FDCMD_BYE, 0};
		SendCheck(server_fd, &data, sizeof(data));
		close(server_fd);
		char buffer[PATH_MAX];
		snprintf(buffer, sizeof(buffer), "/tmp/crane_process_%d.sock", getpid());
		unlink(buffer);
	}

}


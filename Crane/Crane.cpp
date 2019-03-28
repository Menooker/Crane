#include <unistd.h> 
#include <stdio.h> 
#include <sys/socket.h> 
#include <stdlib.h> 
#include <netinet/in.h> 
#include <string.h> 
#include "Crane.h"
#include <CompilerHappy.h>
#include <Devices.h>
#include <signal.h>
#include <string>
#include <vector>
#include <fcntl.h>
#include <error.h>
#include <sys/epoll.h>
#include <thread>
#include <sys/un.h>
#include <linux/limits.h>
#include <unordered_map>
#include <sys/stat.h>

std::string status_str;

#define UNIX_SOCKET_PATH "/home/menooker/crane.sock"

struct Cleaner
{
	~Cleaner()
	{
		unlink(UNIX_SOCKET_PATH);
	}
}_cleaner;

void my_handler(int s) {
	fprintf(stderr, "Caught Ctrl-V, exiting\n");
	exit(0);
}

constexpr int MAXFDS = 16 * 1024;

void MakeSocketNonBlocking(int sockfd) {
	int flags = fcntl(sockfd, F_GETFL, 0);
	if (flags == -1) {
		perror("fcntl F_GETFL");
	}

	if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
		perror("fcntl F_SETFL O_NONBLOCK");
	}
}

void PerrorDie(const char* p)
{
	perror(p);
	exit(-1);
}

void Die(const char* p)
{
	fprintf(stderr, "%s\n", p);
	exit(-1);
}

int epollfd;

void CloseAndUnregSocket(int fd)
{
	if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
		PerrorDie("epoll_ctl EPOLL_CTL_DEL");
	}
	close(fd);
}

//outer: socket fd -> process_fd_map , inner: fd -> fd_idx
struct ProcessFdTable
{
	int pid;
	std::unordered_map<int, int> fd_map;
};
static std::unordered_map<int, ProcessFdTable> process_fd_map;

bool OnRecv(int fd)
{
	FDRequest data;
	int cnt = recv(fd, &data, sizeof(data), 0);
	if (cnt == sizeof(data))
	{
		switch (data.cmd)
		{
		case FDCMD_CLOSE:
		{
			fprintf(stderr, "close %d\n", fd);
			auto itr1 = process_fd_map.find(fd);
			if (itr1 != process_fd_map.end())
			{
				auto itr2 = itr1->second.fd_map.find(data.fd);
				if (itr2 != itr1->second.fd_map.end())
				{
					itr1->second.fd_map.erase(itr2);
				}
				else
				{
					fprintf(stderr, "Cannot find fd %d in process fd table (pid=%d)\n", data.fd, itr1->second.pid);
				}
			}
			else
			{
				fprintf(stderr, "Cannot find socket fd %d in registered fd table\n", fd);
			}
			return true;
		}
		case FDCMD_OPEN:
			fprintf(stderr, "open %d\n", fd);
			int idx;
			if (recv(fd, &idx, sizeof(idx), 0) == sizeof(idx))
			{
				auto itr1 = process_fd_map.find(fd);
				if (itr1 != process_fd_map.end())
				{
					itr1->second.fd_map.insert(std::make_pair(data.fd,idx));
				}
				else
				{
					fprintf(stderr, "Cannot find fd %d in registered fd table\n", fd);
				}
				return true;
			}
			break;
		case FDCMD_BYE:
		{
			fprintf(stderr, "Normal close %d\n", fd);
			auto itr1 = process_fd_map.find(fd);
			if (itr1 != process_fd_map.end())
			{
				process_fd_map.erase(itr1);
			}
			CloseAndUnregSocket(fd);
			return false;
		}
		case FDCMD_FORK:
		{
			fprintf(stderr, "ATFORK %d\n", fd);
			auto& themap = process_fd_map[fd].fd_map;
			int buf[2];
			for (int i = 0; i < data.fd; i++)
			{
				recv(fd, buf, sizeof(buf), 0);
				themap.insert(std::make_pair(buf[0], buf[1]));
			}
			return true;
		}
		case FDCMD_GETFD:
		{
			fprintf(stderr, "GETFD %d\n", fd);
			int buf;
			auto itr1 = process_fd_map.find(fd);
			if (itr1 != process_fd_map.end())
			{
				auto& themap = itr1->second.fd_map;
				buf = themap.size();
				send(fd, &buf, sizeof(buf), 0);
				for (auto& kv : themap)
				{
					buf = kv.first;
					send(fd, &buf, sizeof(buf), 0);
					buf = kv.second;
					send(fd, &buf, sizeof(buf), 0);
				}
			}
			else
			{
				fprintf(stderr, "Cannot find fd %d in registered fd table\n", fd);
				buf = 0;
				send(fd, &buf, sizeof(buf), 0);
			}
			return true;
		}
		}
	}
	perror("remote close");
	auto itr1 = process_fd_map.find(fd);
	if (itr1 != process_fd_map.end())
	{
		fprintf(stderr, "Finalizing process %d\n", itr1->second.pid);
		for (auto& itr2 : itr1->second.fd_map)
			CraneDerefFd(itr2.second);
		process_fd_map.erase(itr1);
		char buf[PATH_MAX];
		snprintf(buf, PATH_MAX, "/tmp/crane_process_%d.sock", itr1->second.pid);
		unlink(buf);
	}
	CloseAndUnregSocket(fd);
	return false;

}

//https://github.com/eliben/code-for-blog/blob/master/2017/async-socket-server/epoll-server.c
void UnixSocketThread()
{
	int listener_sockfd, new_socket, valread;
	struct sockaddr_un server;

	// Creating socket file descriptor 
	if ((listener_sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == 0)
	{
		perror("socket failed");
		exit(EXIT_FAILURE);
	}

	server.sun_family = AF_UNIX;
	strncpy(server.sun_path, UNIX_SOCKET_PATH, sizeof(server.sun_path));

	if (bind(listener_sockfd, (struct sockaddr *)&server,sizeof(server)) < 0)
	{
		perror("bind failed");
		exit(EXIT_FAILURE);
	}
	if (listen(listener_sockfd, 3) < 0)
	{
		perror("listen");
		exit(EXIT_FAILURE);
	}
	MakeSocketNonBlocking(listener_sockfd);
	epollfd = epoll_create1(0);
	if (epollfd < 0) {
		perror("epoll_create1");
		exit(-1);
	}

	chmod(UNIX_SOCKET_PATH, 0666);

	struct epoll_event accept_event;
	accept_event.data.fd = listener_sockfd;
	accept_event.events = EPOLLIN;
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listener_sockfd, &accept_event) < 0) {
		PerrorDie("epoll_ctl EPOLL_CTL_ADD");
	}

	epoll_event* events = (epoll_event*)calloc(MAXFDS, sizeof(struct epoll_event));
	if (events == NULL) {
		Die("Unable to allocate memory for epoll_events");
	}

	while (1) {
		int nready = epoll_wait(epollfd, events, MAXFDS, -1);
		for (int i = 0; i < nready; i++) {
			if (events[i].events & EPOLLERR) {
				PerrorDie("epoll_wait returned EPOLLERR");
			}

			if (events[i].data.fd == listener_sockfd) {
				// The listening socket is ready; this means a new peer is connecting.

				struct sockaddr_in peer_addr;
				socklen_t peer_addr_len = sizeof(peer_addr);
				int newsockfd = accept(listener_sockfd, (struct sockaddr*)&peer_addr,
					&peer_addr_len);
				if (newsockfd < 0) {
					if (errno == EAGAIN || errno == EWOULDBLOCK) {
						// This can happen due to the nonblocking socket mode; in this
						// case don't do anything, but print a notice (since these events
						// are extremely rare and interesting to observe...)
						fprintf(stderr, "accept returned EAGAIN or EWOULDBLOCK\n");
					}
					else {
						PerrorDie("accept");
					}
				}
				else {
					fprintf(stderr, "accept ok\n");

					struct sockaddr_un client_sockaddr;
					socklen_t len = sizeof(client_sockaddr);
					int rc = getpeername(newsockfd, (struct sockaddr *) &client_sockaddr, &len);
					if (rc == -1)
						PerrorDie("GETPEERNAME ERROR");
					int pid;
					int result = sscanf(client_sockaddr.sun_path, "/tmp/crane_process_%d.sock", &pid);
					if (result != 1)
					{
						Die((std::string("Bad process socket file: ") + client_sockaddr.sun_path).c_str());
					}
					fprintf(stderr, "process %d connected\n", pid);
					//MakeSocketNonBlocking(newsockfd);
					if (newsockfd >= MAXFDS) {
						Die("socket fd  >= MAXFDS");
					}

					struct epoll_event event = { 0 };
					event.data.fd = newsockfd;
					event.events |= EPOLLIN;
					ProcessFdTable table;
					table.pid = pid;
					process_fd_map.insert(std::make_pair(newsockfd, std::move(table)));
					if (epoll_ctl(epollfd, EPOLL_CTL_ADD, newsockfd, &event) < 0) {
						PerrorDie("epoll_ctl EPOLL_CTL_ADD");
					}
				}
			}
			else {
				// A peer socket is ready.
				if (events[i].events & EPOLLIN) {
					// Ready for reading.
					int fd = events[i].data.fd;
					if (OnRecv(fd))
					{
						struct epoll_event event = { 0 };
						event.data.fd = fd;
						event.events |= EPOLLIN;

						if (epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event) < 0) {
							PerrorDie("epoll_ctl EPOLL_CTL_MOD");
						}
					}
				}
			}
		}
	}
}

int main(int argc, char const *argv[])
{

	if (!CraneIsServer())
	{
		fprintf(stderr, "A server is running or crane file is not deleted.\n");
		exit(2);
	}

	struct sigaction sigIntHandler;

	sigIntHandler.sa_handler = my_handler;
	sigemptyset(&sigIntHandler.sa_mask);
	sigIntHandler.sa_flags = 0;

	sigaction(SIGINT, &sigIntHandler, NULL);

	int server_fd, new_socket, valread;
	struct sockaddr_in address;
	int opt = 1;
	int addrlen = sizeof(address);
	char buffer[1024] = { 0 };

	std::thread th = std::thread(UnixSocketThread);
	th.detach();
	
	// Creating socket file descriptor 
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
	{
		perror("socket failed");
		exit(EXIT_FAILURE);
	}

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(13579);

	// Forcefully attaching socket to the port 8080 
	if (bind(server_fd, (struct sockaddr *)&address,
		sizeof(address)) < 0)
	{
		perror("bind failed");
		exit(EXIT_FAILURE);
	}
	if (listen(server_fd, 3) < 0)
	{
		perror("listen");
		exit(EXIT_FAILURE);
	}
	for (;;)
	{
		if ((new_socket = accept(server_fd, (struct sockaddr *)&address,
			(socklen_t*)&addrlen)) < 0)
		{
			perror("accept");
			exit(EXIT_FAILURE);
		}
		RemoteRequest req;
		valread = read(new_socket, &req, sizeof(req));
		//process it
		close(new_socket);
	}
	return 0;
}
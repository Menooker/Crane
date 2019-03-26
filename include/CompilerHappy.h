#pragma once
//there are some bugs in MSVC 2017, when using Linux headers to editing the source files
//include this file to avoid annoying IDE errors
#ifdef _WIN32
#define PATH_MAX 4096
#define ESPIPE 213
#define EINVAL 123213
#define EACCES 1
#define INADDR_ANY ""
#define EAGAIN 2321
#define EWOULDBLOCK 22223
struct sockaddr_in
{
	__SOCKADDR_COMMON(sin_);
	in_port_t sin_port;			/* Port number.  */
	struct in_addr sin_addr;		/* Internet address.  */

	/* Pad to size of `struct sockaddr'.  */
	unsigned char sin_zero[sizeof(struct sockaddr) -
		__SOCKADDR_COMMON_SIZE -
		sizeof(in_port_t) -
		sizeof(struct in_addr)];
};
struct sockaddr_un
{
	int  sun_family;			/* Port number.  */
	char sun_path[255];
};
#define AF_UNIX 1221321
int htons(int);
int inet_pton(int Family,const char* pszAddrString,void* pAddrBuf);
#endif
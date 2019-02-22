#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <test.h>
#include <string.h>
#include "HookFunction.h"
#include <limits.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>

using namespace Crane;

static int main_fd;
def_name(open, int, char*, int, mode_t);


struct DeviceInfo
{

};

static int CraneOpen(char* path, int flags, mode_t mode)
{
	char p[PATH_MAX];
	int ret = CallOld<Name_open>(path, flags, mode);
	if (ret == -1)
		return ret;
	if (!strcmp(realpath(path, p), "/dev/clipboard"))
	{

	}
	return ret;
}

int CraneIsServer()
{
	return main_fd != -1;
}

void CraneInit()
{
	main_fd = creat("/dev/crane", S_IRUSR| S_IWUSR | S_IRGRP | S_IWGRP);
	if (main_fd == -1)
	{
		DoHook<Name_open>(CraneOpen);
	}
}

void CraneExit()
{
	if (main_fd != -1)
	{
		close(main_fd);
		if (remove("/dev/crane"))
		{
			perror("deleting /dev/crane failed. ");
		}
	}
}
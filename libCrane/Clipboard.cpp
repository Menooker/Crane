#include <fcntl.h>
#include <Crane.h>
#include <Devices.h>

static DeviceInfo clipboard_dev = {
	/*idx*/ 0,
	/*open*/ [](CraneFile* fd,char*, int flags, mode_t mode) {
		if (flags & O_TRUNC)
		{
			int win_server_fd = ConnectToServer(13578);
			RemoteRequest req {clipboard_dev.idx, CB_TRUNC,0};
			SendToServer(win_server_fd, &req,sizeof(req));
			uint64_t ret_size;
			RecvFromServer(win_server_fd, &ret_size,sizeof(ret_size));
			CraneCloseBypass(win_server_fd);
		}
		if (flags & O_APPEND)
		{
			fd->offset = SIZE_MAX;
		}
		return 0;
	},
	/*read*/ [](CraneFile* fd, char* buf, size_t sz)->ssize_t {
		int win_server_fd = ConnectToServer(13578);
		RemoteRequest req { clipboard_dev.idx, CB_GET,fd->offset,sz};
		SendToServer(win_server_fd, &req,sizeof(req));
		uint64_t ret_size;
		RecvFromServer(win_server_fd, &ret_size,sizeof(ret_size));
		RecvFromServer(win_server_fd, buf,ret_size);
		if (!(fd->flags & O_APPEND))
			fd->offset += ret_size;
		CraneCloseBypass(win_server_fd);
		return ret_size;
	},
	/*write*/ [](CraneFile* fd, char* buf, size_t sz)->ssize_t {
		int win_server_fd = ConnectToServer(13578);
		RemoteRequest req { clipboard_dev.idx, CB_SET,fd->offset,sz};
		SendToServer(win_server_fd, &req,sizeof(req));
		SendToServer(win_server_fd, buf, sz);
		uint64_t ret_size;
		RecvFromServer(win_server_fd, &ret_size,sizeof(ret_size));
		if (!(fd->flags & O_APPEND))
			fd->offset += ret_size;
		CraneCloseBypass(win_server_fd);
		return ret_size;
	},
	/*close*/ CraneDefaultClose,
	/*llseek*/ CraneDefaultLSeek,
	/*get_size*/ [](CraneFile* fd) -> size_t {
		int win_server_fd =  ConnectToServer(13578);
		RemoteRequest req { clipboard_dev.idx, CB_SIZE};
		SendToServer(win_server_fd, &req,sizeof(req));
		uint64_t ret_size;
		RecvFromServer(win_server_fd, &ret_size,sizeof(ret_size));
		CraneCloseBypass(win_server_fd);
		return ret_size;
	},
	/*truncate*/ [](loff_t size) -> int {
		int win_server_fd = ConnectToServer(13578);
		RemoteRequest req { clipboard_dev.idx, CB_TRUNC,size};
		SendToServer(win_server_fd, &req,sizeof(req));
		uint64_t ret_size;
		RecvFromServer(win_server_fd, &ret_size,sizeof(ret_size));
		CraneCloseBypass(win_server_fd);
		return ret_size;
	}
};

extern "C" DeviceInfo* RegisterClipboardLocal()
{
	CraneRegisterDeviceLocal("/dev/clipboard", &clipboard_dev);
	return &clipboard_dev;
}
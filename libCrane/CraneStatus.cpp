#include <fcntl.h>
#include <Crane.h>
#include <Devices.h>
#include <string>
#include <algorithm>
#include <string.h>

static std::string status_str;

static DeviceInfo cranestatus_dev = {
	/*idx*/ 0,
	/*open*/ [](CraneFile* fd, char*, int flags, mode_t mode) {
		
		status_str += "Crane Device Info\n======================\nDevice id | DLL path | Entry function\n";
		CraneIterateDevices([](int devidx, char* dll, char* fnname) {
			status_str += std::to_string(devidx);
			status_str += " | ";
			if(dll)
				status_str += dll;
			else
				status_str += "(Crane)";
			status_str += " | ";
			status_str += fnname;
			status_str += " |\n";
			return 0;
		});
		
		status_str += "Crane File Descriptor Info\n======================\nFd index | Device id | Flags | Offset | Ref count\n";
		CraneIterateFd([](int fdidx, CraneFile* f) {
			status_str += std::to_string(fdidx);
			status_str += " | ";
			status_str += std::to_string(f->device_idx);
			status_str += " | ";
			status_str += std::to_string(f->flags);
			status_str += " | ";
			status_str += std::to_string(f->offset);
			status_str += " | ";
			status_str += std::to_string(f->ref_count.load());
			status_str += " |\n";
			return 0;
		});
		return 0;
	},
	/*read*/ [](CraneFile* fd, char* buf, size_t sz)->ssize_t {
		if (fd->offset >= status_str.size())
			return 0;
		size_t ret_size = std::min(status_str.size() - fd->offset, sz);
		memcpy(buf, status_str.c_str() + fd->offset, ret_size); 
		fd->offset += ret_size;
		return ret_size;
	},
	/*write*/ nullptr,
	/*close*/ CraneDefaultClose,
	/*llseek*/ CraneDefaultLSeek,
	/*get_size*/ [](CraneFile* fd) -> size_t {
		return status_str.size();
	},
	/*truncate*/nullptr
};

extern "C" DeviceInfo* RegisterCraneStatusLocal()
{
	CraneRegisterDeviceLocal("/dev/cranestatus", &cranestatus_dev);
	return &cranestatus_dev;
}
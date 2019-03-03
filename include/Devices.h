#pragma once
#include <stdint.h>

enum ClipboardCmd
{
	CB_GET,
	CB_SET,
	CB_SIZE,
	CB_TRUNC,
};

#pragma pack(push)
#pragma pack(4)
struct RemoteRequest
{
	uint32_t dev;
	uint32_t cmd;
	uint64_t param1;
	uint64_t param2;
};
#pragma pack(pop)

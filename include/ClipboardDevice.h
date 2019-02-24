#pragma once
#include <stdint.h>

enum ClipboardCmd
{
	CB_GET,
	CB_SET,
	CB_SIZE,
};

#pragma pack(push)
#pragma pack(4)
struct ClipboardRequest
{
	uint32_t cmd;
	uint64_t param1;
	uint64_t param2;
};
#pragma pack(pop)

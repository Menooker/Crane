#pragma once
#include <stddef.h>
#include <stdint.h>

inline constexpr size_t DivideAndCeil(size_t x, size_t y)
{
	return 1 + ((x - 1) / y);
}
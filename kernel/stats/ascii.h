#pragma once

#include "asm-generic/int-ll64.h"
#include "linux/math.h"

#define PRINTABLE_ASCII_START 0x20
#define PRINTABLE_ASCII_END 0x7E
#define TOTAL_PRINTABLE_ASCII (PRINTABLE_ASCII_END - PRINTABLE_ASCII_START + 1)

#define PRINTABLE_FRACTION_NUM 95ULL
#define PRINTABLE_FRACTION_DEN 256ULL
#define PRINTABLE_FRACTION_COMPLEMENT (PRINTABLE_FRACTION_DEN - PRINTABLE_FRACTION_NUM)

u32 total_printable(unsigned char *target, size_t len);
s64 calculate_ascii_stat(unsigned char *target, size_t target_size, u64 *printable_bytes, u64 *total_bytes);

#include "ascii.h"

u32 total_printable(unsigned char *target, size_t len) {
    u32 count = 0;
    if (len == 0)
        return 0;

    for (size_t i = 0; i < len; i++) {
        if (target[i] >= PRINTABLE_ASCII_START && target[i] <= PRINTABLE_ASCII_END)
            count++;
    }

    return count;
}

s64 calculate_ascii_stat(unsigned char *target, size_t target_size, u64 *printable_bytes, u64 *total_bytes) {
    *total_bytes += target_size;
    *printable_bytes += total_printable(target, target_size);

    u64 total_bytes_temp = *total_bytes;
    u64 printable_bytes_temp = *printable_bytes;
    
    // Keep the variance product and scaled numerator within 64-bit range
    while (total_bytes_temp > (~0ULL / (PRINTABLE_FRACTION_NUM * PRINTABLE_FRACTION_COMPLEMENT))) {
        total_bytes_temp >>= 1;
        printable_bytes_temp >>= 1;
    }

    s64 numerator = (s64)(printable_bytes_temp * PRINTABLE_FRACTION_DEN) - (s64)(total_bytes_temp * PRINTABLE_FRACTION_NUM);
    u64 variance_scaled = total_bytes_temp * PRINTABLE_FRACTION_NUM * PRINTABLE_FRACTION_COMPLEMENT;
    u32 denominator = int_sqrt64(variance_scaled);
    s64 z_score = denominator ? numerator / denominator : 0;
    return z_score;
}

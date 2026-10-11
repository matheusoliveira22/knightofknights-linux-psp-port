#include "crt.h"
#include "vm.h"
#include "../plat/plat.h"
#include <stdio.h>
#include <stdarg.h>

#define RAND_STATE 0x476110u

void ms_srand(uint32_t seed) { GU32(RAND_STATE) = seed; }

int ms_rand(void)
{
    uint32_t s = GU32(RAND_STATE) * 0x343fdu + 0x269ec3u;
    GU32(RAND_STATE) = s;
    return (s >> 16) & 0x7fff;
}

int ms_wsprintf(char *dst, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(dst, 1024, fmt, ap); /* wsprintf's buffer limit */
    va_end(ap);
    return n;
}

uint32_t ms_time(void) { return (uint32_t)(plat_time_us() / 1000u); }

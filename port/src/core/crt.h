/* Behaviour-exact replacements for the MSVC CRT / Win32 helpers the game relies on. */
#ifndef KOK_CRT_H
#define KOK_CRT_H
#include <stdint.h>

/* MSVC rand(): state at 0x476110 in the original; kept in the VM image so saves of RNG state match. */
void ms_srand(uint32_t seed);
int ms_rand(void);

/* wsprintfA subset (%d %s %c %x %u with flags/width) - same output as vsnprintf for these. */
int ms_wsprintf(char *dst, const char *fmt, ...);

/* timeGetTime(): milliseconds since start. */
uint32_t ms_time(void);

#endif

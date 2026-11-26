/* What the host project provides to the local wireless module. */
#ifndef LW_PLATFORM_H
#define LW_PLATFORM_H
#include <stdint.h>

uint64_t lw_platform_time_us(void);            /* monotonic microseconds */
void lw_platform_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
uint32_t lw_platform_random(void);
int lw_platform_net_init(void);                /* sockets ready (3DS: socInit); 0 ok. Only the UDP link needs it. */

#endif

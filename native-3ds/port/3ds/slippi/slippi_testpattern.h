/* Deterministic test inputs shared by the 3DS self-test and the PC tools, so
 * each side can verify every remote pad it receives. */
#ifndef SLIPPI_TESTPATTERN_H
#define SLIPPI_TESTPATTERN_H
#include <stdint.h>
#include <string.h>

/* Pad for player index 'port' tagged frame 'frame' (8 wire bytes). */
static inline void slippi_test_pad(int port, int frame, uint8_t out[8])
{
    uint32_t x = (uint32_t)frame * 2654435761u ^ (uint32_t)(port + 1) * 0x9E3779B9u;
    out[0] = (uint8_t)((x >> 3) & 0x1F);          /* ---SYXBA */
    out[1] = (uint8_t)((x >> 9) & 0x7F);          /* -LRZUDRL */
    out[2] = (uint8_t)(int8_t)((int)(frame % 160) - 80);  /* stick X */
    out[3] = (uint8_t)(int8_t)(port ? 60 : -60);  /* stick Y */
    out[4] = (uint8_t)(x >> 17);
    out[5] = (uint8_t)(x >> 25);
    out[6] = (uint8_t)frame;                      /* L analog = frame low byte */
    out[7] = (uint8_t)(frame >> 8);               /* R analog = frame high byte */
}

/* Remote pad check: the pattern, or zeros for the sender's delay frames. */
static inline int slippi_test_pad_ok(int port, int frame, const uint8_t pad[8])
{
    uint8_t want[8];
    static const uint8_t zero[8];
    slippi_test_pad(port, frame, want);
    if (!memcmp(want, pad, 8)) return 1;
    return frame <= 15 && !memcmp(zero, pad, 8);
}

/* Fake "checksum" of a finalized frame. */
static inline uint32_t slippi_test_checksum(int frame) { return (uint32_t)frame * 0x01000193u ^ 0xC0FFEEu; }

#endif

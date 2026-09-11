#ifndef MELEE_PORT_ENGINE_H
#define MELEE_PORT_ENGINE_H
/* Keep GameCube ABI types (including its 32-bit bool) out of libctru units. */
int mp_engine_selftest(void);
void mp_engine_curve(float t, float *x, float *y);
#endif

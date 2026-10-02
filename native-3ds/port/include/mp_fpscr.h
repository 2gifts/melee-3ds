/* Floating-point control for the engine (Slippi determinism experiment,
 * docs/slippi/determinism.md). Shared by port/3ds/game_bridge.S, which sets
 * it whenever native code enters the engine, and port/engine/fp_check.c,
 * which logs and checks it at boot.
 *
 * The GameCube runs Melee with round-to-nearest and IEEE subnormals
 * (FPSCR.NI = 0); Dolphin emulates that. The 3DS VFP11 handles subnormal
 * and NaN operands in hardware only in flush-to-zero + default-NaN mode;
 * otherwise it "bounces" to support code, and Horizon reports VFP
 * exceptions to the application's exception handler instead of emulating
 * them (3dbrew, SVC KernelSetState type 6). So:
 *
 *   MP_FPSCR_RUNFAST_RN (default): FZ=1 DN=1, round to nearest. Safe on
 *     hardware. Bit-exact except where a value is subnormal (|x| < 2^-126
 *     single, 2^-1022 double), which flushes to zero, and NaN payloads.
 *   MP_FPSCR_IEEE_RN: FZ=0 DN=0, round to nearest: the console's rules.
 *     Azahar (dynarmic) honours it; real hardware would crash on the first
 *     subnormal or NaN operand. Emulator/replay testing only:
 *     build with -DMP_ENGINE_FPSCR=MP_FPSCR_IEEE_RN for both the engine
 *     and the native bridge.
 * Rounding mode is round-to-nearest in both (RMode = 0; Horizon's own
 * default for new threads is 0x03C00000, round toward zero, which libctru
 * replaces with 0x03000000 in every thread it starts).
 */
#ifndef MP_FPSCR_H
#define MP_FPSCR_H
#define MP_FPSCR_RUNFAST_RN 0x03000000
#define MP_FPSCR_IEEE_RN 0x00000000
#ifndef MP_ENGINE_FPSCR
#define MP_ENGINE_FPSCR MP_FPSCR_RUNFAST_RN
#endif
/* Control bits compared at boot: DN, FZ, RMode, Stride, Len, trap enables. */
#define MP_FPSCR_CONTROL_MASK 0x07f79f00
#endif

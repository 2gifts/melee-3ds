# Floating-point determinism (Slippi experiment)

Slippi netplay against PC Slippi Dolphin needs the port's simulation to be
bit-identical to the GameCube as Dolphin emulates it. This branch
(`slippi-math`) makes the engine's floating point round like the console.
The reference is Melee Unlocked's source port (GPL-3.0-or-later), which builds
the same decomp commit (039c4bf) with GCC and matches Slippi replays; its
annotations are carried over where they apply to clang.

Nothing here touches native 3DS code generation: only the BE8 engine build
(`tools/engine_build.py`) changed.

## 1. Contraction with GameCube rounding

The console compiler (MWCC) fuses `a*b + c` into `fmadds`/`fmsubs`/`fnmsubs`/
`fnmadds` (and the double forms). Dolphin computes a single-precision fused op
as `round_single(round_double(a*c ± b))`; since a float×float product is exact
in double, that is `(float)((double)a*(double)c + (double)b)` with ordinary,
unfused double operations. The ARM11 VFP has no fused multiply-add.

How it is built (`tools/engine_build.py`, `tools/fp_contract.py`):

* Decomp sources (everything under `upstream/`: `src/melee`, `src/sysdolphin`,
  Padclamp, the MSL maths files) and the SDK's C matrix routines
  (`build/generated/dolphin_math.c`) are compiled with `-ffp-contract=on`.
  Clang then fuses only within one expression, and marks each fusion with an
  `llvm.fmuladd` call in its front-end IR.
* The front-end IR is emitted with `-S -emit-llvm -Xclang -disable-llvm-passes`
  (`<stem>.ll`, kept for the census), rewritten in Python (`<stem>.gc.ll`), and
  only then optimized and compiled with the usual `-O2` flags (and
  `-ffp-contract=off`). Rewriting before optimization means every call still
  sits in the function whose source wrote it (no inlining yet), and constant
  folding cannot hide a negated operand. Object sizes match the old build, so
  the optimizer still runs in full. The rewriter's source and the no-contract
  list are part of the cache key.
* `llvm.fmuladd.f32(a, b, c)` → `fpext` ×3, `fmul double`, `fadd double`,
  `fptrunc`. A negated multiplicand (clang's form of `c - a*b`) becomes PowerPC
  `fnmsubs`, `-(a*b - c)`, so an exact cancellation yields −0 as on the console
  (clang's `fma(-a, b, c)` would give +0).
* `llvm.fmuladd.f64` → `mp_fma()` (software, correctly rounded; same fnmsub sign
  rule), except when both factors have few enough significant bits for the
  product to be exact in double (floats, i8/i16 conversions, short constants:
  56 of 294 sites), which become one `fmul` + one `fadd`.
* Functions listed in `tools/slippi/no_contract.txt` get a rounded multiply and
  a rounded add instead (the console fused nothing there). Entries are plain
  names, or `<object stem>::<name>` for one file's static helper; kinds `f32`
  and/or `f64`.
* Vector `fmuladd` forms are rejected with an error (none occur: the rewrite
  happens before the vectorizers run).
* Port code (`port/engine/*.c`) keeps `-ffp-contract=off`.

Verified with a test file: `MU_P(x) = ({ __typeof__(x) t = (x); t; })` blocks
fusion (the product is stored to a local before the add sees it), separate
statements are never fused, `x += a*b` and `x -= a*b` are fused, `a*b + c*d`
fuses the left product (as MWCC does), `a*b - c*d` gives
`fmuladd(a, b, -(c*d))`.

`port/include/mp_fp.h` is force-included into contracted sources: `MU_P`,
`mp_fmadds`/`mp_fmsubs`/`mp_fnmsubs` (explicit console ops, contraction off
inside), `MU_FMADDS`, `mu_*` aliases, `__fnmsubs` (used by Melee's `atanf`).

## 2. Melee Unlocked's source annotations

`tools/slippi/port_mu_annotations.py --patch <melee-native.patch>` extracts
Melee Unlocked's float annotations from its 205k-line patch and writes
`tools/slippi_edits/determinism_ported.py`:

* For every change block whose `+` side contains `MU_P(`, `mu_fnmsubs(`,
  `MU_FMADDS(`, `mu_fmadds(` or `mu_fmsubs(`, the `+` tokens with `MU_P(...)`
  unwrapped and parentheses ignored must equal the `-` tokens (a pure
  annotation, no x86 adaptation mixed in).
* The `-` text is located in this port's overlaid source, widened with the
  original file's own lines (from the hunk's line numbers) until unique.
* Result: 143 annotated blocks; 126 ported automatically (49 files); 3 are in
  headers (overlays edit only `.c` files: `lbVector_Len`, `lbVector_Len_xy`,
  `HSD_MtxColMag`, each wrapped entirely, so they are tagged in
  `no_contract.txt`); 14 were not pure and were hand-ported in
  `tools/slippi_edits/determinism.py` (`HAND`): camera `vec_len`, the
  knockback and shield-knockback decay `fnmsubs` (fighter.c), SDI stick
  threshold, Jigglypuff Rollout `fmadds`, Hyrule Castle / Kongo / Mute City
  distance squares, `hurt_len_sq` `fmadds` (lbcollision.c), `lbVector_CosAngle`.
  The MSL `math.h` hunk does not apply (newlib's math.h is used).
* Further Melee Unlocked changes found by searching its comments
  (`#ifdef MU_NATIVE` restructurings without annotation keywords) and ported by
  hand: shield drain (`ftCo_800925A4`), the inlined copy of `it_802A3C98` in
  itlinkhookshot.c (squares fused there, the out-of-line copy unfused),
  `splGetHelmite` (one multiply then three `fmadds`, made explicit; clang's
  own contraction already gives this order), and the `HSD_JObjAdd*` macros
  (`port/include/mp_jobj_fused.h`, see below).
* Not ported because clang already matches the console: `mpLineIntersection`'s
  cross products (clang fuses the first product with the rounded second, as the
  console does). Not ported as out of scope: reverb (audio), widescreen camera
  code, all MU_MATH_AUDIT / 20XX / TM-CE / x86 layout changes.
* `MU_NO_CONTRACT` tags (133) are not ported as source: the census derives the
  equivalent list for clang from the retail code (90 of Melee Unlocked's 133
  tagged functions are ones clang would fuse; all but two Flipper-item
  functions now match the console; clang does not fuse in the other 43).

## 3. Console maths library

* MSL `trigf.c` (sinf, cosf, tanf), `math.c` (logf), `math_1.c` (frexp,
  fabsf__Ff), `math_data.c`, `float.c` are compiled into the engine
  (contracted), with `port/include/mp_msl_math.h` force-included instead of
  MSL's own `math.h` (which clashes with newlib's). `__four_over_pi_m1` is
  initialized statically (the console fills it from `.ctors`).
* Melee's `lbtrigf.c` (atan2f, acosf, asinf, `atanf`, which the decomp only
  built for MWCC) and `lb_00CE.c` (expf, powf) provide those names.
* `port/engine/fp_exact.c`: `sqrtf` / `sqrtf__Ff` / `sqrtf_accurate` = MSL's
  inline algorithm: Dolphin's exact `frsqrte` table, then per step
  `t = g*g; h = 0.5*g; g = h * fnmsub(x, t, 3.0)` (double, correctly fused),
  finally `(float)(x*g)`. The order was checked against the retail inline
  copy in `lbVector_Normalize` (0x8000D31C–0x8000D364: `fmul f0,g,g`,
  `fmul f1,0.5,g`, `fnmsub f0,x,f0,3.0`, `fmul g,f1,f0`, …, `fmul`, `frsp`)
  and the 4-step `sqrtf_accurate` in `it_8027781C`. `fmodf` = the console's
  (`|b| > |a| ? a : fnmsubs(b, (float)(long long)(a/b), a)`, 0x80364340).
  `mp_fres` = Dolphin's `fres` estimate (for PSMTXInverse/PSMTXQuat).
  The Gekko intrinsics in decomp code map to these (`__frsqrte` → exact
  estimate, `__fnmsubs` → explicit op, `__fabs` → double fabs instead of the
  placeholder's float `fabsf`).
* The newlib bridges for sinf, cosf, tanf, asinf, acosf, atanf, atan2f, logf,
  sqrtf and fmodf were removed from `port/3ds/game_bridge.S`; the engine names
  stay `mp_be_*`. Port-only renderer code (gx.c) keeps the VFP square root.
  All other libc bridges are unchanged (`__aeabi_l2d`/`ul2d` bridges were
  added: contraction exposes int64→double conversions).
* The census matches the retail fused-op counts of sinf, cosf, logf, atanf,
  acosf, atan2f, expf, powf exactly.
* `mp_fma`: a fast exact path in plain double arithmetic (Dekker product,
  TwoSum, round-to-odd addition: Boldo & Melquiond, IEEE TC 2008) inside an
  exponent window where no intermediate can be subnormal or overflow (so
  flush-to-zero cannot disturb it), otherwise musl's integer algorithm
  (MIT) with integer normalization and rounding.
* Joint rotation cache (`port/engine/rotation_cache.c`): its zero shortcut
  returned `sinf(-0) = -0`; the console's MSL `sinf(-0)` is `+0`, so zeros now
  go through the cache like any other angle.

## 4. Paired-single matrix/vector routines

`port/engine/ps_math.c` (adapted from Melee Unlocked's `math/sdk_math.c`)
implements the PS routines the retail program contains (PSMTXIdentity, Copy,
Concat, Transpose, Inverse, RotTrig, RotAxisRad, Trans, Scale, Quat, MultVec,
MultVecSR, PSVECAdd, Subtract, Scale, Normalize, Mag, DotProduct,
CrossProduct) with the assembly's operation order: each `ps_madd`/`ps_msub`/
`ps_nmsub`/`ps_nmadd` is one double-rounded fused op, `ps_nmsub` keeps the
console's `-(a*b - c)` sign, and the `frsqrte` estimate is rounded to 25 bits
when it is the second multiplicand (Dolphin's `Force25Bit`). Compiled without
contraction. `tools/engine_math.py` keeps aliasing the remaining PS names
(not in the retail program) to the SDK's C versions; the C routines
(`C_MTXLookAt`, `MTXPerspective`, …) are compiled contracted like the rest of
the console's C.

## 5. FPSCR

`port/include/mp_fpscr.h`; `port/3ds/game_bridge.S` sets the engine FPSCR in
`mp_game_boot` and in every `ENGINEBRIDGE` entry (native → engine) and
restores the caller's afterwards. `port/engine/fp_check.c` logs it at boot:

    FP: engine FPSCR=0x03000000 (expected control 0x03000000, flush-to-zero, default NaN, round to nearest); ...

Findings:

* Horizon's default for new threads is 0x03C00000 (FZ, DN, round toward
  **zero**); libctru replaces it with 0x03000000 (FZ, DN, round to nearest) in
  `__system_initSyscalls` and in every thread it starts (`initThreadVars`,
  checked by disassembling libctru.a). The engine therefore already ran with
  round-to-nearest; the bridge now makes that explicit and independent of the
  calling thread.
* The GameCube runs Melee with IEEE subnormals (FPSCR.NI = 0), which Dolphin
  emulates. The ARM11's VFP11 handles subnormal operands, underflow and (with
  DN = 0) NaN operands only through "bounce" support code; Horizon does not
  emulate VFP bounces but reports them as exceptions to the application
  (3dbrew: "VFP exceptions are always reported through the exception handler
  mechanism", KernelSetState type 6). So FZ = 0 or DN = 0 would crash on the
  first subnormal or NaN operand on hardware.
* Default (`MP_FPSCR_RUNFAST_RN`, 0x03000000): safe on hardware. Bit-exact
  except where a value is subnormal (|x| < 2^-126 single / 2^-1022 double: the
  VFP flushes it to zero, the console keeps it) and in NaN payloads/signs
  (default NaN). Azahar's dynarmic honours FZ and DN, so the emulator behaves
  like hardware here.
* `build_game.py --fpscr-ieee` (isolated smoke builds only) builds with
  `MP_ENGINE_FPSCR=MP_FPSCR_IEEE_RN` (0x00000000, the console's rules) for
  emulator/replay testing; verified in Azahar (boots, all 178 vectors
  including subnormal ones pass, attract demo match runs).

## 6. Fused-op census

`python tools/slippi/fma_census.py [--engine-dir build/engine-opt] [--all]
[--function NAME] [--write-no-contract]`

Per retail function (DOL + `symbols.txt`; static names shared by several files
are resolved with `splits.txt`): single-precision fused ops (opcode 59 XO
28–31, paired-single opcode 4 XO 14/15/28–31) and double fused ops (opcode 63,
split into the inline sqrtf refinement within 24 instructions of an `frsqrte`
and the rest), versus the build's front-end IR: `llvm.fmuladd` calls per
function after the no-contract list, explicit `mp_fmadds`-style calls, our
sqrtf calls counted as 3 (4) refinement fnmsubs, plus inlined callees (static
helpers absent from the retail program, and calls to retail functions the
retail caller has fewer `bl`s to). Mismatches are sorted by gameplay relevance
(ft, lb, mp, it, gr, cm, baselib, MSL); a retail count that is a multiple of
ours is marked `unrolled?`.

`--write-no-contract` adds every function whose own body clang fuses while the
console fused nothing, and static helpers reached only from such functions.

Result now: 18824 functions compared, 712 with single-precision fused ops on
the console; single-precision counts equal in 18766, double in 18815; 63
mismatches (first census: 250). Census-driven fixes beyond the ported
annotations (all in `determinism.py`): `lbVector_8000E838` (the console fuses
`lbVector_Len` there, its vector is a local in registers),
`ftCo_Damage_OnExitHitlag` stick squares, the grab/bury/sing escape timers
(`ftCo_Bury`, `ftCo_DamageSong`, `ftCo_DamageBind`: product kept across a call,
added unfused), `lbColl_800077A0`'s `dot_diff_cb`, `it_8027781C`'s speed,
quaternion slerp `2*t` (CSE'd by the console), and the `HSD_JObjAdd*` macros
in the 4 files where the census says the console fuses `field += a*b`
(`determinism.JOBJ_FUSED`; Melee Unlocked applied them everywhere, but in
e.g. the shells and Rainbow Cruise the console rounds the product first).

Remaining mismatches (`--all`), by area:

* Known false positives: `ftCo_800CF6E8`, `ftCo_800D0CBC`, `ftCo_800D0EC8`,
  `ftCo_CalcYScaledKnockback` (the console inlines the recursive
  `ftCo_CalcYScaledKnockback` one level and keeps a `bl` for the recursion);
  `mpLib_80055E9C` and other `unrolled?` rows (loops unrolled 8×; same ops).
* CPU AI (`ftCo_0A01.c`: 6 functions): no CPUs online.
* Visual only: `ftCo_800C2600` (sword trails), `lbShadow_*`, `lb_800122F0`,
  `drawShapeAnim`, particles/generator/psdisp, TObj, SObj, menus, `ty`,
  `DrawASCII`.
* Items (off in Slippi online): `it_802E5AC4`, `it_8027C8D0`, `it_802CED54`,
  `it_802D208C` (Lugia), Flipper.
* Non-tournament stages: Corneria (8), Home-Run (5), Brinstar (3),
  Big Blue (3), Green Greens, Mute City, Kongo Jungle. No legal stage
  (Battlefield, Final Destination, Dream Land, Yoshi's Story, Fountain of
  Dreams, Pokémon Stadium) has a mismatch.
* `fn_8003F654` (plbonuslib, end-of-match bonus stats), `CObjLoad`,
  `HSD_CObjGetLeftVector` (double counts only).

## 7. Non-finite guards (tools/engine_overlays.py)

* `lbColl_80006E58` (capsule contact): rejects a contact only when
  `allowed_distance`, `axis.x` or the already-written `out_contact_pos` is
  non-finite; it reads values computed earlier in the function. For finite
  inputs the result is unchanged. With a non-finite intermediate the console
  would compare NaN (false) and go on to accept the contact, while the port
  rejects it: a divergence only in that case.
* `lb_8000B1CC` after `HSD_JObjSetupMatrix`: rebuilds the matrix chain only
  when `mtx[0][0]` is non-finite. No effect for finite matrices; again a
  divergence only where the console itself would carry NaNs.
* Both were left in place. They were added for NaNs seen on the old maths;
  with console rounding they should not trigger. Any "Invalid capsule contact
  rejected" line in game.log now points at a remaining determinism bug (none
  appeared in the test runs).

## 8. Verification

* Boot self-test (`fp_check.c`, every build): 103 `fma` vectors and 75 sqrtf /
  sqrtf_accurate vectors from `tools/slippi/fp_vectors.py`, an independent
  Python model (exact rationals for fma; the frsqrte table re-implemented),
  plus the existing frsqrte vectors; it panics on a mismatch. Vectors with
  subnormal operands/results are skipped in flush-to-zero mode (172 checked;
  178 in the IEEE build). It also logs the cost per 256 calls in Azahar:
  MSL sqrtf 19131 ticks (40.5 MHz), VFP sqrt 995, MSL sinf 5088, mp_fma 5171.
* Host test of `mp_fma` (fast and integer paths) against UCRT `fma`: 6 million
  cases (random, cancellation, sqrtf Newton steps, near-tie products, full
  exponent range), 0 mismatches.
* MSL sqrtf equals the correctly rounded square root on all 150,000 sampled
  inputs in [0.5, 8): the old VFP sqrt was rarely wrong, but the argument
  contraction around it was.
* Generated code checked: `ft_800CB6EC` compiles to `vcvt.f64.f32`, `vmul.f64`,
  `vsub.f64`, `vcvt.f32.f64`, `vneg.f32` (the retail `fnmsubs` at 0x800CB76C);
  `lbVector_Normalize` squares unfused then calls `mp_be_sqrtf`.
* Dev build boots in Azahar, runs the title/menus and the attract-demo match
  for 150–180 s with no PANIC, no invalid-capsule or matrix-repair lines.
  `--fpscr-ieee` build likewise.
* Performance (Azahar): with the rate forced to 60 the match runs at 59.8 FPS
  (p95 17.3 ms; old build 17.6 ms). In `auto` mode one run of the new build
  dropped to the 30 FPS cap at the match-load spike and, the emulator's
  spinning translator counting as busy, never returned; another run stayed at
  60. In a match the game makes about 270 sqrtf and 2,500 `mp_fma` calls per
  frame (most fma calls come from decomp's own inline Newton code); at the
  emulated costs above that is roughly 1–1.5 ms per frame. Hardware numbers
  are in game.log (`FP: cost per 256 calls`).

## 9. Open risks

* The census compares counts, not pairing: it cannot see which product a sum
  fuses or the order of a sum. MWCC's choices differ from clang's in patterns
  found so far: squares of values loaded from memory are not fused; a product
  kept across a call is added unfused; a product the console computed once
  for two uses (CSE) is not fused. Functions that match by count could still
  pair differently. A replay harness is the real test.
* Static inline helpers get one contraction policy for all their callers,
  while the console decides per inlined copy (`lbVector_Len`: unfused from a
  pointer, fused for a local in registers). Fixed where the census shows it.
* Subnormals flush to zero and NaNs are default NaNs on hardware (see §5).
  Float-to-int of NaN: ARM gives 0, `fctiwz` gives 0x80000000.
* Dolphin rounds the second factor of a single-precision multiply to 25 bits
  when it holds a non-single value. Only `ps_math.c` models this (the
  `frsqrte` estimate); C code always rounds to float before single ops.
* Slippi Dolphin's `frsqrte`/`fres` are assumed to be Dolphin's current exact
  table models (used by this port and Melee Unlocked).
* `port/engine/ucf.c` (UCF 0.84) computes `x*x + y*y < t*t` in C without
  contraction; the Gecko code's own rounding was not checked here. Other
  port-side gameplay code (tap jump, offline defaults) is out of scope.
* `HSD_JObjAdd*` macros, header annotations and the no-contract list are
  name-based; new decomp functions with the same names would inherit them.
* Census false positives listed above are judged by reading the retail code,
  not proven per function.

## Files

* `tools/engine_build.py`, `tools/fp_contract.py`, `tools/engine_math.py`,
  `tools/build.py` (placeholder overrides), `tools/build_game.py`
  (`--fpscr-ieee`)
* `tools/slippi_edits/determinism.py` (hand edits, `JOBJ_FUSED`),
  `tools/slippi_edits/determinism_ported.py` (generated)
* `tools/slippi/no_contract.txt`, `tools/slippi/fma_census.py`,
  `tools/slippi/port_mu_annotations.py`, `tools/slippi/fp_vectors.py`
* `port/include/mp_fp.h`, `mp_fpscr.h`, `mp_msl_math.h`, `mp_jobj_fused.h`
* `port/engine/fp_exact.c`, `ps_math.c`, `fp_check.c`, `rotation_cache.c`,
  `os.c` (boot check call)
* `port/3ds/game_bridge.S`

## Maintenance

After changing decomp edits: build, then `python tools/slippi/fma_census.py`.
For a function the console does not fuse at all, `--write-no-contract`. For a
partial mismatch, compare the retail code (`tools/disassemble_dol.py`, or the
census `--function NAME`) and add an `MU_P(...)`/`mp_fmadds` edit to
`determinism.py`. Re-running `port_mu_annotations.py` regenerates
`determinism_ported.py` from the Melee Unlocked patch.

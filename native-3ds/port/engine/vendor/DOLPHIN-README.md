The reciprocal-square-root table and algorithm in `../ppc_math.h` are a C
adaptation of Dolphin Emulator's `Common::ApproximateReciprocalSquareRoot`.
This reproduces the PowerPC estimate's result bits, including special values;
it does not replace the estimate with a full-precision square root.

Source: https://github.com/dolphin-emu/dolphin/blob/a2efdf1197be8132674b90fe9cf4761df39752ed/Source/Core/Common/FloatUtils.cpp
Revision: `a2efdf1197be8132674b90fe9cf4761df39752ed`.
Copyright 2018 Dolphin Emulator Project. GPL-2.0-or-later; the original
license is included as `DOLPHIN-LICENSE.txt`.

The fixture `tests/fixtures/frsqrte.json` combines that revision's 57 input
values from `Source/UnitTests/Core/PowerPC/TestValues.h` (copyright 2021)
and expected outputs from `Source/UnitTests/Common/FloatUtilsTest.cpp`
(copyright 2018). Both are GPL-2.0-or-later. The JSON is an adapted test-data
format. Upstream files are identified by SHA-256 in the fixture.

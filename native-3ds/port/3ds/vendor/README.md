`citro3d_internal.h` is an unmodified copy of devkitPro/citro3d `source/internal.h`
at commit `9f21cf7b380ce6f9e01a0420f19f0763e5443ca7`.
SHA-256: `3689d9f861f2860ba8e7814c170f08fa2e4d11fc0de26936c352900810f86d8b`.
Its original license is in `CITRO3D-LICENSE.txt`.

The port's separate `citro3d_fix.c` implements safe texture-unit unbinding for
the pinned SDK, whose C3D_TexBind dereferences a null texture on units 1/2.
Compile-time assertions check the private context offsets against the linked
SDK's ARM implementation. Recheck these offsets when changing the SDK.

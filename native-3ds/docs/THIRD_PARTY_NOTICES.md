# Third-party notices

The native port uses these libraries and sources. Their source remains available from the linked repositories.

## libctru

https://github.com/devkitPro/libctru


  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any
  damages arising from the use of this software.

  Permission is granted to anyone to use this software for any
  purpose, including commercial applications, and to alter it and
  redistribute it freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you
     must not claim that you wrote the original software. If you use
     this software in a product, an acknowledgment in the product
     documentation would be appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and
     must not be misrepresented as being the original software.
  3. This notice may not be removed or altered from any source
     distribution.

## citro2d

https://github.com/devkitPro/citro2d

Copyright (C) 2017-2018 fincs

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you
   must not claim that you wrote the original software. If you use
   this software in a product, an acknowledgment in the product
   documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and
   must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source
   distribution.

## citro3d

https://github.com/devkitPro/citro3d

Copyright (C) 2014-2018 fincs

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any
damages arising from the use of this software.

Permission is granted to anyone to use this software for any
purpose, including commercial applications, and to alter it and
redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you
   must not claim that you wrote the original software. If you use
   this software in a product, an acknowledgment in the product
   documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and
   must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source
   distribution.

## Additional provenance

The port adapts Dolphin Emulator's PowerPC reciprocal-square-root estimate
and table from `Common/FloatUtils.cpp`, revision
`a2efdf1197be8132674b90fe9cf4761df39752ed`, under GPL-2.0-or-later.
Copyright 2018 Dolphin Emulator Project. The reference input vectors are
copyright 2021 Dolphin Emulator Project under the same terms. Source URLs,
test-data provenance and the original license are in `port/engine/vendor/`.
The altered C implementation is in `port/engine/ppc_math.h`; it is not a
full Dolphin emulator integration. Packages include `DOLPHIN-LICENSE.txt`.

The port vendors Citro3D's unmodified private `internal.h` at commit `9f21cf7b380ce6f9e01a0420f19f0763e5443ca7` to implement a local null-texture binding fix against the pinned SDK. Provenance, the exact header hash and the original license are in `port/3ds/vendor/`; `citro3d_fix.c` is the separately identified port adaptation. The update package includes `CITRO3D-LICENSE.txt`.

The [Super Mario 64 3DS Ultimate renderer](https://github.com/Epic0522/Super-Mario-64-3ds-port---Ultimate/blob/master/src/pc/gfx/gfx_citro3d.c) was studied as a reference for stereoscopic projection and flat HUD separation. This port implements its own Melee camera convergence, shared geometry submission and paired render targets; no Mario assets are included.

UCF algorithms are credited to tauKhan, Altimor, PracticalTAS, CarVac and Krohnos. The native implementation adapts [Altimor's UCF source](https://github.com/AltimorTASDK/ucf) at `5634468e00c2b43e8c9c402970caf562c15a0d0d` and the [Slippi 0.84 integration](https://github.com/project-slippi/slippi-ssbm-asm) at `fcf47f10dc244152c2ebaa3a9dec142ea42243b7`. Offline defaults also reference Slippi's original codes. These ARM adaptations are not an official Slippi build. The Slippi checkout retains its GPL-3.0 license; upstream notices remain applicable.

Optional simplified stage assets are from [Diet Melee Classic](https://diet.melee.tv/), patched locally from the user's ISO. Original settings and selected animation data are restored for native compatibility; these modifications are checked by the preparation tools and their generated archive audits. The official 1.0.3 patch's stage archives were verified identical to the 1.0.2 inputs. The user's original disc assets are preserved separately. No game data has been published or uploaded.

The original RNG and spline routines are from the pinned doldecomp/melee checkout. The CRT, newlib and libgcc binaries are from the official devkitPro image recorded in toolchain.lock.json. Upstream notices and licensing are not replaced by this document.

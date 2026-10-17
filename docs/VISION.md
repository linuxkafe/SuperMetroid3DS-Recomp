# Vision

## Problem

The Super Metroid static recompilation ([SuperMetroidRecomp](https://github.com/mstan/SuperMetroidRecomp))
runs on PC (Windows, Linux, macOS, Android) through SDL. There is no Nintendo 3DS
port. The 3DS is a very different machine — ARM11, VFPv4, dual screens, a
homebrew loader rather than an OS — so running the recompiled game logic there
means writing a new host layer while leaving the game itself alone.

3DS homebrew players and preservationists have no way to play this faithful
recompilation on 3DS hardware. Existing SNES emulators there run by interpretation
and do not have the headroom for Super Metroid's PPU, APU and DMA behaviour. A
static recompilation port would run the original compiled code natively.

## Solution

Port the recompiled core to 3DS using devkitPro/devkitARM and libctru:

1. **Own the host layer.** All 3DS-specific code lives in `frontend/3ds/` (video,
   input, audio, frame loop) and `platform/3ds/` (replacing the framework's SDL
   include boundary). Upstream's SDL entry point is excluded, not edited.
2. **Keep upstream read-only.** Consumed from outside the repository, never
   vendored, never modified in place. Changes that cannot be expressed in the
   frontend go to `patches/`, which is currently empty.
3. **Generate the core from the ROM.** `src/gen/` (~88 MB of C, 116 units) comes
   from upstream's `v2_regen.py` and is never committed, because it is derived
   from copyrighted ROM data.
4. **Gate honestly.** `SM_WITH_CORE` defaults off until the fiber backend exists,
   and the build says so on screen rather than producing a binary that builds and
   misbehaves.

The recompiled game logic (`src/gen/`, `recomp/*.cfg`) is **not modified**. It is
upstream's ground truth.

## Value

- **Preservation** — faithful Super Metroid on 3DS hardware, by recompilation
  rather than emulation
- **Portability evidence** — the snesrecomp framework reaching a fourth embedded
  target, with the host-layer cost written down
- **A reusable pattern** — read-only upstream plus an owned frontend is now
  proven on two ports in this workspace (SimCity and this one)

## What "done" means

Not "it boots". Specifically:

- The core is linked and runs (needs T027)
- A frame from this port matches the upstream PC build's output for the same ROM
  and input sequence (T030)
- Frame time is measured, not extrapolated (T026)

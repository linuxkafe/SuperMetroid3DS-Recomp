# Design — Nintendo 3DS Hardware

Spec for the target machine, and what each part means for this port. Every
figure here is a platform constant or a measured build result; nothing is a guess
about how fast anything runs.

## Memory

| Region | Size | Use here |
|---|---|---|
| App linear memory | 64 MB | The recompiled core, runtime, frame buffers |
| System RAM | 256 MB total | OS, apps |
| VRAM | ~6 MB usable | 3D GPU, textures |
| System shared | 128 MB | Services (gfx, ndsp, hid, soc) |

This is the decisive difference from the DS, where a 4 MB static ROM buffer did
not fit a 3.5 MB ARM9 region and ended that attempt. **The core's footprint is
not yet measured** — the ~28 MB figure in `3DS-PORT-STUDY.md` is extrapolated from
one compiled bank (T025).

## CPU

| | DS (abandoned) | 3DS |
|---|---|---|
| Core | ARM9 @ 67 MHz | ARM11 @ 268 MHz |
| Cores | 1 | 4 |
| FPU | none | NEON + VFPv4 |
| Multilib | ARM946E-S | `armv6k/fpu` |

Build flags: `-march=armv6k -mfloat-abi=hard -mfpu=vfp`.

The FPU matters for the core: NTSC timing arithmetic uses `double`, and the DS
ARM9 has no hardware for it. That is a reason to expect the 3DS to be faster, and
it is **not** a measurement. No frame-time number for this port exists.

### The fiber problem

`snesrecomp`'s scheduler switches cooperatively between contexts with their own C
stacks. Its POSIX backend is `ucontext_t`-based, and:

```
$ echo '#include <ucontext.h>' | arm-none-eabi-gcc -c -march=armv6k -
fatal error: ucontext.h: No such file or directory
```

Options, none chosen (T027): a hand-written ARM11 context switch, real threads via
`pthread.h` (available, but changes snapshot semantics), or stubbing — rejected,
because a stubbed scheduler yields a binary that builds and misbehaves.

## Screens

| Screen | Resolution | Role |
|---|---|---|
| Top | 400 × 240 (2D framebuffer) | The SNES composite |
| Bottom | 320 × 240, touch | Console, later the HUD |

Both are initialised `GPU_RGB565`. The SNES composite is already RGB565, so no
colour conversion happens on the way to the panel.

Scaling is **integer only**, at least 1:1, centred. A fractional scale would need
a resampler, and nearest-neighbour on 256 × 224 pixel art at 1.5× shimmers
visibly. Half-pixel letterbox bars cost less than a resampler that can differ
between runs.

Dimensions come from `gfxGetFramebuffer`, not from the constant 400 × 240 — a
2DS would report differently.

## Input

HID → SNES pad word, active low (a set bit means released). Built from
`SM_PAD_NEUTRAL` with pressed bits cleared, so the polarity lives in one place
rather than being inverted at each call site.

D-pad, A/B/X/Y, L/R, START, SELECT map straight across. The C-stick and touch
screen are **not** mapped — the SNES has no analogue control, and mapping them
would be inventing behaviour.

## Audio

| | Value |
|---|---|
| Core (LLE APU) output | 32,040 Hz stereo S16 |
| 3DS DAC | 48,000 Hz stereo |
| Interface | `ndsp`, channel 0, linear interpolation |

**No resampler.** The mismatch is real and is deferred deliberately to T029: a
resampler is DSP with its own edge cases, and a wrong pitch is worse than a known
gap.

libctru 2.7 exposes `ndspChnIsPlaying` but no "samples remaining", so the drain
wait covers a whole buffer — up to ~6 ms of latency at the current buffer size.

## Networking

3DS **does** have BSD sockets, via libctru's `soc` service. This corrects an
earlier conclusion in this project that it did not; see the retraction in
`3DS-PORT-STUDY.md`.

Port allocation is shared with a sibling project on the same machine:

| Port | Owner |
|---|---|
| 5002 | SimCity-3DS-Port — **not this port** |
| 5003 | this port, command protocol |
| 5004 | this port, GDB RSP |

## Toolchain

| Thing | Value |
|---|---|
| Image | `devkitpro/devkitarm` |
| Compiler | devkitARM 16.1.0, GCC |
| Library | libctru 2.7.0 (`libctru.a` — there is no separate `perf3ds` any more) |
| Toolchain file | `/opt/devkitpro/cmake/3DS.cmake` |
| Packager | `3dsxtool` |

Two traps recorded here because both cost time:

- devkitPro moved its cmake files to `/opt/devkitpro/cmake/`, not
  `$(DEVKITARM)/cmake/Platform/`.
- Do **not** pass `-specs=3dsx.specs` yourself. `devkitARM.cmake` already passes
  it, and passing it twice makes gcc process `sync-dmb.specs` (which
  `3dsx.specs` includes) twice: *"attempt to rename spec 'link' to already
  defined spec 'sync_sync_link'"*.

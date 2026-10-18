# 3DS Port Study — SuperMetroidRecomp → Nintendo 3DS

**Date:** 2026-10-17
**Ticket:** T024
**Status:** ✅ FEASIBLE for the frontend — ❌ core not yet linkable (one blocker, T027)

Every number in this document was measured on this machine. Where something is
estimated rather than measured, it says so.

---

## Executive summary

The Super Metroid static recompilation **compiles for 3DS ARM11 almost entirely
as-is**. 71 of 80 framework and game translation units pass an ARM11 syntax check
with zero errors. The nine that do not fail for host reasons, not algorithmic
ones, and eight of the nine are simply excluded.

One genuine blocker remains: the framework's cooperative fiber scheduler needs
`<ucontext.h>` on its POSIX path, and the 3DS newlib environment does not provide
it. Until that is solved the core cannot be linked, so the port ships a frontend
that boots and renders but does not yet run the game. That is stated on the
device screen rather than hidden.

This stands in contrast to the DS attempt, which hit two memory-budget walls and
stopped: a 4 MB static ROM buffer on a 3.5 MB ARM9 region, and an ARM9 binary
over the 3.5 MB limit. Neither applies to 3DS.

---

## Hardware comparison

| | Nintendo DS (abandoned) | Nintendo 3DS (this port) |
|---|---|---|
| **CPU** | ARM9 @ 67 MHz, single core | ARM11 @ 268 MHz, quad core |
| **FPU** | none | NEON + VFPv4 |
| **Main RAM** | 4 MB (3.5 MB usable region) | 64 MB (app) of a 256 MB system |
| **VRAM** | 656 KB total | ~6 MB usable |
| **Cartridge / storage** | flashcart + SD | microSD, ~128 MB homebrew |
| **Top screen** | 256 × 192 | 400 × 240 (2D framebuffer) |
| **Bottom screen** | 256 × 192, touch | 320 × 240, touch |
| **Audio** | mono | stereo 48 kHz |
| **Homebrew format** | `.nds` | `.3dsx` |
| **Networking** | none (lwIP optional) | BSD sockets via libctru `soc` |

The DS numbers come from the project's own kanban, which recorded both blockers
before the port was abandoned. They were not re-measured here.

---

## Measured: toolchain

Present in `devkitpro/devkitarm:latest` with no additional installation:

| Tool | Value |
|---|---|
| `arm-none-eabi-gcc` | 16.1.0 |
| multilib | `armv6k/fpu` |
| `libctru` | 2.7.0 (single `libctru.a`; there is no separate `perf3ds` any more) |
| `3dsxtool` | present |
| toolchain file | `/opt/devkitpro/cmake/3DS.cmake` |

> **Note for anyone porting an older 3DS project:** devkitPro moved its cmake
> toolchain files to `/opt/devkitpro/cmake/`, not
> `$(DEVKITARM)/cmake/Platform/`. The SimCity3DS Makefile records the same trap.

---

## Measured: ARM11 compile survey

Command (read-only upstream, no ROM needed):

```
arm-none-eabi-gcc -fsyntax-only -mcpu=mpcore -march=armv6k \
  -mfloat-abi=hard -mfpu=vfp -O0 -std=gnu11 \
  -Isnesrecomp/runner/src -Isnesrecomp/runner/src/desktop -Isrc -Irecomp \
  <file>
```

**Result: 71 passed, 9 failed.**

### The nine failures, and what each one is

| File | Cause | Resolution |
|---|---|---|
| `common_rtl.c` | `desktop/sdl_compat.h` → `<SDL.h>` | `platform/3ds/sdl_compat.h` (this repo) |
| `src/sm_rtl.c` | `funcs.h` not generated | `make regen` (also produces `src/gen`) |
| `keybinds.c` | `<SDL.h>` | excluded — SDL keyboard |
| `snes_osd.c` | `<SDL.h>` | excluded — SDL text rendering |
| `snes_savestate_menu.c` | `<SDL.h>` | excluded — SDL menu |
| `host_report.c` | `<sys/utsname.h>` | excluded — no 3DS equivalent |
| `debug_server.c` | `<sys/socket.h>` | **held back, not a real blocker — see below** |
| `lua_bridge.c` | `<lua.h>` | excluded — Lua modding |
| `setup_host_dispatch.c` | `#error` outside setup-host builds | excluded by design |

### A retraction

`debug_server.c` was initially recorded here as blocked because the 3DS has no
`<sys/socket.h>`. **That was wrong.** It was a bad probe: the compiler was run
without `-I$DEVKITPRO/libctru/include`, and libctru ships BSD sockets backed by
the 3DS `soc` service. A follow-up probe compiled `socket`/`bind`/`listen`
against libctru successfully.

This matters beyond tidiness: it means the TCP debug bridge (T028) is viable,
which is what makes the port allocation of 5003/5004 reachable. `debug_server.c`
is held back for a different and weaker reason — it is a host debugging surface,
not part of a shipping port, and `socInit` ownership belongs to the frontend.

### A second retraction — the regen entry point

The regen command recorded here ran `snesrecomp/tools/v2_regen.py` directly.
**That is not the supported entry point.** It emits nothing when the generated
code needs stubs for unresolved dispatches — the tooling refuses with
*"Stubs are a hard build error. Close the recompiler-level gap that produced
each marker; do NOT add an allowlist."* — and the run produced **zero** files
after ~25 minutes before dying.

The supported entry point is `tools/regen.sh`, which wraps `snesrecomp_cli.py
generate` with `--funcs-h recomp/funcs.h`, the ROM digest check, and the
`--cfg-roots` / `--strict-idempotent` flags that make the output reproducible.
`make regen` now uses it.

It also writes into the tree it is run from, so it runs in a writable
`$(WORKTREE)` — a copy of the read-only upstream — leaving upstream untouched.

---

## Measured: generated core size (T025 — done)

**Measured, not extrapolated.** All 338 generated units compile for
`armv6k/fpu` with zero errors.

| Quantity | Value |
|---|---|
| `src/gen/*.c` translation units | **338** |
| Total generated C | **209,614,072 bytes** (~199.9 MiB) |
| Total object code | **184,173,148 bytes** (~175.6 MiB) |
| Largest single unit | `bank94_v2.o` — 4,054,904 bytes |
| Next largest | `bank81_v2.o` — 3,220,076 bytes |
| Compile time (338 units, ~8 parallel) | ~25 minutes |
| Failures | **0** |

**The earlier figures were wrong, twice over.**

1. This document previously claimed "near 28 MB", extrapolated from **one**
   bank. The real total is **184 MB — 6.6× higher**. The extrapolation was not
   merely imprecise; it was optimistic in the wrong direction, and it is the
   kind of number that gets quoted as fact. Replaced.
2. The v0.3.8 checkout had 116 units totalling 87.6 MB of C. The regenerated
   tree has **338** units totalling **200 MB**. Comparing generated trees across
   versions by unit count is meaningless — the sharder changed.

### What 184 MB means

The 3DS app region is 64 MB. **184 MB does not fit.** This is not a port
failure and not a blocker yet, because:

- The frontend link already uses `--gc-sections`, and the live code after
  linking will be far smaller than the sum of every `.o`.
- The `.o` sum includes dead and unreferenced code that the linker drops.

But it is a **measured fact that must not be smoothed over**, and it drives
T032: the linked size after `--gc-sections` is the number that decides whether
the port fits, and it is not yet measured. If the linked core exceeds 64 MB,
the port does not fit on 3DS either and the whole retarget is wrong.

### What would falsify this

- A link that exceeds the 64 MB region even after `--gc-sections` (T032).
- A unit that fails to compile. There are none today; that is a measurement, not
  a guarantee.

### The regen that produced this

`make regen` runs `tools/regen.sh` (the supported entry point) in a writable
`$(WORKTREE)`, pointing at a prebuilt native analyzer via
`SNESRECOMP_NATIVE_ANALYZER`. Two traps were hit and recorded:

1. `v2_regen.py` directly is **not** the supported entry point — it refuses to
   emit when generated code needs stubs ("Stubs are a hard build error"), and
   produced zero files.
2. The native analyzer is a Rust 1.85 crate and no Rust toolchain exists on this
   host. It is built once in a Rust container and cached at
   `SuperMetroid3DS-build/analyze/snesrecomp-analyze`. The retired
   `--analysis-backend python` flag does not work.

---

## The blocker: no `ucontext.h`

```
$ echo '#include <ucontext.h>' | arm-none-eabi-gcc -c -march=armv6k -
fatal error: ucontext.h: No such file or directory
```

`snesrecomp`'s scheduler (`common_rtl.c` / `sm_rtl.c`) switches cooperatively
between a scheduler context and one context per task slot, each with its own C
stack. The POSIX backend is backed by `ucontext_t`. The 3DS has no such header.

Three ways out, none chosen yet:

1. **Hand-written ARM11 context switch** in inline assembly. Real work, fully
   under our control, and the only option that keeps the scheduler's semantics
   exactly.
2. **Real threads** (`pthread.h` *is* available) with a scheduler that yields via
   condition variables. Changes the scheduler's timing characteristics and its
   snapshot semantics — `FiberSnapshot*` is explicitly ucontext-backed and reports
   unsupported elsewhere.
3. **Stub the scheduler.** Rejected: it produces a binary that builds and
   misbehaves, which is the failure mode this port has explicitly chosen to avoid
   (see `SM_WITH_CORE` defaulting to OFF).

**T027.**

---

## Architecture

### Screens

```
Top screen (400x240, RGB565)  — the SNES composite, integer-scaled and letterboxed
Bottom screen (320x240)       — console: status, and the game's own HUD later
```

RGB565 on both screens because the SNES composite is already RGB565. No colour
conversion on the way to the panel, which is both the fastest path and the one
that cannot introduce a colour bug.

### Frame flow

```
frontend/3ds/3ds_main.c
  ├── SmInputPoll()      HID  ->  SNES pad word (active low)
  ├── [core]            smrecomp_frame_begin()  <-- NOT LINKED YET (T027)
  ├── SmRenderPresent()  RGB565 blit, integer scale, letterbox
  └── SmAudioSubmit()    core PCM -> ndsp
```

Aspect, scanline count and HUD policy stay in the core (`sm_display.c`,
`sm_video.c`). The frontend does not second-guess them. The DS port duplicated
that logic and ended up with two sources of truth for the same question.

### Input mapping

| 3DS | SNES |
|---|---|
| D-pad | D-pad |
| A | A |
| B | B |
| X | X |
| Y | Y |
| L / R | L / R |
| START | START |
| SELECT | SELECT |

Built from `SM_PAD_NEUTRAL` with pressed bits cleared, so the active-low polarity
lives in exactly one place.

### Audio

The core's LLE APU emits 32,040 Hz stereo S16; the 3DS DAC runs at 48,000 Hz.
**No resampler is implemented.** That is a deliberate deferral, not an oversight:
a resampler is DSP with its own edge cases, and getting it wrong is audible in a
way that a dropped frame is not. Until there is a measured reason, the port runs
the core's rate into the DAC and records the mismatch as T029.

libctru 2.7 has `ndspChnIsPlaying` but no "samples remaining" query, so the DAC
drain wait is for the whole buffer — up to ~6 ms of added latency at the buffer
size used.

---

## Port allocation

| Port | Owner | Purpose |
|---|---|---|
| 5002 | **SimCity-3DS-Port** | command protocol — do not touch |
| **5003** | **this port** | command protocol |
| **5004** | **this port** | GDB RSP |

Two builds on one Deck, both bridges on 5002, cross-connect and produce
measurements that look real and are not.

---

## What CI does and does not cover

`make test` and CI both run the frontend gate: boot, render N frames, terminate at
exactly frame N.

**Neither builds the core.** `src/gen` is ~88 MB of C derived from the ROM, the
ROM is not in the repository by design, and regenerating it takes far longer than
a CI runner should spend. The CI job says this in its own log rather than
implying coverage it does not have.

---

## What would falsify this document

- Compiling all 116 generated units and finding the total `.o` sum materially
  above 64 MB (T025). The 28 MB figure is extrapolated from one bank.
- Writing the ARM11 fiber (T027) and finding the scheduler needs semantics a
  hand-written switch cannot reproduce — in particular `FiberSnapshot*`.
- A frame-comparison failure against the upstream PC build's output (T030). Every
  claim here is about *compiling and running*; none of them is about *correct
  output*.
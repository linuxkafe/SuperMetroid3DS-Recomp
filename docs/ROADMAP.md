# Roadmap

## Sprint 1: Foundation & Build System [HIGH]

| ID | Task | Impact | Effort | Status |
|----|------|--------|--------|--------|
| T001 | Initialize devkitARM CMake/Makefile, produce minimal `.nds` | High | Medium | todo |
| T002 | Set up submodule structure for SuperMetroidRecomp + snesrecomp | High | Low | todo |
| T003 | Port memory map: define ARM9/ARM7 memory regions, pools, arenas | High | High | todo |
| T004 | Implement minimal ARM9 entry point + VBlank handler | High | Medium | todo |

## Sprint 2: CPU Runtime Port [HIGH]

| ID | Task | Impact | Effort | Status |
|----|------|--------|--------|--------|
| T005 | Port `sm_rtl.c`: single-fiber frame model to DS (ARM9 main loop) | High | High | todo |
| T006 | Port NMI/IRQ/HDMA handling to DS hardware timers + VBlank | High | High | todo |
| T007 | Port `sm_cpu_infra.c`: RtlGameInfo registration for DS | High | Medium | todo |
| T008 | Integrate recompiled banks (`src/gen/`) into DS build | High | Medium | todo |

## Sprint 3: Video Subsystem [HIGH]

| ID | Task | Impact | Effort | Status |
|----|------|--------|--------|--------|
| T009 | Implement PPU → DS BG/OBJ/Mode7 renderer (libnds) | High | High | todo |
| T010 | Dual-screen layout: main gameplay + sub minimap/HUD | High | Medium | todo |
| T011 | Palette management (SNES 15-bit → DS 15-bit) | Medium | Medium | todo |
| T012 | DMA-driven VRAM updates, double-buffering | High | Medium | todo |

## Sprint 4: Audio Subsystem [HIGH]

| ID | Task | Impact | Effort | Status |
|----|------|--------|--------|--------|
| T013 | Port `sm_spc_player.c`: SPC700 upload interception → ARM7 | High | High | todo |
| T014 | Implement ARM7 audio mixer (maxmod): PSG + PCM | High | High | todo |
| T015 | Audio sync with VBlank, sample rate conversion (32kHz → 48kHz/32kHz) | Medium | Medium | todo |

## Sprint 5: Input, Save, Polish [MEDIUM]

| ID | Task | Impact | Effort | Status |
|----|------|--------|--------|--------|
| T016 | Input mapping: DS keypad → SNES controller | Medium | Low | todo |
| T017 | SRAM save/load via fatfs (SD) or cartridge EEPROM | Medium | Medium | todo |
| T018 | Boot ROM selection menu (file browser) | Medium | Medium | todo |
| T019 | Optimization pass: profile, reduce RAM, improve frame time | Medium | High | todo |
| T020 | Regression testing: attract demo, new game, doors, save | High | Medium | todo |

## Backlog

- [DISCOVERED] ARM7/ARM9 IPC for audio commands
- [DISCOVERED] HDMA timing accuracy on DS timer hardware
- [DISCOVERED] Mode 7 rendering on DS 2D engine limitations
- [DISCOVERED] Generated code size vs. 4MB RAM constraint
- [DISCOVERED] **ROM residency — BLOCKS ANY ROM.** `src/main.c:19` allocates a 4 MB
  static buffer on a 3.5 MB ARM9 region. Measured ROM is 3 MB (expanded, not the 512 KB
  LoROM ceiling), so no resident-buffer configuration fits. Needs cartridge-resident
  (simplest, fastest) or SD bank-streaming (slow, hardware-dependent). Design and
  measurements: `aes/design/DS-memory-budget-and-rom-residency.md`
- [RESOLVED — partially] **Link failure: shim missing `common_rtl.c` surface.**
  Was 15 distinct undefined symbols (40 references). The non-APU group (`g_ppu`/`g_dma`/`g_sram`/`g_sram_size`,
  `RomPtr`, `WriteReg`, `WriteRegWord`, `ReadReg`, `ReadRegOpenBus`, `ReadRegWord`) is
  now ported in `src/ds_rtl_regs.c`. **6 APU symbols remain** (`RtlApuWrite`,
  `RtlAudioOutputRate`, `rtl_sync_apu_to_cpu_locked`, `rtl_apu_port_observers_read`,
  `rtl_apu_frame_timeline_active`, `rtl_apu_extended_frame_timing`) — T013/T014, not
  stubbed.
  - **CORRECTION to the original entry:** it called the non-APU group "small shim work".
    That was wrong. `WriteReg` is on the interpreter's mandatory path (`cpu_state.c:636`
    `cpu_write16`) and its `$2140-$217F` branch calls `RtlApuWrite`; `ReadRegWord`'s
    APU-port path calls `rtl_sync_apu_to_cpu_locked` + `rtl_apu_port_observers_read`.
    **Group A alone cannot produce a booting DS port** — the two groups are coupled at
    three call sites, not independent. Super Metroid drives `$2140` constantly (SPC700
    IPL upload, echo), so the interpreter cannot boot until T013 lands.
  - [DISCOVERED] **`RomPtr` diverges from desktop, deliberately.** Desktop `RomPtr`
    (`common_rtl.c:1310`) is ROM-only: it delegates every bank to `cart_getRomPtr`,
    which returns NULL for `$7E`/`$7F` (`cart.c:143`) and the `$00-$3F/$80-$BF`
    `$0000-$1FFF` mirror, then latches `g_fail` and returns `&g_rom[0]`. So a WRAM
    address through desktop `RomPtr` reads ROM byte 0. The DS port resolves WRAM and
    the mirror to `g_ram` first — behavior inherited from the `ds_rom_ptr()` that
    preceded the export, and required by `SimpleHdma_PtrRangeValid`, which validates
    HDMA table pointers against `g_ram` *or* `g_rom`. This is a **faithfulness
    deviation** and needs a decision at review: keep it (safer, current) or match
    upstream (faithful, risks regressing the HDMA path). Do not "fix" it silently.
- [DISCOVERED] **`extern/snesrecomp` has an uncommitted upstream edit.** Working tree
  is dirty: `runner/src/snes/ppu.c` gains `PPU_T0_DECL` in `PpuDrawBackgrounds`
  without ever using `_ppu_t` (no `PPU_T0`, no `PPU_ACC`) — a half-finished profiling
  edit. **No effect on the DS build**: the macro is guarded by
  `#ifdef SNESRECOMP_INTERP_PROFILE`, which the Makefile deliberately omits, so
  `PPU_T0_DECL` expands to nothing. Left uncommitted deliberately — `extern/` is
  upstream and `CLAUDE.md` forbids silent edits there. Needs a decision: finish the
  instrumentation, or `git checkout` it away. Do not let it get absorbed into a
  submodule-pointer commit.
  Also: the two candidate pins `b04d8fa` and `63c8be8` are **divergent commits with an
  identical tree** (`e935a61`), neither an ancestor of the other. Functionally
  equivalent; pinned to `63c8be8`, the commit actually built against.
- [DISCOVERED — BLOCKER, memory safety] **`src/ppu_ds.c:209` reads ~121 KB past the
  priority buffer.** `bg_buf->data[src_x + 272 + src_y * 544]`. `Ppu.data` is declared
  `PpuZbufType data[kPpuBufWidth]` (`extern/snesrecomp/runner/src/snes/ppu.h:78`) — a
  **single row** of `kPpuBufWidth = 256 + 272*2 = 800` entries, not a 2-D grid. With
  `src_y` up to 223 the index reaches `255 + 272 + 223*544 = 121839` against an
  800-element (1600-byte) array. Two compounding defects:
  1. **Stride is wrong.** `544` is `kPpuBufWidth - kPpuXPixels` (the border allowance),
     not the row pitch. The row pitch is `kPpuBufWidth` (800). Looks like a subtraction
     where a full-width was intended.
  2. **The buffer is not 2-D.** The row-major `src_y * stride` addressing has no
     backing storage. Lines 187-188 (`data[x + 272]`) are in range and correct —
     single-row access, matching the real declaration.
  gcc flags this: `src/ppu_ds.c:209:41: warning: iteration 2 invokes undefined
  behavior [-Waggressive-loop-optimizations]`. Beyond wrong pixels, this reads
  ~121 KB of adjacent emulator state (other Ppu/Cpu/Snes fields, .bss) and renders it
  as pixel data. Needs a fix before T011 can be called done — either index a genuine
  2-D buffer or drop to single-row access. **Not fixed here: out of scope for the
  register-dispatch work, and it needs a decision about which contract F001 settles.**
- [DISCOVERED] **F001 priority-buffer frame contract** — `ppuDsExtractLine` reads 8
  scanlines from a buffer the runner rewrites every line. Analysis:
  `aes/design/F001-priority-buffer-frame-contract.md`
- [DISCOVERED — BLOCKER, correctness] **`src/main.c:78-100` bypasses `SmDrawPpuFrame`
  and runs its own raster, so no HDMA ever executes.** The frame loop calls
  `ppu_rasterBegin` + `ppu_runLine` per line directly. But `SmDrawPpuFrame`
  (`src/ds_rtl.c:139`) is the registered `draw_ppu_frame` hook (see
  `ds_cpu_infra.c`) *and* the only place that walks the HDMA tables per line
  (`SimpleHdma_Init`/`SimpleHdma_DoLine`) and the only place that calls
  `snes_set_hdma_beam_enabled(g_snes, false)`. main.c never calls it, so:
  - **Zero HDMA transfers run.** Super Metroid drives the HUD/status split, the
    Ceres elevator gradient, and the color-math beam effects entirely through HDMA —
    every one of them would be silently dead.
  - The beam-timeline HDMA state is never reconciled with the host walk, so the two
    are left in an undefined relationship rather than the documented
    "host walks the tables, beam stays off" contract.
  Note `SmDrawPpuFrame` and main.c's loop *both* call `ppu_runLine` for lines 0..224,
  so this is not merely a bypass — wiring the hook in without removing the loop would
  render every line twice. The fix is one or the other, not both.
- [DISCOVERED — BLOCKS `make check`] **`make lint` fails on 8 pre-existing findings,
  all `nullPointerRedundantCheck`/`ctunullpointer` in `main.c` (7) and
  `src/ppu_ds_effects.c:206` (1).** Cause is cppcheck not modelling
  `while (1) swiWaitForVBlank();` (`main.c:69`) as non-terminating, so it treats the
  `if (!snes)` guard at `main.c:68` as removable and every later `snes->` deref as
  reachable with `snes == NULL`. Almost certainly a false positive. Verified
  pre-existing: 8 findings with `src/ds_rtl_regs.c` removed, 8 with it present — the
  register-dispatch work adds none. Per-file cppcheck is clean on every file
  individually; only whole-program analysis trips it. Unfixed deliberately: the fix
  is either a targeted suppression or a restructure of the boot guard, and picking
  which is a quality-gate decision, not a drive-by. `make check` (= `setup all lint`)
  therefore cannot go green until this and the link are both resolved.
- [DISCOVERED] Quality gates were no-ops (`make lint`/`test` exited 0 without running
  anything). Now fixed and verified; see Makefile `TOOLCHAIN_ENV`. `make test` still
  fails by design — there is no ROM assertion harness.
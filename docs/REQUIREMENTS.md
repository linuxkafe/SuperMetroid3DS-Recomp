# Requirements

Statuses: **[DONE]** verified · **[PARTIAL]** works, incomplete · **[TODO]** not started

## Functional

- **[PARTIAL]** Build a `.3dsx` that boots on 3DS hardware and in an emulator
  (`make docker-build` → 121 KB artifact; `make test` → 120 frames)
- **[PARTIAL]** Present the SNES composite to the top screen as RGB565,
  integer-scaled and letterboxed — implemented, but fed by a test pattern
- **[PARTIAL]** Map 3DS controls to the SNES controller word — implemented,
  unverified against the core's joypad model
- **[TODO]** Link and run the recompiled core (blocked on T027, no `ucontext.h`)
- **[TODO]** Render real Super Metroid frames
- **[TODO]** Play audio from the core's APU to the 3DS DAC
- **[TODO]** Load the ROM from the SD card and verify its digest
- **[TODO]** Save/load SRAM
- **[TODO]** Regenerate `src/gen/` reproducibly via `make regen`
- **[TODO]** Expose a debug bridge on TCP 5003 / GDB on 5004 (T028)

## Non-functional

- **Memory** — the core's compiled size must fit the 3DS app region. Currently
  *extrapolated* at ~28 MB from one measured bank; unverified (T025)
- **Performance** — target 60 FPS on hardware. **No measurement exists.** Any
  number quoted before T026 is an extrapolation and must be labelled as one
- **Correctness** — a frame must match the upstream PC build for the same ROM and
  input. Nothing is asserted about this yet (T030)
- **Legal** — no ROM, and nothing derived from one, in the repository. This
  includes `src/gen/` and `src/supermetroid_rom.h`
- **Reproducibility** — a clean checkout plus `make setup` reproduces the build
  without the upstream tree being modified

## Explicitly not required

- Frame rate beyond what the platform achieves
- Widescreen, mods, or any gameplay change
- Cartridge (`.cia`) builds — `.3dsx` only

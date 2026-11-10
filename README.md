# SuperMetroid3DS

A Nintendo 3DS port of **Super Metroid** built via **static recompilation** using [SuperMetroidRecomp](https://github.com/mstan/SuperMetroidRecomp) by mstan.

> **No ROM is included and none will ever be.** See [Legal](#legal).

---

## Status: Works in Emulator, Crashes on Real 3DS Hardware

⚠️ **WARNING: The game does NOT work on real 3DS hardware** — crashes at entry point (PC=0x07100000)

| Build | Azahar Emulator | Real 3DS |
|-------|-----------------|----------|
| Test pattern (`SM_WITH_CORE=0`) | ✅ Stable (500+ frames, up to 450 fps) | ❓ Unknown |
| Core embedded (`SM_WITH_CORE=1`) | 📊 Works (500 frames, 44-217 sys_fps) | ❌ **Crashes at entry point** |

**Luma Crash Dump (real 3DS hardware):**
- PC = `0x07100000` (entry point, before `main()`)
- Cause: crash during static initialization or early C runtime
- Root cause: T027 unresolved — missing fiber shim for ARM11

---

## Quick Start

### What you need
- A Nintendo 3DS with custom firmware (CFW)
- Homebrew Launcher (or any `.3dsx` launcher)
- A legally obtained **Super Metroid (USA or Japan)** ROM file (`.sfc`)

### Installation
1. Copy the entire `SuperMetroid3DS` folder to your 3DS SD card at:
   ```
   /3ds/SuperMetroid3DS/
   ```
2. Place your ROM file in the same folder with **one of these exact names**:
   - `sm.sfc`
   - `SuperMetroid-usa.sfc`

   The application automatically detects and verifies the ROM on startup.

3. Launch **SuperMetroid3DS** from Homebrew Launcher.

### Controls
| 3DS Button | SNES Action |
|------------|-------------|
| A          | Jump        |
| B          | Shoot / Run |
| X          | Item Select |
| Y          | Item Cancel |
| L / R      | Aim Up / Down |
| Start      | Start       |
| Select     | Select      |
| D-Pad      | Movement    |

---

## Route B: snesrev/sm Decompilation

This port uses **Route B** — the [snesrev/sm](https://github.com/snesrev/sm) functionally complete decompilation — instead of the original recompilation pipeline.

### Validation Results
| Metric | Value |
|--------|-------|
| Code banks | 32 |
| Functions | 5,671 |
| Instructions | 132,318 |
| Binary size (x86_64) | 1.8 MB |
| Binary size (3DSX, core embedded) | 33.9 MB |
| Frame accuracy (20,000 frames) | **0 mismatches** |
| PatchBytes workarounds | 90 |

### Screenshots
- [screenshot.png](screenshot.png) — Title screen (frame 60, ~1.8% non-zero)
- [screenshot_gameplay.png](screenshot_gameplay.png) — Gameplay (frame 1800, ~42% non-zero)

Full feasibility study: [`docs/ROUTE_B_STUDY.md`](docs/ROUTE_B_STUDY.md)

---

## Building from Source

### Prerequisites
- Docker / Podman
- A legally obtained Super Metroid ROM (`.sfc`)
- UPSTREAM_ROOT pointing to a clone of [SuperMetroidRecomp](https://github.com/mstan/SuperMetroidRecomp)
- GEN_DIR pointing to the regenerated core sources

### Build Options

| Option | Output | Size | Status |
|--------|--------|------|--------|
| `SM_WITH_CORE=0` (default) | `build3ds/supermetroid3ds.3dsx` | ~180 KB | ✅ Stable in emulator |
| `SM_WITH_CORE=1` | `build3ds/supermetroid3ds.3dsx` | ~33.9 MB | ⚠️ Crashes on real 3DS |

### Build Commands
```bash
# Clone this repo
git clone git@github.com:linuxkafe/SuperMetroid3DS-Recomp.git
cd SuperMetroid3DS-Recomp

# Set environment
export UPSTREAM_ROOT=/path/to/SuperMetroidRecomp
export GEN_DIR=/path/to/SuperMetroidRecomp-work/src/gen

# Build 3DS homebrew
make docker-build
```

### Performance (Azahar headless, 500+ frames tested):
| Build | sys_fps | emu_speed |
|-------|---------|-----------|
| SM_WITH_CORE=0 | 0 / 450 fps | 0.02x - 8.7x |
| SM_WITH_CORE=1 | 44 / 217 fps | 0.2x - 4.0x |

The core has limited performance due to lack of 3DS-specific optimizations.

### Headless Validation (CI)
```bash
make docker-headless-test
```
Runs the headless comparator harness — validates 0 mismatches over 20,000 frames.

---

## Project Structure
```
SuperMetroid3DS-Recomp/
├── platform/3ds/          # 3DS platform layer (SDL -> 3DS shim)
│   ├── host.c             # ROM loading, SHA-256 verification, SnesInit
│   ├── host.h
│   ├── fiber_compat.c     # ARM11 fiber implementation (T027)
│   └── fiber_compat.h
├── frontend/3ds/          # 3DS frontend (input, audio, render, main)
│   ├── 3ds_main.c
│   ├── 3ds_input.c
│   ├── 3ds_audio.c
│   └── 3ds_render.c
├── docs/                  # Documentation
│   └── ROUTE_B_STUDY.md   # Full feasibility study with measured data
├── scripts/               # CI / validation scripts
├── CMakeLists.txt
├── Makefile
└── README.md              # This file
```

---

## Legal

This is an **unofficial port**. Super Metroid is copyrighted by Nintendo.

**This repository contains:**
- No ROM, no ROM image, no copy of Nintendo code
- No derived work that embeds ROM data
- Only original platform shim code and build automation

**The user must:**
- Provide their own legally obtained Super Metroid `.sfc` file
- Verify the SHA-256 matches the expected digest (the app does this automatically)
- Comply with their local laws regarding game ownership and backups

Upstream: [SuperMetroidRecomp](https://github.com/mstan/SuperMetroidRecomp) — the expected digest is in upstream's `rom_identity.txt`; `make doctor` prints both for verification.

---

## Credits

- **mstan** — SuperMetroidRecomp, snesrecomp, snesrev/sm decompilation
- **snesrev team** — Functionally complete Super Metroid decompilation
- **devkitPro** — devkitARM toolchain and libctru
- **Super Metroid** — © 1994 Nintendo

---

## Troubleshooting

### Game crashes on real 3DS
- This is a known issue (T027)
- The fiber shim for ARM11 context switching is not yet implemented
- The game works correctly in the Azahar emulator

### ROM not found
- Ensure the ROM file is named `SuperMetroid-usa.sfc` or `sm.sfc`
- The ROM must be in the same directory as the `.3dsx` file
- Verify the ROM SHA-256 matches the expected value

### Build fails with "SM_WITH_CORE=ON requires -DGEN_DIR"
- Run `make regen` first to generate the core from the ROM
- Or use the pre-built core from the UPSTREAM_ROOT worktree
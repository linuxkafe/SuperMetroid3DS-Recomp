# SuperMetroid3DS

A Nintendo 3DS port of **Super Metroid** built via **static recompilation** using [SuperMetroidRecomp](https://github.com/mstan/SuperMetroidRecomp) by mstan.

> **No ROM is included and none will ever be.** See [Legal](#legal).

---

## Status: RUNNING IN EMULATOR — Crashes on Real Hardware

| Build | Emulator (Azahar) | Real 3DS |
|-------|-------------------|----------|
| Test pattern (`SM_WITH_CORE=0`) | ✅ Works (120+ frames) | ❓ Unknown |
| Core embedded (`SM_WITH_CORE=1`) | ⚠️ Runs ~60 frames then hangs | ❌ Crashes at entry point |

**Luma Crash Dump (3DS real hardware):**
- PC = `0x07100000` (entry point)
- Cause: crash before `main()` during static initialization
- Root cause: missing fiber shim for 3DS (T027 unresolved)

---

## Quick Start

### What you need
- A Nintendo 3DS with custom firmware (CFW)
- Homebrew Launcher (or any `.3dsx` launcher)
- A legally obtained **Super Metroid (USA or Japan)** ROM file (`.sfc`)

### Installation
1. Download the latest release from the [FTP server](ftp://10.0.0.130:5000/3ds/SuperMetroid3DS/) or build from source (see below)
2. Copy the entire `SuperMetroid3DS` folder to your 3DS SD card at:
   ```
   /3ds/SuperMetroid3DS/
   ```
3. Place your ROM file in the same folder with **one of these exact names**:
   - `sm.sfc`
   - `SuperMetroid-usa.sfc`
   
   The application will automatically detect and verify the ROM on startup.

4. Launch **SuperMetroid3DS** from Homebrew Launcher.

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

Full feasibility study: [`docs/ROUTE_B_STUDY.md`](docs/ROUTE_B_STUDY.md)

---

## Building from Source

### Prerequisites
- Docker / Podman
- A legally obtained Super Metroid ROM (`.sfc`)
- UPSTREAM_ROOT pointing to a clone of [SuperMetroidRecomp](https://github.com/mstan/SuperMetroidRecomp)

### Build (test pattern - works in emulator)
```bash
export UPSTREAM_ROOT=/path/to/SuperMetroidRecomp
make docker-build SM_WITH_CORE=0
```
Output: `build3ds/supermetroid3ds.3dsx` (~180 KB, test pattern only)

### Build (core embedded - crashes on real 3DS)
```bash
export UPSTREAM_ROOT=/path/to/SuperMetroidRecomp
make docker-build SM_WITH_CORE=1
```
Output: `build3ds/supermetroid3ds.3dsx` (~33.9 MB, core embedded)

### Headless validation (CI)
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
│   └── host.h
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

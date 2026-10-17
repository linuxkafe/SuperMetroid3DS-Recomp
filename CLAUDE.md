# CLAUDE.md — SuperMetroid3DS

## Intent

A Nintendo 3DS port of the Super Metroid static recompilation
([SuperMetroidRecomp](https://github.com/mstan/SuperMetroidRecomp)), adapting
upstream's read-only C core to run on 3DS hardware (ARM11, VFPv4, dual screens)
using devkitPro/libctru.

Evidence for every feasibility claim lives in `docs/3DS-PORT-STUDY.md`.

---

## Non-Goals

- Port to any platform other than 3DS. The DS attempt is preserved on the
  `ds-legacy` branch and is not to be revived without a measured reason.
- Modify the game logic or the ROM. This is a port of an existing recompilation.
- New gameplay features, widescreen, or mods. Faithful port only.
- Any ROM other than Super Metroid (Japan, USA) 1.0.
- An emulator, interpreter, JIT or dynamic recompiler.

---

## Critical Files

Changes here need to be flagged to the user before making them.

- `CMakeLists.txt` — the 3DS build; the source inclusion and exclusion lists are
  decisions on the record, each with a stated reason.
- `platform/3ds/sdl_compat.h` — replaces the framework's SDL include boundary.
  Anything added here is a claim that the 3DS can do what SDL did.
- `frontend/3ds/3ds_main.c` — the `SM_WITH_CORE` gate. Flipping it to `ON` without
  T027 resolved produces a binary that builds and misbehaves.
- `scripts/gate-headless.sh` — the behavioural gate. Weakening it turns every
  future green check into theatre.
- `$(UPSTREAM)/recomp/*.cfg` and the generated `src/gen/` — upstream's ground
  truth. Never committed, never hand-edited.

---

## Never Do

- **Never edit the upstream checkout.** It is read-only and lives outside this
  repository. A DS-era edit (`PPU_T0_DECL` injected into `snesrecomp/runner/src/snes/ppu.c`)
  was invisible in this repo's diffs and cannot compile anywhere else. Use
  `frontend/` or `platform/` first; `patches/NNNN-*.patch` only if that is
  genuinely impossible.
- **Never commit a ROM, or anything derived from one.** That includes
  `src/gen/` and `src/supermetroid_rom.h` — a 19.6 MB header embedding the whole
  ROM as a byte array. The extension does not save it. A ROM was sitting
  untracked-but-ungitignored in the DS tree; that hole is now closed.
- **Never claim a measurement that was not measured.** "3DS is 4–8× faster than
  DS" was a clock-speed extrapolation and is not in any doc. `with_core=0` in a
  green gate means the game did not run.
- **Never turn off `SM_WITH_CORE` defaults or weaken the gate to get a green build.**
- **Never use TCP 5002.** SimCity-3DS-Port owns it.
- **Never allocate dynamically in a hot path** without a pool/arena strategy.
- **Never modify CI to skip a quality gate.**

---

## Port Allocation

SimCity-3DS-Port and this project are both run on the same Steam Deck. Two builds
whose debug bridges share a port cross-connect, and the resulting measurements
look real and are not.

| Port | Owner | Purpose |
|---|---|---|
| 5002 | SimCity-3DS-Port | command protocol — **do not use** |
| **5003** | SuperMetroid3DS | command protocol |
| **5004** | SuperMetroid3DS | GDB RSP |

---

## Evidence Required

Every non-trivial change must include:

- [ ] Output of `make test` (the headless gate), or an explicit statement that it
      was not run and why
- [ ] Output of `make lint`
- [ ] A diffstory: what changed, why, what was deliberately untouched, what remains risky
- [ ] Updated docs if behaviour changed

A build that compiles is not evidence that anything works. So far the only
behavioural evidence that exists is "the frontend renders N frames and exits".

---

## Working with the core

- `src/gen/` and `recomp/funcs.h` are **generated**, from the ROM, by upstream's
  `v2_regen.py`. Changing `recomp/*.cfg` does not reach the compiled tier until
  `make regen` is run again. See the `deck-launch` skill's warning: compare
  generated file hashes before and after, not substring counts.
- The ROM digest must match upstream's `rom_identity.txt`. `make doctor` compares.
- Reads run on the Steam Deck (`ssh deck@steamdeck`) go in the **foreground**.
  Backgrounded remote runs have died silently three times on this workspace. Use
  `/dev/shm` on the Deck, never `/tmp`. Never touch `/home/deck/*` with a file
  tool — put `head`/`tail` inside the ssh command.

---

## Review Rule

- AI may implement and suggest.
- Human approves all merges.
- No auto-merge without passing all quality gates.
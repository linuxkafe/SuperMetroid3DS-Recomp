# Personas

## User: 3DS Homebrew Player

**Profile:** Owns a 3DS or 2DS with a flashcart (Luma3DS, AtDstry) or runs
Azahar. Wants Super Metroid on the hardware they have.

**Goals**
- Play Super Metroid on a 3DS, faithfully
- A frame rate that makes the game playable
- Save/load that does not corrupt
- Install by copying one file

**Pain points**
- SNES emulators on 3DS are not fast enough for Super Metroid
- Flashcart installs are fiddly and easy to get wrong
- Ports that claim 60 FPS and deliver 15

**What they need from this project right now:** nothing. The port does not run
the game yet. The honest state is in the README's first paragraph, and that is
the correct thing to show them.

## Maintainer

**Profile:** Works in this workspace alongside `SimCity-3DS-Port`. Comfortable
with devkitPro, cross-compilation, and AES.

**Needs**
- Upstream to stay untouched, so that updating it is a pin change
- Every performance number to say what it measured and what it did not
- No green gate that means less than it says

**Failure modes this project has already hit, and now guards against**

| Failure | Guard |
|---|---|
| Editing the read-only upstream, invisible in this repo's diff | Read-only external checkout; `make upstream-check` |
| A ROM-derived file reaching git | `.gitignore` covers `src/gen/` and `src/supermetroid_rom.h` |
| A green gate that only proved a build | `make test` asserts a frame count |
| A measurement nobody could reproduce | `docs/3DS-PORT-STUDY.md` separates measured from extrapolated |
| Two ports fighting over a TCP port | 5003/5004, recorded in `CLAUDE.md` |

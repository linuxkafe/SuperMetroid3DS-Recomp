# Hostile Insights Registry

Lessons from hostile analysis where an assumption turned out to be false. Each
entry names the thing that was believed, the evidence that broke it, and what
changed as a result.

## [Verification] — "the 3DS has no BSD sockets"

- **Task:** 3DS port feasibility (T024)
- **Was believed:** 3DS has no `<sys/socket.h>`, so upstream's TCP debug server
  is not portable and the sibling port's `3dsdbgkit` cannot be reused.
- **Broken by:** a probe run as `arm-none-eabi-gcc … -c` **without**
  `-I$DEVKITPRO/libctru/include`. libctru ships `<sys/socket.h>` backed by the
  3DS `soc` service; `socket`/`bind`/`listen` compile against it.
- **Impact:** had it stood, T028 would have been written as "impossible" and the
  port would have shipped with no debugging path at all.
- **Applied to:** the retraction is recorded in `docs/3DS-PORT-STUDY.md` and in
  `CMakeLists.txt`'s exclusion list, so the wrong reason is not copied forward.
- **Date:** 2026-10-17

## [Verification] — "-Werror caught a format bug only on ARM"

- **Task:** adding the self-terminating frame counter
- **Was believed:** `printf("%u", tick)` with `uint32_t` is fine.
- **Broken by:** the build. On ARMv6K `uint32_t` is `long unsigned int`, so `-Werror`
  rejected it. The same code is harmless on x86-64, where `uint32_t` *is*
  `unsigned int`.
- **Impact:** would have been a silent stack-garbage bug on hardware, invisible on
  the host.
- **Applied to:** `<inttypes.h>` + `PRIu32`. The frontend builds with
  `-Wall -Wextra -Werror` so this class of error cannot return.
- **Date:** 2026-10-17

## [Process] — a gate that only proved a build

- **Task:** replacing the DS-era `make test`
- **Was believed:** the DS `make test`, which depended on the `.nds` target and
  therefore "passed" whenever the build passed, was coverage.
- **Broken by:** reading it. It never started a program. And the first 3DS gate
  ran the port under a timeout and called surviving the window a pass — which
  cannot distinguish "ran correctly for every frame" from "hung on frame 3 and
  the timeout saved it".
- **Impact:** the DS project reported green for a port that had never built green
  at all; its own kanban says "Nothing has ever built green".
- **Applied to:** the gate now asserts `frames == N`, read from a result file the
  port writes into the emulator's SD root, and prints what it does **not** cover.
- **Date:** 2026-10-17

## [Evidence] — Azahar's SD root is the real SD card on the Deck, not `~/.local/share`

- **Task:** running the headless gate on the Deck
- **Was believed:** Azahar's SD root is `~/.local/share/azahar-emu/sdmc/` on every host.
- **Broken by:** `qt-config.ini`. Locally that is true. On the Deck it is
  `/run/media/mmcblk0p1/Emulation/storage/azahar/sdmc/` — the real SD card.
  The local path was a **writable temp directory**, so the gate wrote the frame
  budget there and the port ran with no budget at all, while the gate still
  passed. That is a gate that passes while doing nothing.
- **Impact:** would have shipped a green check on the Deck that never started a
  port. The result file was written to a path the emulator never looked at.
- **Applied to:** `scripts/gate-headless.sh` selects the SD root by host, and
  `SDMC` is a Deck path that is written via `ssh`, not locally.
- **Date:** 2026-10-17

## [Evidence] — a double-quoted ssh string eats backslash line-continuations

- **Task:** the Deck branch of the gate
- **Was believed:** `\` at the end of a line inside a double-quoted string
  continues the line.
- **Broken by:** the gate failing with no result file, while the same command
  typed by hand worked. The backslashes reached the Deck as literal characters,
  breaking the guest command. Only visible when the whole thing was reconstructed
  as one line.
- **Impact:** a gate that fails on a host where it should pass — and which would
  have been "fixed" by weakening the assertion rather than the quoting.
- **Applied to:** the Deck command is one line. The gate records the trap.
- **Date:** 2026-10-17

## [Process] — the Deck runs Azahar 2127.0-alpha1, not 2126.2

- **Task:** getting the Deck build to start
- **Was believed:** the Deck and local Azahar were the same build.
- **Broken by:** `-v`. The Deck AppImage is 2127.0-alpha1 and its default Qt
  platform is `wayland`, which Azahar cannot load under `ssh` — only `xcb`
  works, and only with the session's X authority file
  (`/run/user/1000/xauth_nOhvDY`) passed explicitly. `DISPLAY=:0` alone was not
  enough.
- **Impact:** every Deck run failed at "no Qt platform plugin" with no obvious
  cause, and the failure looked like a port problem rather than an environment
  one.
- **Applied to:** `HOST=deck` sets `XAUTHORITY` and `QT_QPA_PLATFORM=xcb`. The
  local build stays 2126.2 under `xvfb-run`.
- **Date:** 2026-10-17

## [Evidence] — the Deck's SD root is root-owned at `/run/media`

- **Task:** writing the frame budget from the gate
- **Was believed:** `mkdir -p "$SDMC"` would work on every host.
- **Broken by:** `mkdir: cannot create directory '/run/media': Permission denied`.
  `/run/media` is root-owned, and the SD root already exists.
- **Impact:** would have made the Deck gate unusable on the host where it is the
  only gate that matters.
- **Applied to:** directory creation is local-only; the frame budget is written
  on the Deck via `ssh`.
- **Date:** 2026-10-17

- **Task:** making the headless gate deterministic
- **Was believed:** `SM_MAX_FRAMES=120` on the host command line would reach the
  guest, as it does for a native process.
- **Broken by:** measurement. The gate ran to its timeout with the variable
  ignored. No env-passing mechanism exists in Azahar's flags or config.
- **Impact:** two wasted gate designs. It is also why the gate writes to the SD
  root rather than stdout — a headless emulator discards the guest console, so
  stdout is not a channel at all.
- **Applied to:** `read_sd_int()` plus `write_gate_result()`; the file path is
  also what a real user would use on hardware.
- **Date:** 2026-10-17

## [Scope] — a 19.6 MB ROM sitting in the source tree, untracked but not ignored

- **Task:** the retarget
- **Was believed:** `.gitignore` covered the ROM.
- **Broken by:** `git status`. `src/supermetroid_rom.h` was a 19.6 MB generated
  header embedding the entire copyrighted Super Metroid USA ROM as a byte array —
  untracked, and **not** covered by any ignore rule. A `.gitignore` rule does not
  apply to an already-tracked path, and this one was one `git add -A` from being
  committed.
- **Impact:** a rights problem and a permanent history problem.
- **Applied to:** `.gitignore` now covers `supermetroid_rom.h` in any directory,
  `*.sfc`, and the whole of `src/gen/`. Recorded in `CLAUDE.md` under Never Do.
- **Date:** 2026-10-17

# Quality Gates

What is enforced, what is merely checked, and — more usefully — **what each gate
does not prove**. A gate that cannot say what it misses is not a gate.

## Enforced

| Gate | Command | Asserts | Failure |
|---|---|---|---|
| Upstream pin | `make upstream-check` | upstream VERSION is 0.3.11 | BLOCKER |
| Frontend build | `make docker-build` | the four `frontend/3ds` units compile and link to a `.3dsx`, warnings-as-errors | BLOCKER |
| Lint | `make lint` | cppcheck clean over `frontend/` and `platform/` only | BLOCKER |
| Headless gate | `make test` | the port boots, renders exactly N frames, terminates at frame N | BLOCKER |
| CI | push / PR | the two above, on GitHub runners | BLOCKER |

`make test` reads `sm_gate_result.txt` back out of the emulator's SD root and
compares `frames` against the budget. That is a falsifiable assertion, not a
liveness check.

## Not enforced

| Gap | Why | Ticket |
|---|---|---|
| The core is never built | `src/gen` is ~88 MB of C derived from the ROM; the ROM is not in the repository and regeneration outlasts a CI runner | T025, T027 |
| No frame-correctness check | needs the core linked first | T030 |
| No frame-rate measurement | needs the core linked | T026 |
| Audio is never exercised | not wired to the core | T029 |
| ROM digest never checked in CI | needs the ROM | T031 |

## What `make test` does NOT prove

Read this before treating a green gate as progress on the game:

- **It does not prove the core is linked.** The gate prints `with_core=0` and says
  so. A pass with `with_core=0` means the frontend works and Super Metroid does
  not run.
- **It does not prove anything renders correctly.** It counts frames. It does not
  look at a pixel.
- **It does not run on hardware.** Everything is under an emulator.
- **It cannot detect a hung emulator as a port failure.** It polls for the result
  file and reports a timeout — which is ambiguous between a slow guest and a dead
  one, and is reported as such.

## Deliberately excluded from lint

`make lint` does not walk the upstream checkout. Those sources are read-only,
vendored verbatim, and upstream's to lint. The DS-era `make lint` reported eight
`nullPointerRedundantCheck` findings on a `while (1)` loop it could not model as
non-terminating — noise that trains people to ignore the gate.

## Rules for changing a gate

1. A gate may only get stricter without a documented reason.
2. Weakening one requires a ticket and a stated reason.
3. A new gate must print what it does **not** cover. The existing ones do.

# Roadmap

Priority reflects what blocks what, not what is fun. **T027 blocks the entire
port** — until the core can link, nothing after it produces a playable game.

## Critical path

| ID | Ticket | Impact | Effort | Status |
|---|---|---|---|---|
| **T027** | **ARM11 fiber backend (no `ucontext.h`)** | **blocks everything** | **High** | **todo** |
| T025 | Compile all 116 generated units; measure real size | High | Medium | todo |
| T026 | Measure frame time on hardware | High | Medium | todo |
| T030 | Frame-for-frame comparison vs upstream PC build | High | Medium | todo |

## Sprint 3 — get the core linked

| ID | Ticket | Impact | Effort | Status |
|---|---|---|---|---|
| T027 | Hand-written ARM11 context switch for the fiber API | High | High | todo |
| T025 | Full ARM11 compile of `src/gen`, replace the 28 MB estimate | High | Medium | todo |
| T032 | Flip `SM_WITH_CORE=ON` and resolve the link errors it exposes | High | Medium | todo |

## Sprint 4 — make it correct

| ID | Ticket | Impact | Effort | Status |
|---|---|---|---|---|
| T030 | Deterministic frame hash; compare against upstream PC output | High | Medium | todo |
| T033 | ROM load from SD + digest verification against `rom_identity.txt` | High | Medium | todo |
| T029 | Audio: resample 32,040 Hz core output to 48,000 Hz DAC | Medium | Medium | todo |
| T034 | SRAM save/load | Medium | Medium | todo |

## Sprint 5 — make it measurable and operable

| ID | Ticket | Impact | Effort | Status |
|---|---|---|---|---|
| T028 | Debug bridge on TCP 5003 + GDB RSP on 5004 | Medium | Medium | todo |
| T026 | Per-frame time measurement, interleaved A/B pairs | High | Medium | todo |
| T035 | Hardware validation on real 3DS hardware | Medium | Low | todo |
| T036 | Frame capture to host for visual diffing | Medium | Medium | todo |

## Done

| ID | Ticket | Evidence |
|---|---|---|
| T023 | Upstream pinned at v0.3.11, read-only, external | `make upstream-check` |
| T024 | 3DS port study: 71/80 units compile for ARM11 | `docs/3DS-PORT-STUDY.md` |
| T031 | Headless behavioural gate with a falsifiable assertion | `make test` passes, 120 frames |
| — | DS work preserved on `ds-legacy`; removed from master | branch `ds-legacy` |
| — | ROM-derived files gitignored (`src/gen/`, `supermetroid_rom.h`) | `.gitignore` |

## Closed: the DS backlog

These were the DS plan. They did not fail on merit — they ran out of memory
budget. Carried here so the reasoning is not lost:

| Old ID | Was | Disposition |
|---|---|---|
| T012 | DMA-driven VRAM updates | subsumed by T030 |
| T013 | Port `sm_spc_player.c` → ARM7 | **dropped** — 3DS has no second CPU to hand audio to |
| T014 | ARM7 audio mixer (maxmod) | **dropped** — same |
| T015 | Audio sync with VBlank | subsumed by T029 |
| T016 | Input mapping | **done** in `frontend/3ds/3ds_input.c` |
| T017 | SRAM save/load | → T034 |
| T018 | Boot ROM selection menu | dropped — 3DS homebrew loads one ROM |
| T019 | Optimization pass | → T026, after there is something to profile |
| T020 | Regression testing | → T030, T036 |

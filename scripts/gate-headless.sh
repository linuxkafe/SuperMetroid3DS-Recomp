#!/usr/bin/env bash
#
# Headless behavioural gate.
#
# Asserts something specific and falsifiable: the port boots under an emulator,
# renders N frames, and terminates on its own at exactly frame N.
#
# WHY THE ASSERTION IS "frames == N" AND NOT "did not crash"
#   The first version of this gate ran the port under a timeout and treated
#   "survived the window" as success. That cannot distinguish "ran correctly for
#   every frame" from "hung on frame 3 and the timeout saved it" — and the DS
#   era's `make test` had exactly this shape, which is why nothing was ever
#   actually verified.
#
# WHY THE CHANNEL IS A FILE AND NOT AN ENVIRONMENT VARIABLE
#   The emulator does not propagate the host environment into the guest.
#   SM_MAX_FRAMES=120 on the host had no effect in the guest at all; this was
#   measured, not assumed. The port reads sm_max_frames from the SD root and
#   writes sm_gate_result.txt back, and the guest console is invisible to a
#   headless emulator, so the filesystem is the only channel that works.
#
# HONEST LIMIT: this proves the frontend boots, renders and terminates on cue.
# It says NOTHING about the game: the core is not linked (with_core=0), so no
# frame here comes from Super Metroid. See docs/3DS-PORT-STUDY.md.

set -uo pipefail

AZAHAR="${AZAHAR:-/home/seyon/dev/tools/azahar/usr/bin/azahar}"
SDMC="${SDMC:-$HOME/.local/share/azahar-emu/sdmc}"
ROM_3DSX="${1:?usage: gate-headless.sh <path-to.3dsx> [frames]}"
FRAMES="${2:-120}"
TIMEOUT="${TIMEOUT:-180}"

fail() { echo "GATE FAIL: $*" >&2; exit 1; }

[ -x "$AZAHAR" ] || fail "Azahar not found at $AZAHAR"
[ -f "$ROM_3DSX" ] || fail "no .3dsx at $ROM_3DSX"
command -v xvfb-run >/dev/null 2>&1 || fail "xvfb-run not found (Azahar needs a display server)"

mkdir -p "$SDMC"
rm -f "$SDMC/sm_gate_result.txt"
echo "$FRAMES" > "$SDMC/sm_max_frames"

echo "==> gate: $FRAMES frames, timeout ${TIMEOUT}s"

# The emulator may not exit when the guest does; that is an emulator teardown
# quirk, not a port failure. The result file is the assertion, so the timeout is
# expected here and its exit code is deliberately ignored.
xvfb-run -a "$AZAHAR" "$ROM_3DSX" >/dev/null 2>&1 &
EMU_PID=$!

DEADLINE=$((SECONDS + TIMEOUT))
while [ ! -f "$SDMC/sm_gate_result.txt" ]; do
    if ! kill -0 "$EMU_PID" 2>/dev/null; then
        # Emulator exited before writing a result.
        fail "emulator exited before writing sm_gate_result.txt"
    fi
    if [ "$SECONDS" -ge "$DEADLINE" ]; then
        kill "$EMU_PID" 2>/dev/null
        fail "timed out after ${TIMEOUT}s with no result file"
    fi
    sleep 1
done

kill "$EMU_PID" 2>/dev/null
wait "$EMU_PID" 2>/dev/null

GOT_FRAMES=$(sed -n 's/^frames=//p' "$SDMC/sm_gate_result.txt")
GOT_MAX=$(sed -n 's/^max_frames=//p' "$SDMC/sm_gate_result.txt")
WITH_CORE=$(sed -n 's/^with_core=//p' "$SDMC/sm_gate_result.txt")

echo "==> result: frames=$GOT_FRAMES max_frames=$GOT_MAX with_core=$WITH_CORE"

[ "$GOT_FRAMES" = "$FRAMES" ] || fail "expected $FRAMES frames, port reported $GOT_FRAMES"
[ "$GOT_MAX" = "$FRAMES" ]    || fail "port read the wrong frame budget ($GOT_MAX)"

if [ "$WITH_CORE" != "0" ]; then
    echo "GATE NOTE: core IS linked — the frontend is rendering real game frames now." >&2
    echo "GATE NOTE: this gate still only checks frame COUNT, not frame CONTENT." >&2
else
    echo "GATE NOTE: core NOT linked — this gate proves the frontend only." >&2
    echo "GATE NOTE: it says nothing about Super Metroid rendering correctly." >&2
fi

echo "GATE PASS: $FRAMES frames, clean self-termination"
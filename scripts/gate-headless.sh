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
# Azahar's SD root differs by host. Locally it is a temp directory; on the
# Deck it is the real SD card. Getting this wrong silently produces a gate that
# passes while the port never ran — the result file was written to a path the
# emulator never looked at, and the port was running with no frame budget.
SDMC_LOCAL="$HOME/.local/share/azahar-emu/sdmc"
SDMC_DECK="/run/media/mmcblk0p1/Emulation/storage/azahar/sdmc"
if [ "${HOST:-}" = "deck" ]; then
    SDMC="${SDMC:-$SDMC_DECK}"
else
    SDMC="${SDMC:-$SDMC_LOCAL}"
fi
ROM_3DSX="${1:?usage: gate-headless.sh <path-to.3dsx> [frames]}"
FRAMES="${2:-120}"
TIMEOUT="${TIMEOUT:-180}"
RESULTS="$(mktemp)"

fail() { echo "GATE FAIL: $*" >&2; exit 1; }

[ -x "$AZAHAR" ] || fail "Azahar not found at $AZAHAR"
[ -f "$ROM_3DSX" ] || fail "no .3dsx at $ROM_3DSX"
command -v xvfb-run >/dev/null 2>&1 || fail "xvfb-run not found (Azahar needs a display server)"

# Only create the directory locally. On the Deck the SD root is the real SD
# card at /run/media/... and /run/media is root-owned, so mkdir fails there —
# and the directory already exists anyway.
rm -f "$SDMC/sm_gate_result.txt"
if [ "${HOST:-}" = "deck" ]; then
    # sm_max_frames has to be written on the Deck, because $SDMC is a Deck
    # path and does not exist on the host.
    ssh deck@steamdeck "echo $FRAMES > $SDMC/sm_max_frames"
else
    mkdir -p "$SDMC"
    echo "$FRAMES" > "$SDMC/sm_max_frames"
fi

echo "==> gate: $FRAMES frames, timeout ${TIMEOUT}s, host=${HOST:-local}"

# The emulator may not exit when the guest does; that is an emulator teardown
# quirk, not a port failure. The result file is the assertion, so the timeout is
# expected here and its exit code is deliberately ignored.
if [ "${HOST:-}" = "deck" ]; then
    # The Deck runs Azahar 2127.0-alpha1 on the live SteamOS session. It needs
    # the X authority file explicitly — the same `ssh` invocation that sets
    # DISPLAY does not carry Xauth — and QT_QPA_PLATFORM=xcb, because the
    # session is Xwayland and Azahar's default platform is wayland.
    #
    # Runs in the FOREGROUND per the deck-launch rules: a backgrounded remote
    # run has died silently here before, with no error and no exit status.
    # Single-line ssh command. The backslash line-continuations that look
    # harmless here are literal inside a double-quoted string, so they reached
    # the Deck as stray backslashes and broke the guest command.
    BIN=$(basename "$ROM_3DSX")
    scp -q "$ROM_3DSX" deck@steamdeck:/dev/shm/"$BIN" 2>/dev/null
    timeout "$TIMEOUT" ssh deck@steamdeck "echo $FRAMES > $SDMC/sm_max_frames && XAUTHORITY=/run/user/1000/xauth_nOhvDY DISPLAY=:0 QT_QPA_PLATFORM=xcb timeout $TIMEOUT ~/Applications/azahar.AppImage /dev/shm/$BIN > /dev/shm/azahar.out 2>&1" >/dev/null 2>&1
    # The result file lives on the Deck, not here. Checking it locally would
    # always fail — the path does not exist on this host — and would make the
    # gate useless rather than merely noisy.
    if ! ssh deck@steamdeck "test -f $SDMC/sm_gate_result.txt" 2>/dev/null; then
        fail "Deck run produced no result file"
    fi
    ssh deck@steamdeck "cat $SDMC/sm_gate_result.txt" > "$RESULTS"
else
    xvfb-run -a "$AZAHAR" "$ROM_3DSX" >/dev/null 2>&1 &
    EMU_PID=$!

    DEADLINE=$((SECONDS + TIMEOUT))
    while [ ! -f "$SDMC/sm_gate_result.txt" ]; do
        if ! kill -0 "$EMU_PID" 2>/dev/null; then
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
fi

GOT_FRAMES=$(sed -n 's/^frames=//p' "$RESULTS")
GOT_MAX=$(sed -n 's/^max_frames=//p' "$RESULTS")
WITH_CORE=$(sed -n 's/^with_core=//p' "$RESULTS")

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

rm -f "$RESULTS"
echo "GATE PASS: $FRAMES frames, clean self-termination, host=${HOST:-local}"
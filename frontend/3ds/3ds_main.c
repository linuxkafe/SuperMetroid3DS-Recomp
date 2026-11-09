/*
 * SuperMetroid3DS — entry point and frame loop.
 *
 * SCOPE, STATED PLAINLY: this is a bring-up frontend, not a playable port. It
 * initialises the three 3DS services the port needs (video, HID, audio), runs a
 * frame loop, and either drives the recompiled core or renders a test pattern.
 * Which one it does is a build-time decision, and the reason is not a
 * preference:
 *
 * The core does not link on 3DS yet. snesrecomp's cooperative scheduler is
 * built on a fiber API whose POSIX backend needs <ucontext.h>, and the 3DS
 * newlib environment has no such header. Until that is resolved (ticket T027),
 * linking the core means either a hand-written ARM11 context switch or stubbing
 * the scheduler — and stubbing a scheduler produces a binary that runs and
 * misbehaves, which is worse than one that is honestly not built.
 *
 * So: SM_WITH_CORE=OFF by default, and the test pattern says so on screen. A
 * build that claims to run the game without doing so is the failure mode this
 * comment exists to prevent.
 */
#include <3ds.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "3ds_audio.h"
#include "3ds_input.h"
#include "3ds_render.h"

#ifdef SM_WITH_CORE
/* Provided by platform/3ds/host.c — the host glue between this frontend and
 * the core library. */
#include "host.h"
#endif

/* The SNES composite is 256x224; the test pattern is drawn at that size so the
 * letterbox path exercised here is the same one a real frame takes. */
#define TEST_W 256
#define TEST_H 224

#ifndef SM_WITH_CORE
static uint16_t s_testFrame[TEST_W * TEST_H] __attribute__((aligned(4)));
#endif

/*
 * A moving gradient plus a block whose colour is driven by the pad, so input
 * can be verified visually without a debugger. Deliberately not a "Hello World"
 * rectangle: it should be obvious at a glance whether the frame path, the
 * letterbox scaling and the input mapping are all live.
 */
/*
 * The test pattern is only drawn when the core is not linked. When it is, the
 * frame comes from the game and this function is dead — which -Werror flags,
 * and which is correct: a function that is never called in the shipped build
 * is dead code, not a fallback.
 */
#ifndef SM_WITH_CORE
static void build_test_frame(uint32_t tick, uint16_t pad) {
    for (int y = 0; y < TEST_H; y++) {
        for (int x = 0; x < TEST_W; x++) {
            uint16_t r = (uint16_t)((x * 255) / TEST_W);
            uint16_t g = (uint16_t)((y * 255) / TEST_H);
            uint16_t b = (uint16_t)((x + y + tick) & 0xFF);
            s_testFrame[y * TEST_W + x] = (uint16_t)((r << 11) | (g << 5) | (b >> 3));
        }
    }

    /* Any button at all tints a block, so "is input reaching the core" is a
     * glance rather than an instrument reading. */
    if (pad != SM_PAD_NEUTRAL) {
        uint16_t tint = 0xFFFF;
        for (int y = 96; y < 128; y++) {
            for (int x = 96; x < 160; x++) {
                s_testFrame[y * TEST_W + x] = tint;
            }
        }
    }
}
#endif /* !SM_WITH_CORE */

static void report_status(void) {
    printf("\x1b[2;0H");
    printf("SuperMetroid3DS — bring-up build\n");
    printf("--------------------------------\n");
#ifdef SM_WITH_CORE
    printf("core:       linked\n");
#else
    printf("core:       NOT linked (T027)\n");
#endif
    printf("top fb:     %dx%d\n", SM3DS_TOP_WIDTH, SM3DS_TOP_HEIGHT);
    printf("audio:      %d Hz core -> %d Hz DAC\n", SM3DS_CORE_SAMPLE_RATE, SM3DS_DAC_SAMPLE_RATE);
    printf("\npress any button to tint the centre block\n");
    printf("START to exit\n");
}

/*
 * Read a small integer knob from the SD card root, or return `fallback`.
 *
 * WHY A FILE AND NOT AN ENV VAR: the automated gate needs the port to
 * terminate on its own after a fixed number of frames, so that "ran correctly"
 * and "hung on frame 3" produce different exit codes. The obvious channel is an
 * environment variable, but the emulator does not propagate the host
 * environment into the guest — verified, not assumed — so SM_MAX_FRAMES=120 on
 * the host had no effect in the guest at all.
 *
 * A file next to the .3dsx is the channel that works, and it is also what a
 * person would use to configure a real 3DS. Absent file => `fallback`, so the
 * shipped default is unchanged.
 */
static int read_sd_int(const char *name, int fallback) {
    FILE *f = fopen(name, "r");
    if (f == NULL) {
        return fallback;
    }
    int value = fallback;
    if (fscanf(f, "%d", &value) != 1) {
        value = fallback;
    }
    fclose(f);
    return value;
}

/*
 * Write the run's outcome to the SD card so the host can read it back.
 *
 * The guest console is drawn to the bottom screen, which a headless emulator
 * discards — so anything the port wants to tell an automated gate has to leave
 * through the filesystem. This is also the hook a frame hash would grow out of.
 *
 * `rom` is the ROM outcome as an integer, not a string, because the gate parses
 * this file with sed and a path containing spaces would need quoting rules the
 * reader would then have to get right. The path itself is deliberately NOT
 * written here: it is the user's filename on their own card, and the gate has
 * no use for it.
 */
static void write_gate_result(uint32_t frames, int max_frames, int rom_status) {
    FILE *f = fopen("sm_gate_result.txt", "w");
    if (f == NULL) {
        return;
    }
    fprintf(f, "frames=%" PRIu32 "\n", frames);
    fprintf(f, "max_frames=%d\n", max_frames);
    fprintf(f, "with_core=%d\n",
#ifdef SM_WITH_CORE
            1
#else
            0
#endif
    );
    fprintf(f, "rom=%d\n", (int)rom_status);
    fclose(f);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    SmRenderInit();
    SmInputInit();
    const bool audioUp = SmAudioInit();

    if (!audioUp) {
        /* Video and input still work without audio, so this is a warning, not a
         * bail. Silently continuing with no sound would read as "the port has
         * no audio" rather than "the audio service failed to start". */
        printf("audio init failed; continuing without sound\n");
    }

#ifdef SM_WITH_CORE
    /*
     * ROM first, before the frame budget is read and before any frame runs.
     * Loading it here means a wrong or missing ROM is refused while the screen
     * still shows the report, instead of half way through a run.
     *
     * A failure here does NOT abort: the frontend must still reach the frame
     * loop and still write its result file, or the gate sees "emulator exited
     * before writing sm_gate_result.txt" and reports a crash where the truth is
     * a missing file. The rom= line in the result carries the real reason.
     *
     * Pass argv[0] (path to .3dsx) so the ROM loader can search in the same
     * directory (zelda3 method).
     */
    const SmRomStatus rom_status = smrecomp_rom_load(argv[0]);
    if (rom_status != SMROM_OK) {
        printf("rom: REFUSED — %s\n", smrecomp_rom_status_text(rom_status));
    }

    /*
     * Boot the SNES machine from the loaded ROM. This registers the game,
     * allocates the guest state, runs the ROM's reset vector up to the first
     * WaitForNMI yield, and sets up the SPC audio player. Must happen before
     * any call to smrecomp_frame_begin() or the guest state is garbage and
     * the first frame crashes.
     *
     * A refused ROM does not prevent initialisation — the gate still needs
     * to render the "rom=refused" result file. SnesInit will itself refuse a
     * bad image and the gate's rom= line carries the real reason.
     */
    if (rom_status == SMROM_OK) {
        smrecomp_init();
    }
#else
    const int rom_status = 0;
#endif

    /*
     * A fixed frame count makes the process terminate on its own. This exists so
     * the automated gate has a clean success signal: a smoke test that must be
     * killed by a timeout cannot distinguish "ran correctly for the whole
     * window" from "hung after 3 frames and the timeout saved it". Reaching the
     * frame count and exiting 0 is the assertion; anything else is a failure.
     *
     * The env var is read too, because it costs one line and is the right
     * channel on hardware or any host that does propagate it.
     */
    int max_frames = read_sd_int("sm_max_frames", 0);
    const char *env = getenv("SM_MAX_FRAMES");
    if (env != NULL) {
        max_frames = atoi(env);
    }

    report_status();
    printf("max_frames: %d\n", max_frames);
    uint32_t tick = 0;

    while (aptMainLoop()) {
        hidScanInput();

        if (SmInputStartPressed()) {
            break;
        }

        uint16_t pad = SmInputPoll();

        SmFrame frame;
#ifdef SM_WITH_CORE
        /* Feed the pad BEFORE the frame runs. Without this the core sees a
         * permanently-neutral controller and the game is unplayable — a bug that
         * would not crash, would not be reported by any gate, and would look
         * like "the input mapping is broken" on hardware. */
        smrecomp_set_pad(pad);
        smrecomp_frame_begin();
        if (!smrecomp_frame_rgb565(&frame)) {
            /* The PPU has not produced a frame yet — the first frame has not
             * completed its raster pass. Wait one more loop before giving up;
             * the title screen may still appear on frame 2. */
            if (tick > 2) {
                printf("render: no frame from core after %" PRIu32 " ticks\n", tick);
            }
        }
        smrecomp_render_audio();
#else
        build_test_frame(tick, pad);
        frame.pixels = s_testFrame;
        frame.width = TEST_W;
        frame.height = TEST_H;
#endif

        if (!SmRenderPresent(&frame)) {
            /* A present that fails means the core handed us nothing usable.
             * Saying so beats drawing a frozen frame and letting the user
             * believe the game is simply slow. */
            printf("render: frame rejected (null or zero-dimension)\n");
        }

        tick++;
        if (max_frames > 0 && (int)tick >= max_frames) {
            /* Normal, self-requested termination. The gate keys on this. */
            break;
        }

        /* Pace frames to ~60 Hz. Without this the loop runs as fast as possible
         * and the game would execute thousands of guest cycles per real second,
         * exhausting the frame budget in milliseconds and producing no useful
         * output. The SNES runs at 21.477 MHz NTSC; 60 Hz is the closest
         * practical target for a headless gate. */
        const s64 NS_PER_FRAME = 1000000000LL / 60;
        svcSleepThread(NS_PER_FRAME);
    }

    write_gate_result(tick, max_frames, rom_status);
    hidExit();
    SmAudioExit();
    SmRenderExit();
    return 0;
}
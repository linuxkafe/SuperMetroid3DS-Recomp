/*
 * platform/3ds/host.c — the 3DS host glue between the frontend and the core.
 *
 * The core is a library, not an executable: it exports the game's simulation
 * and render entry points and reads the framebuffer through a global, and it
 * expects the host to drive it. This file is that driver.
 *
 * WHY THE API LOOKS LIKE THIS
 * An earlier version of this file invented three functions
 * (smrecomp_frame_begin / smrecomp_frame_rgb565 / smrecomp_set_pad) that do
 * not exist anywhere in the framework. The build linked against nothing and
 * would have shipped a port that calls into the void. The real entry points,
 * read from sm_rtl.c and the desktop host_main.c, are:
 *
 *   RunOneFrameOfGame()   — advance the simulation one frame
 *   SmDrawPpuFrame()      — run the PPU raster pass for that frame
 *   g_ppu->renderBuffer   — the composited frame, uint32_t ARGB
 *   g_ppu->renderPitch    — its pitch in bytes
 *
 * HONEST LIMIT, STATED UP FRONT
 * These functions are wired. They are NOT yet exercised. Running the game also
 * requires host initialisation that lives in the excluded src/main.c — ROM
 * loading, SnesInit, reset, the SPC player, and the per-frame pacing loop. That
 * work is T033, not T027. So the frame path below is correct in shape but has
 * never produced a game pixel. Calling it before T033 would crash, and the
 * gate must not pretend otherwise.
 */
#include <3ds.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "host.h"
#include "rom_image_verify.h"
#include "snes/ppu.h"
#include "spc_player.h"
#include "sm_spc_player.h"
#include "3ds_audio.h"

/* kSuperMetroidGameInfo is defined in sm_cpu_infra.c and used here through
 * the same extern declaration pattern as upstream's main.c. */
extern const RtlGameInfo kSuperMetroidGameInfo;

/* Generated at configure time from upstream's rom_identity.txt — see
 * CMakeLists.txt. NOT written out by hand: the digest has exactly one source of
 * truth upstream, and a literal pasted here would be a second one that a ROM
 * revision would update while silently missing this file. */
#include "snesrecomp_rom_identity.h"

/* The game's own entry points, exported by the core library. */
extern void RunOneFrameOfGame(void);
extern void SmDrawPpuFrame(void);

/* g_ppu is the PPU state; renderBuffer is the composited frame. Both are
 * exported by the framework (common_rtl.h declares g_ppu). */
extern Ppu *g_ppu;

/* ── ROM loading and verification (T033 step A) ──────────────────────────── */

/*
 * Where the ROM is looked for, in order. The first entry is the exact filename
 * upstream's rom_identity.txt names, so a user who follows upstream's own
 * instructions gets the right file without reading this port's docs. The other
 * two are the names a person or a script is likely to have used instead.
 *
 * A LIST rather than one fixed name because the failure this exists to prevent
 * is "the ROM is right there and the port says it cannot find it". Every path
 * tried is printed on failure, so the message names the answer.
 */
static const char *const kRomCandidates[] = {
    SNESRECOMP_ROM_FILE,
    "sm.sfc",
    "SuperMetroid-usa.sfc",
};
#define ROM_CANDIDATE_COUNT (sizeof(kRomCandidates) / sizeof(kRomCandidates[0]))

static uint8_t *s_rom;
static size_t s_rom_size;
static char s_rom_path[256];

/* Directory containing the .3dsx (extracted from argv[0]). Empty if not set. */
static char s_exe_dir[256] = {0};

/*
 * "12b77c4b…" -> 32 bytes. The identity header carries the digest as text
 * because that is what rom_identity.txt holds; snesrecomp_rom_verify_sha256()
 * wants bytes. Returns false on a malformed string rather than trusting it —
 * a silently-truncated expected digest would make the check compare against
 * garbage and reject every ROM, including the right one.
 */
static bool expected_digest_from_hex(const char *hex, uint8_t out[32]) {
    if (strlen(hex) != 64) {
        return false;
    }
    for (int i = 0; i < 32; i++) {
        int hi = -1, lo = -1;
        for (int k = 0; k < 2; k++) {
            const char c = hex[i * 2 + k];
            int v;
            if (c >= '0' && c <= '9') {
                v = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                v = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                v = c - 'A' + 10;
            } else {
                return false;
            }
            if (k == 0) {
                hi = v;
            } else {
                lo = v;
            }
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

/* Read `path` whole into a fresh malloc. NULL on any failure. */
static uint8_t *read_whole_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    const long raw = ftell(f);
    if (raw <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }

    uint8_t *buf = (uint8_t *)malloc((size_t)raw);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    const size_t got = fread(buf, 1, (size_t)raw, f);
    fclose(f);
    if (got != (size_t)raw) {
        /* A short read on the SD card is a real failure and the single most
         * likely way a 3 MiB read goes wrong. Handing a half-read image to
         * SnesInit would produce a boot that looks like a corrupt ROM. */
        free(buf);
        return NULL;
    }
    *out_size = (size_t)raw;
    return buf;
}

/* Extract directory from argv[0] (path to .3dsx). Called once at startup. */
void smrecomp_set_exe_dir(const char *argv0) {
    if (argv0 == NULL) {
        s_exe_dir[0] = '\0';
        return;
    }
    /* Find last '/' in the path. */
    const char *last_slash = strrchr(argv0, '/');
    if (last_slash == NULL) {
        /* No directory component, use current directory. */
        s_exe_dir[0] = '\0';
        return;
    }
    size_t dir_len = (size_t)(last_slash - argv0);
    if (dir_len >= sizeof(s_exe_dir)) {
        dir_len = sizeof(s_exe_dir) - 1;
    }
    memcpy(s_exe_dir, argv0, dir_len);
    s_exe_dir[dir_len] = '\0';
    printf("rom: exe directory: %s\n", s_exe_dir);
}

/* Build a full path: dir + "/" + candidate. Returns true if fits in buf. */
static bool build_candidate_path(char *buf, size_t bufsz, const char *dir, const char *candidate) {
    if (dir[0] == '\0') {
        /* No directory: just use candidate as-is (relative to cwd). */
        return snprintf(buf, bufsz, "%s", candidate) < (int)bufsz;
    }
    return snprintf(buf, bufsz, "%s/%s", dir, candidate) < (int)bufsz;
}

SmRomStatus smrecomp_rom_load(const char *argv0) {
    /* Idempotent: a second call must not leak the first image. */
    free(s_rom);
    s_rom = NULL;
    s_rom_size = 0;
    s_rom_path[0] = '\0';

    /* Set exe directory from argv[0] if provided (zelda3 method). */
    if (argv0 != NULL) {
        smrecomp_set_exe_dir(argv0);
    }

    uint8_t expected[32];
    if (!expected_digest_from_hex(SNESRECOMP_ROM_EXPECTED_SHA256, expected)) {
        printf("rom: rom_identity.txt carries no usable sha256; "
               "this build cannot verify a ROM\n");
        return SMROM_ERR_DIGEST;
    }

    /* Pass 1: which candidate exists.
     * Search order (zelda3 method):
     *   1. Same directory as the .3dsx
     *   2. Current working directory / SD root
     */
    const char *found = NULL;
    char full_path[256];

    for (size_t i = 0; i < ROM_CANDIDATE_COUNT; i++) {
        /* Try exe directory first. */
        if (s_exe_dir[0] != '\0') {
            if (build_candidate_path(full_path, sizeof(full_path), s_exe_dir, kRomCandidates[i])) {
                if (snesrecomp_rom_is_readable(full_path)) {
                    found = full_path;
                    break;
                }
            }
        }
        /* Fall back to candidate as-is (cwd / SD root). */
        if (snesrecomp_rom_is_readable(kRomCandidates[i])) {
            found = kRomCandidates[i];
            break;
        }
    }

    if (found == NULL) {
        printf("rom: not found. Tried:\n");
        for (size_t i = 0; i < ROM_CANDIDATE_COUNT; i++) {
            if (s_exe_dir[0] != '\0') {
                char p[256];
                build_candidate_path(p, sizeof(p), s_exe_dir, kRomCandidates[i]);
                printf("  - %s\n", p);
            }
            printf("  - %s (cwd/SD root)\n", kRomCandidates[i]);
        }
        printf("rom: place a legally obtained Super Metroid (Japan, USA) "
               ".sfc at one of those names\n");
        return SMROM_ERR_NOT_FOUND;
    }

    /* Pass 2: verify before loading it into memory. A wrong ROM is refused on
     * its digest, not on whatever it happens to do when the CPU starts running
     * it — that is the difference between a readable message and a hang. */
    if (!snesrecomp_rom_verify_sha256(found, expected)) {
        printf("rom: '%s' is not this ROM (sha256 mismatch).\n", found);
        printf("rom: expected sha256 %s\n", SNESRECOMP_ROM_EXPECTED_SHA256);
        return SMROM_ERR_DIGEST;
    }

    size_t size = 0;
    uint8_t *image = read_whole_file(found, &size);
    if (image == NULL) {
        printf("rom: '%s' verified but could not be read in full.\n", found);
        return SMROM_ERR_UNREADABLE;
    }

    /*
     * KNOWN LIMIT, not worked around: upstream's verifier hashes the image
     * WITHOUT a 512-byte copier header (rom_image_verify.c strips one when
     * size %% 1024 == 512), while upstream's own host hands SnesInit the RAW
     * file. That is upstream's behaviour and this port mirrors it rather than
     * diverging silently. For a headerless image — which is what this ROM is,
     * 3145728 bytes exactly — the two are the same bytes. A headered copy would
     * verify and then boot wrong; that is a real hazard and it is recorded,
     * not papered over.
     */
    s_rom = image;
    s_rom_size = size;
    snprintf(s_rom_path, sizeof(s_rom_path), "%s", found);
    printf("rom: %s (%zu bytes, sha256 verified)\n", found, size);
    return SMROM_OK;
}

const char *smrecomp_rom_status_text(SmRomStatus status) {
    switch (status) {
        case SMROM_OK:            return "ok";
        case SMROM_ERR_NOT_FOUND: return "not found";
        case SMROM_ERR_UNREADABLE:return "found but unreadable";
        case SMROM_ERR_DIGEST:    return "wrong ROM (sha256 mismatch)";
    }
    return "unknown";
}

const char *smrecomp_rom_path(void) {
    return s_rom_path[0] != '\0' ? s_rom_path : NULL;
}

size_t smrecomp_rom_size(void) {
    return s_rom_size;
}

/* ── Core initialisation (T033 step B / C) ───────────────────────────────── */

/*
 * Register the game with the framework and boot the SNES machine from the
 * loaded ROM image. Must be called after smrecomp_rom_load() returns SMROM_OK
 * and before the first call to smrecomp_frame_begin().
 *
 * Order matters:
 *   1. RtlRegisterGame() — the framework needs kSuperMetroidGameInfo before
 *      SnesInit calls the .initialize hook (SmInitialize → SmPostMortemRegister).
 *   2. SnesInit() — allocates g_snes, g_ppu, g_cpu, loads the ROM into cart
 *      memory, runs snes_reset, sets up the CPU state in native mode, and
 *      registers the APU/DSP model. Returns NULL on a bad ROM; the caller
 *      must not proceed.
 *   3. SPC player — created from the game descriptor, initialised to reset
 *      the DSP to sane defaults. If the descriptor has no create_spc_player
 *      hook, g_spc_player stays NULL and the guest's IPL upload path is
 *      untouched (which is what happens when there is no host interception).
 *   4. RtlSetAudioOutputRate() — tells the framework the device rate so the
 *      APU pacing anchors are computed against the real consumer clock.
 *      The 3DS DAC runs at 48,000 Hz but the core emits at 32,040 Hz; the
 *      mismatch is T029. For now we pass the core rate so RtlRenderAudio
 *      does not try to resample itself.
 */
void smrecomp_init(void) {
    RtlRegisterGame(&kSuperMetroidGameInfo);
    Snes *snes = SnesInit(s_rom, (int)s_rom_size);
    if (snes == NULL) {
        printf("snes: SnesInit failed (bad ROM image)\n");
        abort();
    }
    printf("snes: initialised (%zu bytes)\n", s_rom_size);

    /* Create the SPC audio player if one is not already present.
     * SmSpcPlayer_Create allocates a 64 KB APU-RAM buffer, resets the DSP,
     * and installs the upload hook that the guest's IPL calls on first boot.
     * Without this the guest's APU upload would find g_spc_player == NULL
     * and fall back to the raw SPL bootrom, which does not play music. */
    if (g_spc_player == NULL) {
        SpcPlayer *player = SmSpcPlayer_Create();
        if (player != NULL) {
            player->initialize(player);
            g_spc_player = player;
            printf("snes: SPC player created\n");
        }
    }

    RtlSetAudioOutputRate(SM3DS_CORE_SAMPLE_RATE);
    printf("snes: audio rate set to %d Hz\n", SM3DS_CORE_SAMPLE_RATE);
}

/* ── Audio (T033 step D) ─────────────────────────────────────────────────── */

/*
 * Pull one frame's worth of stereo S16 PCM from the framework's APU renderer
 * and hand it to the 3DS audio driver. Called after each call to
 * smrecomp_frame_begin() / smrecomp_frame_rgb565(), before the next frame
 * runs.
 *
 * The desktop host runs its audio through an SDL pull callback; on 3DS there
 * is no callback thread, so the host drives audio synchronously from the main
 * loop. The call is safe because RtlRenderAudio runs entirely under the APU
 * lock (single-threaded on 3DS, the lock is a no-op), and it blocks only
 * while waiting for the DSP ring to drain — a bounded wait that exits when
 * the device consumes enough samples.
 *
 * Buffer size: 1024 stereo frames (2048 int16 values). Matches the desktop's
 * default `g_frames_per_block` at the core sample rate and is small enough
 * to avoid visible latency on the 3DS.
 */
#define SM_AUDIO_SAMPLES (1024)

void smrecomp_render_audio(void) {
    static int16_t s_buf[SM_AUDIO_SAMPLES * 2];
    RtlRenderAudio(s_buf, SM_AUDIO_SAMPLES, 2);
    SmAudioSubmit(s_buf, SM_AUDIO_SAMPLES);
}

/* ── Frame path ──────────────────────────────────────────────────────────── */

/* The framebuffer is uint32_t ARGB. The 3DS panel is RGB565, so a conversion
 * is unavoidable. This is the one place the port changes pixel format, and it
 * is the obvious place for a colour bug to hide — see T030, which exists
 * specifically to catch it. */
static uint16_t argb_to_rgb565(uint32_t argb) {
    uint32_t r = (argb >> 16) & 0xFFu;
    uint32_t g = (argb >> 8) & 0xFFu;
    uint32_t b = argb & 0xFFu;
    return (uint16_t)((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}

void smrecomp_frame_begin(void) {
    RunOneFrameOfGame();
    SmDrawPpuFrame();
}

/* The SNES frame is 256x224. The 3DS panel is 400x240 and the frontend
 * letterboxes it, so the host only ever produces this size. */
#define SM_FRAME_W 256
#define SM_FRAME_H 224

/* The converted frame. Declared here so smrecomp_frame_rgb565 can use it;
 * declared static so the frontend cannot hold a stale pointer across calls. */
static uint16_t s_frame[SM_FRAME_W * SM_FRAME_H] __attribute__((aligned(4)));

/*
 * Copy the core's composited frame into `frame` as RGB565. Returns false if
 * the PPU has not produced a frame yet — which is the honest signal that the
 * host initialisation (T033) has not run, and which the frontend reports
 * rather than presenting a frozen or black screen.
 */
bool smrecomp_frame_rgb565(SmFrame *frame) {
    if (g_ppu == NULL || g_ppu->renderBuffer == NULL || frame == NULL) {
        return false;
    }

    const uint32_t *src = (const uint32_t *)g_ppu->renderBuffer;
    int pitch_px = (int)(g_ppu->renderPitch / 4);
    for (int y = 0; y < SM_FRAME_H; y++) {
        for (int x = 0; x < SM_FRAME_W; x++) {
            s_frame[y * SM_FRAME_W + x] =
                argb_to_rgb565(src[y * pitch_px + x]);
        }
    }

    frame->pixels = s_frame;
    frame->width = SM_FRAME_W;
    frame->height = SM_FRAME_H;
    return true;
}

void smrecomp_set_pad(uint16_t pad) {
    RtlSetPadState(0, pad);
}

/* Stubs for diagnostic replay (v0.3.14). The upstream's sm_replay.c depends on
 * desktop-only replay writer and config; the 3DS port does not use replay. */
void SmReplayStateLoaded(void) {}
bool SmReplayBegin(uint32_t *inputs) { (void)inputs; return true; }
void SmReplayEnd(void) {}
/*
 * APU lock and the block-enter tracing hook.
 *
 * These three symbols are normally provided by snesrecomp's debug_server.c,
 * which is a desktop-only TCP host surface and is not built here. They are
 * host hooks, not part of the scheduler, so providing them is a porting
 * decision, not a stub of game logic.
 *
 * RtlApuLock/RtlApuUnlock serialize the APU output ring. On 3DS there is one
 * thread, so the lock is a no-op — not a simplification, the actual
 * concurrency is single-threaded.
 *
 * debug_on_block_enter is called when the interpreter hits a branch the AOT
 * tier cannot handle. It is a tracing hook for the debug server; with no
 * debug server there is nothing to report to. Returning silently is correct,
 * and the call site still runs.
 */
void RtlApuLock(void)   {}
void RtlApuUnlock(void) {}
void debug_on_block_enter(uint32_t pc, uint32_t a, uint32_t x, uint32_t y) {
    (void)pc; (void)a; (void)x; (void)y;
}

/*
 * recomp_post_mortem_set_game_section is the desktop crash-report hook that
 * lets a title register its own section in the post-mortem output. On 3DS
 * there is no post-mortem report path, so a no-op is correct. Defined here
 * so sm_post_mortem.c (which calls it unconditionally) links.
 */
typedef void (*RecompPostMortemGameSectionFn)(void);
void recomp_post_mortem_set_game_section(RecompPostMortemGameSectionFn fn) {
    (void)fn;
}

/* Define g_spc_player: the framework reads this symbol to intercept SPC
 * uploads. The 3DS host creates the player in smrecomp_init() and stores
 * the pointer here. */
SpcPlayer *g_spc_player = NULL;

/*
 * WRAM write tracing hooks. Like debug_on_block_enter, these are callbacks the
 * framework calls on every guest memory write when the debug server is armed.
 * With no debug server there is nothing to report to, so they are no-ops. They
 * are NOT part of the simulation: the write still happens; only the report is
 * suppressed. That is the whole difference between this and stubbing the
 * scheduler, which would change what the game does.
 */
void debug_on_wram_write_byte(uint32_t addr, uint8_t old_val, uint8_t new_val) {
    (void)addr; (void)old_val; (void)new_val;
}
void debug_on_wram_write_word(uint32_t addr, uint16_t old_val, uint16_t new_val) {
    (void)addr; (void)old_val; (void)new_val;
}

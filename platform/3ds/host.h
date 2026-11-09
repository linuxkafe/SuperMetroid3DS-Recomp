/*
 * platform/3ds/host.h — the 3DS host glue between the frontend and the core.
 *
 * The core is a library: it exports the game's simulation and render entry
 * points and reads the framebuffer through a global, and it expects the host
 * to drive it. This is that driver.
 *
 * The API here is the real one, read from sm_rtl.c and the desktop host_main.c.
 * An earlier version invented smrecomp_frame_begin / smrecomp_frame_rgb565 /
 * smrecomp_set_pad as functions that do not exist anywhere in the framework;
 * the build linked against nothing. RunOneFrameOfGame(), SmDrawPpuFrame() and
 * g_ppu->renderBuffer are the actual entry points.
 *
 * HONEST LIMIT: smrecomp_frame_begin() is wired but not yet exercised. Running
 * the game also needs host initialisation that lives in the excluded
 * src/main.c — SnesInit, reset, the SPC player and the per-frame pacing loop.
 * That is T033. Calling the frame path before then would crash, and the gate
 * must not pretend otherwise. ROM loading and verification (T033 step A) IS
 * done and is honest about what it does not yet check.
 */
#ifndef SM3DS_PLATFORM_HOST_H
#define SM3DS_PLATFORM_HOST_H

#include <stddef.h>
#include <stdint.h>

#include "3ds_render.h"

/* ── ROM (T033 step A) ───────────────────────────────────────────────────── */

typedef enum {
    SMROM_OK = 0,
    SMROM_ERR_NOT_FOUND,   /* no candidate path exists                     */
    SMROM_ERR_UNREADABLE,  /* the file exists but could not be read in full */
    SMROM_ERR_DIGEST,      /* sha256 is not this ROM — a wrong-game refusal */
} SmRomStatus;

/*
 * Find the ROM, verify its sha256 against the digest upstream compiled in,
 * and keep it in memory for SnesInit.
 *
 * The search order (zelda3 method):
 *   1. Same directory as the .3dsx executable (from argv[0])
 *   2. Current working directory / SD root
 *
 * Returns SMROM_OK only when the bytes in memory are the ones upstream's
 * rom_identity.txt names. Every other outcome is a distinct status.
 */
SmRomStatus smrecomp_rom_load(const char *argv0);

/* A one-line, user-readable explanation of a status. Never NULL. */
const char *smrecomp_rom_status_text(SmRomStatus status);

/* The path the ROM was loaded from, or NULL if none. For the gate's result
 * file and the on-screen report — a verified ROM from an unexpected place is
 * worth seeing named. */
const char *smrecomp_rom_path(void);

/* Byte count of the loaded image, or 0 if nothing is loaded. */
size_t smrecomp_rom_size(void);

/* ── Initialisation (T033 step B / C) ───────────────────────────────────── */

/*
 * Register the game, boot the SNES machine from the loaded ROM, create the
 * SPC player and set the audio output rate. Must be called after
 * smrecomp_rom_load() returns SMROM_OK and before the first call to
 * smrecomp_frame_begin(). Calling it again is a no-op for the ROM and player
 * (SnesInit reuses the already-loaded image), but RtlSetAudioOutputRate is
 * idempotent so repeated calls are harmless.
 */
void smrecomp_init(void);

/* ── Audio (T033 step D) ─────────────────────────────────────────────────── */

/*
 * Pull one frame's worth of stereo S16 PCM from the framework's APU renderer
 * and hand it to the 3DS audio driver. Call after each frame render and
 * before the next smrecomp_frame_begin().
 */
void smrecomp_render_audio(void);

/* ── Frame path ──────────────────────────────────────────────────────────── */

/* Advance the simulation one frame and run the PPU raster pass. */
void smrecomp_frame_begin(void);

/* Copy the core's composited frame into `frame` as RGB565.
 * Returns false if the PPU has not produced a frame yet. */
bool smrecomp_frame_rgb565(SmFrame *frame);

/* Feed the SNES pad word to the core's LLE joypad model via RtlSetPadState.
 * Called before each smrecomp_frame_begin() so the guest sees the current
 * controller state at the start of its NMI handler. */
void smrecomp_set_pad(uint16_t pad);

/* APU lock and the block-enter tracing hook. Normally provided by
 * snesrecomp's debug_server.c, which is a desktop-only TCP host surface and is
 * not built here. See host.c for the reasoning. */
void RtlApuLock(void);
void RtlApuUnlock(void);
void debug_on_block_enter(uint32_t pc, uint32_t a, uint32_t x, uint32_t y);
void debug_on_wram_write_byte(uint32_t addr, uint8_t old_val, uint8_t new_val);
void debug_on_wram_write_word(uint32_t addr, uint16_t old_val, uint16_t new_val);

#endif /* SM3DS_PLATFORM_HOST_H */

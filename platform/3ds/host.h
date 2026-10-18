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
 * HONEST LIMIT: these are wired but not yet exercised. Running the game also
 * needs host initialisation that lives in the excluded src/main.c — ROM
 * loading, SnesInit, reset, the SPC player and the per-frame pacing loop. That
 * is T033. Calling these before then would crash, and the gate must not
 * pretend otherwise.
 */
#ifndef SM3DS_PLATFORM_HOST_H
#define SM3DS_PLATFORM_HOST_H

#include "3ds_render.h"

/* Advance the simulation one frame and run the PPU raster pass. */
void smrecomp_frame_begin(void);

/* Copy the core's composited frame into `frame` as RGB565.
 * Returns false if the PPU has not produced a frame yet. */
bool smrecomp_frame_rgb565(SmFrame *frame);

/* Feed the SNES pad word to the core's LLE joypad model.
 * Stubbed until T033 wires real input. */
void smrecomp_set_pad(uint16_t pad);

#endif /* SM3DS_PLATFORM_HOST_H */

/* APU lock and the block-enter tracing hook. Normally provided by
 * snesrecomp's debug_server.c, which is a desktop-only TCP host surface and is
 * not built here. See host.c for the reasoning. */
void RtlApuLock(void);
void RtlApuUnlock(void);
void debug_on_block_enter(uint32_t pc, uint32_t a, uint32_t x, uint32_t y);
void debug_on_wram_write_byte(uint32_t addr, uint8_t old_val, uint8_t new_val);
void debug_on_wram_write_word(uint32_t addr, uint16_t old_val, uint16_t new_val);

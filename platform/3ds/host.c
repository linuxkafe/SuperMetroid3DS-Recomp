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

#include "common_rtl.h"
#include "host.h"
#include "snes/ppu.h"

/* The game's own entry points, exported by the core library. */
extern void RunOneFrameOfGame(void);
extern void SmDrawPpuFrame(void);

/* g_ppu is the PPU state; renderBuffer is the composited frame. Both are
 * exported by the framework (common_rtl.h declares g_ppu). */
extern Ppu *g_ppu;

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
    /* The core reads input through the LLE joypad model. The hook that feeds
     * it is part of the host initialisation (T033); this stub exists only so
     * the symbol resolves. It must be replaced before any frame runs. */
    (void)pad;
}
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

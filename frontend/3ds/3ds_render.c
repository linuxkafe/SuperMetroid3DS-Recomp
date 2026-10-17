#include "3ds_render.h"

#include <3ds.h>
#include <stdlib.h>
#include <string.h>

static int s_topFBWidth = 0;
static int s_topFBHeight = 0;

bool SmRenderInit(void) {
    /* RGB565 both screens: the SNES composite is RGB565, so this is the format
     * that needs no conversion on the way to the panel. The bottom screen is
     * set to match so a future HUD does not have to repaint the palette. */
    gfxInit((GSPGPU_FramebufferFormat)GPU_RGB565, (GSPGPU_FramebufferFormat)GPU_RGB565, true);
    gfxSetDoubleBuffering(GFX_TOP, true);
    consoleInit(GFX_BOTTOM, NULL);

    /* Ask for the panel's real frame buffer dimensions rather than assuming
     * 400x240. What comes back is what SmRenderPresent letterboxes against, so
     * hardcoding either one is a guess that happens to be right on a 2DS and
     * wrong everywhere else. */
    u16 w = 0, h = 0;
    (void)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &w, &h);
    s_topFBWidth = (int)w;
    s_topFBHeight = (int)h;
    return s_topFBWidth > 0 && s_topFBHeight > 0;
}

void SmRenderExit(void) { gfxFlushBuffers(); gfxScreenSwapBuffers(GFX_TOP, true); }

/*
 * Integer letterbox scale: the largest scale that fits, at least 1.
 *
 * Integer-only on purpose. A fractional scale would need a resampler, and a
 * nearest-neighbour resample of a 256x224 SNES frame at 1.5x produces visible
 * shimmer on the pixel art. Half-pixel letterbox bars are a smaller cost than
 * a resampler that can differ between runs.
 */
static void compute_fit(const SmFrame *f, int *out_x, int *out_y, int *out_w, int *out_h,
                       int *out_scale) {
    int scale_x = s_topFBWidth / f->width;
    int scale_y = s_topFBHeight / f->height;
    int scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale < 1) {
        scale = 1;
    }

    *out_w = f->width * scale;
    *out_h = f->height * scale;
    *out_x = (s_topFBWidth - *out_w) / 2;
    *out_y = (s_topFBHeight - *out_h) / 2;
    *out_scale = scale;
}

bool SmRenderPresent(const SmFrame *frame) {
    if (frame == NULL || frame->pixels == NULL || frame->width <= 0 || frame->height <= 0) {
        return false;
    }

    int x = 0, y = 0, w = 0, h = 0, scale = 1;
    compute_fit(frame, &x, &y, &w, &h, &scale);

    uint16_t *fb = (uint16_t *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);

    for (int row = 0; row < h; row++) {
        const uint16_t *src_row = frame->pixels + (row / scale) * frame->width;
        uint16_t *dst = fb + (y + row) * s_topFBWidth + x;
        for (int col = 0; col < w; col++) {
            dst[col] = src_row[col / scale];
        }
    }

    gfxFlushBuffers();
    gfxScreenSwapBuffers(GFX_TOP, true);
    return true;
}
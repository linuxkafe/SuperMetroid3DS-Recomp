/*
 * 3DS render target — presents the SNES composite to the top screen.
 *
 * The core hands us the SNES frame buffer in the format sm_renderer.c already
 * builds. We do not convert it here: 3DS framebuffers are RGB565 and so is the
 * 3DS GPU's natural texture format, so a straight blit is both the fastest
 * path and the one that cannot introduce a colour bug.
 *
 * Only geometry is ours. Aspect, scanline count and HUD policy are the core's
 * (src/sm_display.c, src/sm_video.c) — duplicating that logic here is how the
 * DS port ended up with two sources of truth for the same question.
 */
#ifndef SM3DS_RENDER_H
#define SM3DS_RENDER_H

#include <stdbool.h>
#include <stdint.h>

/* The SNES composite, as the core produces it. */
typedef struct {
    const uint16_t *pixels; /* RGB565, top-down, no padding */
    int width;
    int height;
} SmFrame;

/* Geometry the 3DS side has to know about itself: the panel, not the game. */
#define SM3DS_TOP_WIDTH 400
#define SM3DS_TOP_HEIGHT 240
#define SM3DS_BOTTOM_WIDTH 320
#define SM3DS_BOTTOM_HEIGHT 240

bool SmRenderInit(void);
void SmRenderExit(void);

/*
 * Blit `frame` to the top screen, letterboxed to preserve aspect.
 *
 * Returns false if the frame is unusable (null pixels or a zero dimension) —
 * that is a core bug, not a render bug, and the caller decides whether it is
 * fatal. We do not paper over it here.
 */
bool SmRenderPresent(const SmFrame *frame);

#endif /* SM3DS_RENDER_H */
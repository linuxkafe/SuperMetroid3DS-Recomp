#include "3ds_audio.h"

#include <3ds.h>
#include <string.h>

/* ndspWaveBuf is 0x400-byte aligned; over-aligning avoids a memcpy per frame. */
static ndspWaveBuf s_waveBuf __attribute__((aligned(0x400)));
static int16_t s_sampleBuf[SM3DS_DAC_SAMPLE_RATE / 15 * 2] __attribute__((aligned(4)));

bool SmAudioInit(void) {
    if (ndspInit() != 0) {
        return false;
    }

    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)SM3DS_CORE_SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    memset(&s_waveBuf, 0, sizeof(s_waveBuf));
    s_waveBuf.data_vaddr = s_sampleBuf;
    s_waveBuf.nsamples = 0;
    ndspChnWaveBufAdd(0, &s_waveBuf);

    return true;
}

void SmAudioExit(void) { ndspExit(); }

void SmAudioSubmit(const int16_t *interleaved, size_t frames) {
    if (interleaved == NULL || frames == 0) {
        return;
    }

    size_t capacity = sizeof(s_sampleBuf) / sizeof(int16_t);
    size_t want = frames * 2; /* stereo */
    if (want > capacity) {
        want = capacity;
    }

    /*
     * Wait for the DAC to finish the buffer we handed it last frame before
     * overwriting it.
     *
     * libctru has no "samples remaining" query — only ndspChnIsPlaying — so the
     * wait is for the whole buffer, not for the tail of it. That costs up to
     * one buffer of latency, which at 32040 Hz is ~6 ms for the buffer below.
     * A drop instead of a wait would be cheaper and would click.
     */
    while (ndspChnIsPlaying(0)) {
        svcSleepThread(1);
    }

    memcpy(s_sampleBuf, interleaved, want * sizeof(int16_t));
    s_waveBuf.nsamples = (u32)frames;
    DSP_FlushDataCache(s_sampleBuf, want * sizeof(int16_t));
    ndspChnWaveBufAdd(0, &s_waveBuf);
}
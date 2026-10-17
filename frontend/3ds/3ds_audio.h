/*
 * 3DS audio — SPC700 output to the 3DS DAC.
 *
 * The core produces audio through snesrecomp's APU/DSP model and hands it to the
 * host as interleaved 16-bit PCM. This file only moves those samples into the
 * 3DS audio service; it does not resample.
 *
 * WHY NOT RESAMPLE: the SNES rate and the 3DS rate differ (32040 Hz vs 48000 Hz),
 * so a naive "write whatever the core produced" stutters and a resampler is the
 * obvious fix. But a resampler is a chunk of DSP with its own edge cases, and
 * getting it wrong is audible in a way a dropped frame is not. Until there is
 * a measured reason, the port runs the core's rate into the DAC and records the
 * mismatch as a ticket. Silence is not the goal; a wrong pitch is worse.
 */
#ifndef SM3DS_AUDIO_H
#define SM3DS_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The rate the core's LLE APU model emits, and the rate the 3DS DAC wants. */
#define SM3DS_CORE_SAMPLE_RATE 32040
#define SM3DS_DAC_SAMPLE_RATE  48000

bool SmAudioInit(void);
void SmAudioExit(void);

/* Push interleaved stereo S16 samples to the DAC queue. */
void SmAudioSubmit(const int16_t *interleaved, size_t frames);

#endif /* SM3DS_AUDIO_H */
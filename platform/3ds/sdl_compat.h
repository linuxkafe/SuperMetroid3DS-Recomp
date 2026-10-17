/*
 * platform/3ds/sdl_compat.h — the 3DS replacement for the desktop SDL
 * include boundary.
 *
 * WHY THIS FILE EXISTS
 * snesrecomp's common_rtl.c includes "sdl_compat.h", which on every desktop
 * target drags in <SDL.h>. The framework uses that header for two things: a
 * mutex wrapper and the audio mixing primitive. Neither needs SDL, and pulling
 * SDL onto a 3DS build is not an option.
 *
 * So this file provides the same symbols for the 3DS, backed by libctru. It is
 * a REPLACEMENT, not a compatibility layer: nothing here pretends to be SDL,
 * and any framework source that reaches for a real SDL API will fail to compile
 * rather than silently do the wrong thing. That is the intended behaviour — a
 * loud failure at build time beats a subtly wrong frame.
 *
 * The fiber API is NOT here. fiber_compat.h needs <ucontext.h> on its POSIX
 * path, which the 3DS newlib environment does not provide, so the scheduler
 * still does not build. See ticket T027.
 */
#ifndef SM3DS_PLATFORM_SDL_COMPAT_H
#define SM3DS_PLATFORM_SDL_COMPAT_H

#include <3ds.h>
#include <stdbool.h>
#include <stdint.h>

/* The framework's audio-mix volume ceiling, carried over unchanged so the
 * volume scale the core uses means the same thing here. */
#define SNESRECOMP_SDL_MIX_MAXVOLUME 128

/* The 3DS equivalent of the SDL audio buffer pointer type the framework passes
 * around. It is always a byte address and always carries S16 stereo frames. */
typedef void (*SmAudioMixFn)(uint8_t *dst, const uint8_t *src, int len,
                             int volume_0_128);

/*
 * Saturating S16 mix, matching SDL_MixAudioFormat(AUDIO_S16).
 *
 * Written out rather than delegated because this is the one piece of audio
 * arithmetic that must be bit-exact: an unclamped sum wraps, and a wrap in a
 * mixer is a click that appears only on loud frames, which is the hardest kind
 * of audio bug to hear and the easiest to attribute to the wrong layer.
 */
static inline void snesrecomp_sdl_mix_audio(uint8_t *dst, const uint8_t *src,
                                           int len, int volume_0_128) {
    int16_t *d = (int16_t *)(void *)dst;
    const int16_t *s = (const int16_t *)(const void *)src;
    int samples = len / (int)sizeof(int16_t);

    if (volume_0_128 < 0) {
        volume_0_128 = 0;
    } else if (volume_0_128 > SNESRECOMP_SDL_MIX_MAXVOLUME) {
        volume_0_128 = SNESRECOMP_SDL_MIX_MAXVOLUME;
    }

    for (int i = 0; i < samples; i++) {
        int acc = (int)d[i] + ((int)s[i] * volume_0_128) / SNESRECOMP_SDL_MIX_MAXVOLUME;
        if (acc > 32767) {
            acc = 32767;
        } else if (acc < -32768) {
            acc = -32768;
        }
        d[i] = (int16_t)acc;
    }
}

/*
 * The framework's mutex wrapper is a thin inline around SDL_LockMutex. libctru
 * exposes the same primitive, so this is a rename rather than a translation —
 * but it is kept as a named function so that if the 3DS ever needs a real
 * critical section the change lands in one place.
 */
typedef LightLock SmMutex;

static inline bool snesrecomp_sdl_lock_mutex(SmMutex *mutex) {
    if (mutex == NULL) {
        return false;
    }
    LightLock_Lock(mutex);
    return true;
}

static inline void snesrecomp_sdl_unlock_mutex(SmMutex *mutex) {
    if (mutex != NULL) {
        LightLock_Unlock(mutex);
    }
}

#endif /* SM3DS_PLATFORM_SDL_COMPAT_H */
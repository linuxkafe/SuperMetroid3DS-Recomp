#ifndef SUPERMETROIDDS_DS_RTL_H
#define SUPERMETROIDDS_DS_RTL_H

/*
 * DS-adapted Super Metroid frame driver — the LLE path of the desktop
 * sm_rtl.c, rehosted on the interp bridge. No fibers, no HLE gen stubs.
 */

#include <stdint.h>
#include "common_cpu_infra.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Drive one host frame of the guest (LLE only). */
void RunOneFrameOfGame(void);

/* Rasterize the current field (HDMA + line render + H/V-IRQ split). */
void SmDrawPpuFrame(void);

/* Host frame counter (mirrors the desktop counter_global_frames). */
extern uint16_t counter_global_frames;

/* Game registration consumed by SnesInit() (see ds_cpu_infra.c). */
extern RtlGameInfo kSuperMetroidGameInfo;

#ifdef __cplusplus
}
#endif

#endif /* SUPERMETROIDDS_DS_RTL_H */
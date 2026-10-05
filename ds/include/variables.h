/* DS game-specific RAM variable declarations (mirror of the SMR scaffold).
 *
 * The snesrecomp framework's snes/snes.c and snes/cpu.c #include
 * "variables.h"; the per-game port owns the file. This DS shim mirrors
 * SuperMetroidRecomp/src/variables.h: the frame-counter protocol extern and
 * the $7E:05B4 NMI handshake alias the frame driver keys off.
 */
#ifndef SUPERMETROIDDS_VARIABLES_H
#define SUPERMETROIDDS_VARIABLES_H

#include "types.h"

/* g_ram is declared by snesrecomp/runner/src/common_rtl.h. */

/* Host-protocol frame counter (framework-shaped). */
extern uint16 counter_global_frames;

/* Super Metroid's vblank handshake lives at $7E:05B4 (decomp:
 * `waiting_for_nmi`). WaitForNMI ($80:8338) sets it and spins at $80:8343
 * until the NMI handler ($80:9583, Vector_NMI) clears it. */
#define waiting_for_nmi (*(uint8 *)(g_ram + 0x05B4))

#endif /* SUPERMETROIDDS_VARIABLES_H */
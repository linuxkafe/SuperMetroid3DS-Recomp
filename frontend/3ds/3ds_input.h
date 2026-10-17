/*
 * 3DS input — maps HID state to the SNES controller word.
 *
 * The core reads the pad through its own LLE joypad model
 * (snesrecomp/runner/src/snes/joypad.c); we only produce the 16-bit active-low
 * pad byte pair that model expects. Getting the polarity or the bit order
 * wrong here produces a game that runs and reads every button as pressed, so
 * the constants below are named after SNES controller positions rather than
 * written as bare hex.
 */
#ifndef SM3DS_INPUT_H
#define SM3DS_INPUT_H

#include <stdbool.h>
#include <stdint.h>

/* SNES controller bits, as the joypad model sees them (active low: a set bit
 * in the value we return means RELEASED). */
#define SM_PAD_B     0x0080
#define SM_PAD_Y     0x0040
#define SM_PAD_SELECT 0x0020
#define SM_PAD_START 0x0010
#define SM_PAD_UP    0x0008
#define SM_PAD_DOWN  0x0004
#define SM_PAD_LEFT  0x0002
#define SM_PAD_RIGHT 0x0001
#define SM_PAD_A     0x0100
#define SM_PAD_X     0x0200
#define SM_PAD_L     0x0400
#define SM_PAD_R     0x0800

/* All buttons released: every bit set. Anything else is "some button down". */
#define SM_PAD_NEUTRAL 0xFFFF

void SmInputInit(void);

/* Read the current HID state into the SNES pad word. */
uint16_t SmInputPoll(void);

/* The START button edge (pressed this frame, not held), for pause/menu. */
bool SmInputStartPressed(void);

#endif /* SM3DS_INPUT_H */
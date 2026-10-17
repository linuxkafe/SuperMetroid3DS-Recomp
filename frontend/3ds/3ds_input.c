#include "3ds_input.h"

#include <3ds.h>
#include <string.h>

static uint32_t s_prevKeys = 0;

void SmInputInit(void) { s_prevKeys = 0; }

uint16_t SmInputPoll(void) {
    hidScanInput();
    u32 held = hidKeysHeld();

    /*
     * Build the word by clearing bits that are pressed. Starting from
     * SM_PAD_NEUTRAL and ANDing off held buttons keeps the polarity in one
     * place; the alternative — ORing in pressed bits — needs an inversion at
     * every call site and is exactly how an all-buttons-pressed pad happens.
     */
    uint16_t pad = SM_PAD_NEUTRAL;

    if (held & KEY_A)     pad &= (uint16_t)~SM_PAD_A;
    if (held & KEY_B)     pad &= (uint16_t)~SM_PAD_B;
    if (held & KEY_X)     pad &= (uint16_t)~SM_PAD_X;
    if (held & KEY_Y)     pad &= (uint16_t)~SM_PAD_Y;
    if (held & KEY_L)     pad &= (uint16_t)~SM_PAD_L;
    if (held & KEY_R)     pad &= (uint16_t)~SM_PAD_R;
    if (held & KEY_SELECT) pad &= (uint16_t)~SM_PAD_SELECT;
    if (held & KEY_START) pad &= (uint16_t)~SM_PAD_START;
    if (held & KEY_DUP)   pad &= (uint16_t)~SM_PAD_UP;
    if (held & KEY_DDOWN) pad &= (uint16_t)~SM_PAD_DOWN;
    if (held & KEY_DLEFT) pad &= (uint16_t)~SM_PAD_LEFT;
    if (held & KEY_DRIGHT) pad &= (uint16_t)~SM_PAD_RIGHT;

    s_prevKeys = held;
    return pad;
}

bool SmInputStartPressed(void) {
    return (hidKeysDown() & KEY_START) != 0;
}
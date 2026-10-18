/*
 * fiber-selftest.c — proves the cooperative fiber API works on 3DS.
 *
 * This is the gate for T027. A build succeeding is not evidence: the scheduler
 * is cooperative, so a broken switch looks exactly like a working one from
 * outside — the fiber never runs, or runs and never returns, and the only
 * difference is whether a value appears in the result file.
 *
 * The test writes to the SD root (sm_fiber_result.txt) so the host can read
 * it back after the run, the same channel the headless gate uses.
 */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>

#include "fiber_compat.h"

#define RESULT_FILE "sm_fiber_result.txt"

static void *g_main_fiber = NULL;
static int g_fiber_wrote = 0;

/* Runs inside the created fiber. Writes its marker, yields back to main, and
 * then returns — which abort()s in the real backend, so it must not fall off
 * the end. The yield is the whole point of the test. */
static void fiber_entry(void *arg) {
    (void)arg;
    g_fiber_wrote = 0xC0FFEE;
    SwitchToFiber(g_main_fiber);
    /* Unreachable: the scheduler abort()s if a fiber entry returns. */
}

int main(void) {
    FILE *out = fopen(RESULT_FILE, "w");
    if (!out) {
        /* Write nothing; the host will see no result file and report failure. */
        return 1;
    }

    g_main_fiber = ConvertThreadToFiber(0);
    if (!g_main_fiber) {
        fprintf(out, "result=fail\nreason=ConvertThreadToFiber\n");
        fclose(out);
        return 1;
    }

    void *child = CreateFiber(256 * 1024, fiber_entry, 0);
    if (!child) {
        fprintf(out, "result=fail\nreason=CreateFiber\n");
        fclose(out);
        return 1;
    }

    SwitchToFiber(child);

    if (g_fiber_wrote == 0xC0FFEE) {
        fprintf(out, "result=pass\nfiber_wrote=0x%X\n", g_fiber_wrote);
    } else {
        fprintf(out, "result=fail\nfiber_wrote=0x%X\n", g_fiber_wrote);
    }

    DeleteFiber(child);
    fclose(out);
    return 0;
}

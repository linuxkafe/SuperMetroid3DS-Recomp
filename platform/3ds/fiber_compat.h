/*
 * fiber_compat.h — ARM11 fiber API for 3DS cooperative scheduler (T027)
 *
 * This implements the Win32-like Fiber API used by the core's mmx_rtl.c
 * scheduler. On 3DS ARM11, we implement context switching using pthread
 * condition variables (similar to the Android backend).
 */
#pragma once

#include <stddef.h>   /* size_t */

typedef void (*FiberProc)(void *);

/* Promote the current thread to a fiber; returns its handle (or NULL). */
void *ConvertThreadToFiber(void *param);

/* Create a fiber with its own stack. Returns a handle (or NULL on failure). */
void *CreateFiber(size_t stack_size, FiberProc entry, void *param);

/* Cooperatively switch execution to the given fiber. */
void  SwitchToFiber(void *fiber);

/* Free a fiber created by CreateFiber. */
void  DeleteFiber(void *fiber);

/* Win32 parity for the scheduler's error logging; always 0 on 3DS. */
unsigned long GetLastError(void);

/* Snapshot API (not supported on 3DS fibers). */
int    FiberSnapshotSupported(void);
size_t FiberSnapshotBound(void *fiber);
size_t FiberSnapshotSave(void *fiber, void *out, size_t capacity);
int    FiberSnapshotLoad(void *fiber, const void *in, size_t size);
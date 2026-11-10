/*
 * fiber_compat.c — ARM11 fiber implementation for 3DS (T027)
 *
 * Implements cooperative fiber switching using pthread condition variables.
 * This approach is used in the Android backend of the upstream fiber_compat.c
 * and adapted for the 3DS ARM11 architecture.
 */
#include "fiber_compat.h"
#include <3ds.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

#define MIN_STACK_SIZE (256 * 1024)

typedef struct Fiber {
    pthread_t thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int scheduled;
    FiberProc entry;
    void *param;
    int started;
    int stack_size;
} Fiber;

static pthread_mutex_t g_fiber_lock = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
static Fiber *g_current_fiber = NULL;

/* Thread entry function for fibers created with CreateFiber */
static void *fiber_thread_main(void *arg) {
    Fiber *self = (Fiber *)arg;
    
    pthread_mutex_lock(&g_fiber_lock);
    while (!self->scheduled) {
        pthread_cond_wait(&self->cond, &g_fiber_lock);
    }
    pthread_mutex_unlock(&g_fiber_lock);
    
    /* Call the user's entry function */
    if (self->entry) {
        self->entry(self->param);
    }
    
    /* Fiber entries should never return - they should always yield back.
     * If we get here, there's a logic error in the scheduler. */
    abort();
    
    return NULL;
}

void *ConvertThreadToFiber(void *param) {
    (void)param;
    
    Fiber *f = (Fiber *)calloc(1, sizeof(Fiber));
    if (!f) return NULL;
    
    pthread_mutex_init(&f->lock, NULL);
    pthread_cond_init(&f->cond, NULL);
    
    f->scheduled = 1;  /* Mark as running */
    f->started = 1;    /* No thread needed, uses current thread */
    f->entry = NULL;
    f->param = NULL;
    
    g_current_fiber = f;
    return f;
}

void *CreateFiber(size_t stack_size, FiberProc entry, void *param) {
    if (stack_size < MIN_STACK_SIZE) stack_size = MIN_STACK_SIZE;
    
    Fiber *f = (Fiber *)calloc(1, sizeof(Fiber));
    if (!f) return NULL;
    
    if (pthread_mutex_init(&f->lock, NULL) != 0) {
        free(f);
        return NULL;
    }
    
    if (pthread_cond_init(&f->cond, NULL) != 0) {
        pthread_mutex_destroy(&f->lock);
        free(f);
        return NULL;
    }
    
    f->entry = entry;
    f->param = param;
    f->started = 0;      /* Thread not created yet */
    f->scheduled = 0;
    f->stack_size = stack_size;
    
    return f;
}

void SwitchToFiber(void *fiber) {
    if (!fiber) return;
    
    Fiber *target = (Fiber *)fiber;
    if (!target) return;
    
    /* Self-switch: no-op */
    if (target == g_current_fiber) return;
    
    Fiber *self = g_current_fiber;
    
    pthread_mutex_lock(&g_fiber_lock);
    
    if (!target->started) {
        /* Create the thread for the first time */
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, target->stack_size);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
        
        if (pthread_create(&target->thread, &attr, fiber_thread_main, target) != 0) {
            pthread_attr_destroy(&attr);
            pthread_mutex_unlock(&g_fiber_lock);
            return;
        }
        
        pthread_attr_destroy(&attr);
        target->started = 1;
    }
    
    /* Schedule target fiber */
    target->scheduled = 1;
    pthread_cond_signal(&target->cond);
    
    /* Update running fiber */
    g_current_fiber = target;
    
    /* Wait for target to schedule us */
    while (!self->scheduled) {
        pthread_cond_wait(&self->cond, &g_fiber_lock);
    }
    
    self->scheduled = 0;
    
    pthread_mutex_unlock(&g_fiber_lock);
}

void DeleteFiber(void *fiber) {
    if (!fiber) return;
    
    Fiber *f = (Fiber *)fiber;
    
    if (f->started) {
        /* If thread was created, we can't easily cancel it */
        /* Leak the thread (it will exit after calling entry) */
    }
    
    pthread_cond_destroy(&f->cond);
    pthread_mutex_destroy(&f->lock);
    free(f);
}

unsigned long GetLastError(void) {
    return 0;
}

/* Snapshot API - not supported on pthread-based implementation */
int FiberSnapshotSupported(void) {
    return 0;
}

size_t FiberSnapshotBound(void *fiber_ptr) {
    (void)fiber_ptr;
    return 0;
}

size_t FiberSnapshotSave(void *fiber_ptr, void *out, size_t capacity) {
    (void)fiber_ptr;
    (void)out;
    (void)capacity;
    return 0;
}

int FiberSnapshotLoad(void *fiber_ptr, const void *in, size_t size) {
    (void)fiber_ptr;
    (void)in;
    (void)size;
    return 0;
}
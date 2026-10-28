/* Worker threads for the GPU's upscaled drawing (gpu.c): thread start, one lock with two wake-up
 * conditions, the processor count. Win32 on Windows (no pthread DLL to ship), pthreads elsewhere. */
#include "workers.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static SRWLOCK lock = SRWLOCK_INIT;
static CONDITION_VARIABLE cond[2] = { CONDITION_VARIABLE_INIT, CONDITION_VARIABLE_INIT };

typedef struct { int (*fn)(void *); void *arg; } Start;

static DWORD WINAPI start(LPVOID p)
{
    Start s = *(Start *)p;
    HeapFree(GetProcessHeap(), 0, p);
    return (DWORD)s.fn(s.arg);
}

int wk_thread(int (*fn)(void *), void *arg)
{
    Start *s = HeapAlloc(GetProcessHeap(), 0, sizeof *s);
    HANDLE h;
    if (!s)
        return 0;
    s->fn = fn;
    s->arg = arg;
    h = CreateThread(NULL, 0, start, s, 0, NULL);
    if (!h) {
        HeapFree(GetProcessHeap(), 0, s);
        return 0;
    }
    CloseHandle(h);
    return 1;
}

void wk_lock(void) { AcquireSRWLockExclusive(&lock); }
void wk_unlock(void) { ReleaseSRWLockExclusive(&lock); }
void wk_wait(int c) { SleepConditionVariableSRW(&cond[c], &lock, INFINITE, 0); }
void wk_wake(int c) { WakeAllConditionVariable(&cond[c]); }

int wk_cpu_count(void)
{
    DWORD n = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    return n ? (int)n : 1;
}
#else
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond[2] = { PTHREAD_COND_INITIALIZER, PTHREAD_COND_INITIALIZER };

typedef struct { int (*fn)(void *); void *arg; } Start;

static void *start(void *p)
{
    Start s = *(Start *)p;
    free(p);
    s.fn(s.arg);
    return NULL;
}

int wk_thread(int (*fn)(void *), void *arg)
{
    Start *s = malloc(sizeof *s);
    pthread_t t;
    if (!s)
        return 0;
    s->fn = fn;
    s->arg = arg;
    if (pthread_create(&t, NULL, start, s)) {
        free(s);
        return 0;
    }
    pthread_detach(t);
    return 1;
}

void wk_lock(void) { pthread_mutex_lock(&lock); }
void wk_unlock(void) { pthread_mutex_unlock(&lock); }
void wk_wait(int c) { pthread_cond_wait(&cond[c], &lock); }
void wk_wake(int c) { pthread_cond_broadcast(&cond[c]); }

int wk_cpu_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}
#endif

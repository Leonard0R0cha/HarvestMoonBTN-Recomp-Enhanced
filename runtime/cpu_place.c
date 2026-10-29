/* Where the game's thread and the drawing workers run (Windows).
 * Measured on a hybrid CPU (8 performance + 8 efficiency cores) with the Balanced power plan:
 * Windows parks all performance cores but one, so the game's thread and the workers took turns
 * on one or two cores and each in-between picture cost the game's thread the time of drawing it.
 * Each in-between worker now gets a performance core of its own (the last ones), then each band
 * worker drawing the upscaled 3D (gpu.c) when enough are left (sharing the game's cores, they
 * were crowded onto the few Windows kept awake for seconds at a time, drawing at half speed),
 * and the game's thread the other performance cores; hard affinity, which Windows honours on
 * parked cores too (it wakes them). Nothing changes on older Windows, with too few performance
 * cores, or elsewhere. */
#include "psx.h"
#include "gpu.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdlib.h>

typedef BOOL (WINAPI *GetCpuSetsFn)(PSYSTEM_CPU_SET_INFORMATION, ULONG, PULONG, HANDLE, ULONG);

/* which: 0 = the game's thread, 1 .. INTERP_WORKERS = the in-between workers, INTERP_WORKERS + 1 + k
 * = band worker k of nbands */
void place_thread(int which, int nbands)
{
    GetCpuSetsFn get = (GetCpuSetsFn)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetSystemCpuSetInformation");
    ULONG len = 0;
    unsigned cores[64], ncores = 0, i, k, own;
    unsigned long long mask = 0;
    BYTE best = 0;
    uint8_t *buf, *p;
    if (!get)
        return;
    get(NULL, 0, &len, GetCurrentProcess(), 0);
    if (!len || !(buf = malloc(len)))
        return;
    if (!get((PSYSTEM_CPU_SET_INFORMATION)buf, len, &len, GetCurrentProcess(), 0)) {
        free(buf);
        return;
    }
#define EACH_CPU for (p = buf; p < buf + len; p += ((PSYSTEM_CPU_SET_INFORMATION)p)->Size) \
    if (((PSYSTEM_CPU_SET_INFORMATION)p)->Type == CpuSetInformation && \
        ((PSYSTEM_CPU_SET_INFORMATION)p)->CpuSet.Group == 0 && ((PSYSTEM_CPU_SET_INFORMATION)p)->CpuSet.LogicalProcessorIndex < 64)
#define CPU ((PSYSTEM_CPU_SET_INFORMATION)p)->CpuSet
    EACH_CPU if (CPU.EfficiencyClass > best) best = CPU.EfficiencyClass;     /* performance cores */
    EACH_CPU if (CPU.EfficiencyClass == best && ncores < 64) {
        for (i = 0; i < ncores && cores[i] != CPU.CoreIndex; i++)
            ;
        if (i == ncores)
            cores[ncores++] = CPU.CoreIndex;
    }
    if (ncores >= INTERP_WORKERS + 2) {     /* worker k: core ncores - k; the game's thread: the rest */
        own = ncores >= INTERP_WORKERS + 1 + (unsigned)nbands ? (unsigned)nbands : 0;   /* band workers with a core */
        if ((unsigned)which > INTERP_WORKERS + own)
            which = 0;                      /* a band worker on the game's cores */
        EACH_CPU if (CPU.EfficiencyClass == best) {
            for (k = 1; k <= INTERP_WORKERS + own && cores[ncores - k] != CPU.CoreIndex; k++)
                ;
            if (which ? k == (unsigned)which : k > INTERP_WORKERS + own)
                mask |= 1ull << CPU.LogicalProcessorIndex;
        }
        if (mask)
            SetThreadAffinityMask(GetCurrentThread(), (DWORD_PTR)mask);
    }
    free(buf);
}
#else
void place_thread(int which, int nbands)
{
    (void)which;
    (void)nbands;
}
#endif

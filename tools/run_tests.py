#!/usr/bin/env python3
"""Build and run the runtime unit tests (GTE, GPU, SPU, memory-card file system).
They test the hardware/BIOS reimplementation only and need no game files.

usage: python tools/run_tests.py
"""
import glob, os, platform, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RT = os.path.join(ROOT, 'runtime')
OUT = os.path.join(ROOT, 'build', 'tests')

if platform.system() == 'Windows':
    CC = [os.path.join(ROOT, 'deps', 'llvm-mingw', 'bin', 'clang.exe')]
    EXE = '.exe'
else:
    CC = os.environ.get('CC', 'cc').split()
    EXE = ''

# test -> runtime sources it needs (None = the whole runtime except main + frontend)
TESTS = {
    'gte_test': ['gte.c'],
    'gpu_test': ['gpu.c', 'gpu_replay.c', 'gpu_replay2.c', 'gpu_replay3.c',
                 'gpu_band1.c', 'gpu_band2.c', 'gpu_band3.c', 'gpu_band4.c', 'gpu_view2.c',
                 'gpu_view2_band1.c', 'gpu_view2_band2.c', 'gpu_view2_band3.c', 'gpu_view2_band4.c',
                 'gpu_view2_replay1.c', 'gpu_view2_replay2.c', 'gpu_view2_replay3.c', 'workers.c', 'cpu_place.c',
                 'texpack.c', 'cdpak.c'],
    'spu_test': ['spu.c'],
    'memcard_fs_test': None,
}


def sdl_dyn_missing():
    """SDL functions platform_sdl.c calls that runtime/sdl_dyn.h doesn't load: on Linux, where SDL2 is
    loaded with dlopen, each one is an undefined reference when the game is linked"""
    import re
    dyn = open(os.path.join(RT, 'sdl_dyn.h'), encoding='utf-8').read()
    loaded = set(re.findall(r'X\((SDL_\w+)\)', dyn)) & set(re.findall(r'#define (SDL_\w+) dyn_\1\b', dyn))
    called = set(re.findall(r'\b(SDL_\w+)\s*\(', open(os.path.join(RT, 'platform_sdl.c'), encoding='utf-8').read()))
    platform_only = {'SDL_GetWindowWMInfo'}    # in the Windows-only Vulkan code (SDL_syswm.h)
    return sorted(f for f in called - loaded - platform_only if not f[4:].isupper() and f != 'SDL_zero')   # macros


def main():
    os.makedirs(OUT, exist_ok=True)
    everything = [f for f in sorted(glob.glob(os.path.join(RT, '*.c')))
                  if os.path.basename(f) not in ('main.c', 'platform_sdl.c', 'netplay.c')]
    failed = []
    missing = sdl_dyn_missing()
    print('%-16s %s  %s' % ('sdl_dyn', 'FAIL' if missing else 'ok  ',
                            'not in runtime/sdl_dyn.h: ' + ', '.join(missing) if missing else 'every SDL call is loaded'))
    if missing:
        failed.append('sdl_dyn')
    for name, deps in TESTS.items():
        srcs = [os.path.join(RT, 'tests', name + '.c')]
        srcs += everything if deps is None else [os.path.join(RT, d) for d in deps]
        exe = os.path.join(OUT, name + EXE)
        cmd = CC + ['-std=gnu11', '-O2', '-w', '-I' + RT] + srcs + ['-o', exe]
        if platform.system() != 'Windows':
            cmd += ['-lm', '-lpthread']
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            print(r.stdout + r.stderr)
            failed.append(name + ' (build)')
            continue
        r = subprocess.run([exe] + ([OUT] if name == 'spu_test' else []), cwd=OUT, capture_output=True, text=True)
        last = (r.stdout.strip().splitlines() or [''])[-1]
        print('%-16s %s  %s' % (name, 'ok  ' if r.returncode == 0 else 'FAIL', last))
        if r.returncode:
            failed.append(name)
    if failed:
        sys.exit('failed: ' + ', '.join(failed))


if __name__ == '__main__':
    main()

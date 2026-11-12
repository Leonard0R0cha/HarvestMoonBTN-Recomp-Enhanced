#!/usr/bin/env python3
"""Compile the runtime + the recompiled game code (generated/) into the native executable.
Normally run through ../build.py, which first creates generated/ from your disc.

Windows: uses the llvm-mingw toolchain + SDL2 from deps/ (tools/fetch_deps.py downloads them);
         --linux cross-builds the Linux version with Zig (fetch_deps.py --linux).
Linux:   uses Zig and the SDL2 headers from deps/ when there (fetch_deps.py, as the launcher does),
         else the system C compiler ($CC, default cc; $CXX, default c++) and the SDL2 headers
         (sdl2-config or pkg-config sdl2). SDL2 itself is loaded at runtime.

usage: compile.py [--release] [--linux] [--clean] [--jobs N]
"""
import os, sys, subprocess, shutil, platform, hashlib
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEPS = os.path.join(ROOT, 'deps')
IS_WIN = platform.system() == 'Windows'


def need(path, what, linux=False):
    if not os.path.exists(path):
        sys.exit('%s not found (%s)\nrun:  python tools/fetch_deps.py%s' % (what, path, ' --linux' if linux else ''))
    return path


# no console window for each compiler run when started from a window (the launcher's build page)
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)


def sh(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True, creationflags=NO_WINDOW)
    return r.returncode, r.stdout + r.stderr


def output_dir(release, linux_cross):
    return os.path.join(ROOT, 'build', ('linux-' if linux_cross else '') + ('release' if release else 'debug'))


def build(release=False, linux_cross=False, clean=False, jobs=None):
    """compile + link; returns the path of the executable"""
    linux_cross = linux_cross and IS_WIN
    jobs = jobs or os.cpu_count() or 4
    if not os.path.exists(os.path.join(ROOT, 'generated', 'funcs.h')):
        sys.exit('generated/ is missing - run build.py, which recompiles the game code from your disc first')
    outdir = output_dir(release, linux_cross)
    objdir = os.path.join(outdir, 'obj')
    if clean and os.path.exists(outdir):
        shutil.rmtree(outdir)
    os.makedirs(objdir, exist_ok=True)

    zig = os.path.join(DEPS, 'zig', 'zig.exe' if IS_WIN else 'zig')
    use_zig = linux_cross or (not IS_WIN and os.path.exists(zig))
    if use_zig:
        # zig: cross-compiling from Windows, or on Linux without a compiler of its own (fetch_deps.py);
        # glibc 2.17 baseline, SDL2 resolved at runtime
        cc = [need(zig, 'Zig', True), 'cc']
        target = ['-target', 'x86_64-linux-gnu.2.17']
        sdl_cflags = ['-I' + need(os.path.join(DEPS, 'SDL2-src', 'include'), 'SDL2 headers', True), '-D_REENTRANT']
        sdl_libs = []
        exe = os.path.join(outdir, 'HarvestMoonBTN')
    elif IS_WIN:
        cc = need(os.path.join(DEPS, 'llvm-mingw', 'bin', 'clang.exe'), 'llvm-mingw')
        sdl = need(os.path.join(DEPS, 'SDL2-2.32.10', 'x86_64-w64-mingw32'), 'SDL2 for MinGW')
        sdl_cflags = ['-I' + os.path.join(sdl, 'include', 'SDL2')]
        sdl_libs = ['-L' + os.path.join(sdl, 'lib'), '-lSDL2']
        exe = os.path.join(outdir, 'HarvestMoonBTN.exe')
        target = ['--target=x86_64-w64-mingw32']
        cc = [cc]
    else:
        cc = os.environ.get('CC', 'cc')
        try:
            sdl_cflags = subprocess.check_output(['sdl2-config', '--cflags'], text=True).split()
            sdl_libs = subprocess.check_output(['sdl2-config', '--libs'], text=True).split()
        except Exception:
            sdl_cflags = subprocess.check_output(['pkg-config', '--cflags', 'sdl2'], text=True).split()
        sdl_libs = ['-ldl']   # SDL2 is loaded at runtime (runtime/sdl_dyn.h)
        exe = os.path.join(outdir, 'HarvestMoonBTN')
        target = []
        cc = cc.split()

    common = target + ['-std=gnu11', '-fno-strict-aliasing', '-fwrapv', '-I' + os.path.join(ROOT, 'runtime'),
                       '-I' + os.path.join(ROOT, 'generated')]
    opt_gen = ['-O2'] if release else ['-O1']
    opt_rt = ['-O2', '-g'] if release else ['-O1', '-g']
    warn_rt = ['-Wall', '-Wextra', '-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-missing-field-initializers']
    warn_gen = ['-w']

    srcs = []
    rt = os.path.join(ROOT, 'runtime')
    # online co-op only connects copies built from the same sources (the same on any PC, unlike the .exe)
    h = hashlib.sha1()
    for f in sorted(os.listdir(rt)):
        if f.endswith(('.c', '.h')):
            h.update(open(os.path.join(rt, f), 'rb').read().replace(b'\r\n', b'\n'))
    stamp_gen = os.path.join(ROOT, 'generated', '.stamp')
    if os.path.exists(stamp_gen):
        h.update(open(stamp_gen, 'rb').read())
    build_id = h.hexdigest()[:8]
    for f in sorted(os.listdir(rt)):
        if f.endswith('.c'):
            flags = common + opt_rt + warn_rt
            if f == 'platform_sdl.c':
                flags = flags + sdl_cflags
            if f == 'netplay.c':
                flags = flags + ['-DHMBTN_BUILD_ID=0x' + build_id]
            srcs.append((os.path.join(rt, f), flags))
    # experiment: parallel-psx (runtime/vk, C++; Vulkan is loaded at runtime) behind "renderer = vulkan"
    vk = os.path.join(rt, 'vk')
    cxx = None
    if os.path.isdir(vk):
        pp = os.path.join(vk, 'ppsx')
        inc = ['-I' + os.path.join(vk, 'compat')] + ['-I' + os.path.join(pp, *d) for d in (
            ('renderer',), ('glsl', 'prebuilt'), ('volk',), ('khronos', 'include'), ('SPIRV-Cross',))]
        if use_zig:
            cxx = [cc[0], 'c++']
        elif IS_WIN:
            cxx = [cc[0].replace('clang.exe', 'clang++.exe')]
        else:
            cxx = os.environ.get('CXX', 'c++').split()
        cxxflags = target + ['-std=gnu++14', '-O2', '-DNDEBUG', '-DSPIRV_CROSS_EXCEPTIONS_TO_ASSERTIONS'] + inc
        srcs = [(s, f + ['-DHMBTN_VULKAN']) for s, f in srcs]
        srcs.append((os.path.join(vk, 'vk_renderer.cpp'), cxxflags + ['-Wall']))
        srcs.append((os.path.join(pp, 'renderer', 'renderer.cpp'), cxxflags + ['-w']))
        for f in ('spirv_cfg', 'spirv_cross', 'spirv_cross_parsed_ir', 'spirv_parser'):
            srcs.append((os.path.join(pp, 'SPIRV-Cross', f + '.cpp'), cxxflags + ['-w']))
        srcs.append((os.path.join(pp, 'volk', 'volk.c'), target + ['-O2', '-w'] + inc))
    gen = os.path.join(ROOT, 'generated')
    for f in sorted(os.listdir(gen)):
        if f.endswith('.c'):
            srcs.append((os.path.join(gen, f), common + opt_gen + warn_gen))

    def compile_one(item):
        src, flags = item
        obj = os.path.join(objdir, os.path.splitext(os.path.basename(src))[0] + '.o')
        # rebuild if source, header set or flags changed
        key = hashlib.sha1((' '.join(flags)).encode()).hexdigest()[:8]
        stamp = obj + '.' + key
        hdr_t = max(os.path.getmtime(os.path.join(d, h)) for d in (rt, vk) if os.path.isdir(d)
                    for h in os.listdir(d) if h.endswith(('.h', '.inc')))
        if os.path.exists(obj) and os.path.exists(stamp):
            t = os.path.getmtime(obj)
            src_t = os.path.getmtime(src)
            if os.path.basename(src).startswith(('gpu_replay', 'gpu_band', 'gpu_view2')):    # include gpu.c
                src_t = max(src_t, os.path.getmtime(os.path.join(rt, 'gpu.c')))
            if t >= src_t and t >= hdr_t:
                return obj, 0, ''
        code, out = sh((cxx if src.endswith('.cpp') else cc) + flags + ['-c', src, '-o', obj])
        if code == 0:
            for old in os.listdir(objdir):
                if old.startswith(os.path.basename(obj) + '.'):
                    os.remove(os.path.join(objdir, old))
            open(stamp, 'w').close()
        return obj, code, out

    objs = []
    failed = False
    with ThreadPoolExecutor(jobs) as ex:
        for obj, code, out in ex.map(compile_one, srcs):
            objs.append(obj)
            if out.strip():
                print(out.strip())
            if code:
                failed = True
    if failed:
        sys.exit('BUILD FAILED')
    if IS_WIN and not linux_cross:      # the program icon (runtime/icon.rc)
        res = os.path.join(objdir, 'icon_rc.o')
        code, out = sh([os.path.join(DEPS, 'llvm-mingw', 'bin', 'x86_64-w64-mingw32-windres.exe'),
                        os.path.join(rt, 'icon.rc'), '-o', res])
        if code:
            sys.exit('ICON FAILED\n' + out)
        objs.append(res)
    # Windows: libstdc++ in the .exe; Linux: the system's (Zig links its libc++ in)
    link = (cxx + (['-static-libstdc++'] if IS_WIN and not linux_cross else []) if cxx else cc) + target + objs + sdl_libs + ['-o', exe]
    if use_zig:
        link += ['-lm', '-ldl', '-lpthread', '-Wl,-z,stack-size=16777216', '-s']
    elif IS_WIN:
        link += ['-static-libgcc', '-Wl,--stack,16777216']
        link += ['-mwindows'] if release else ['-mconsole']
        link += ['-lwinmm', '-lws2_32']
    else:
        link += ['-lm', '-Wl,-z,stacksize=16777216']
    code, out = sh(link)
    if out.strip():
        print(out.strip())
    if code:
        sys.exit('LINK FAILED')
    if IS_WIN and not linux_cross:
        dll = os.path.join(DEPS, 'SDL2-2.32.10', 'x86_64-w64-mingw32', 'bin', 'SDL2.dll')
        dst = os.path.join(outdir, 'SDL2.dll')
        if not (os.path.exists(dst) and os.path.getsize(dst) == os.path.getsize(dll)):
            shutil.copy(dll, dst)
    print('built', exe)
    return exe


if __name__ == '__main__':
    jobs = int(sys.argv[sys.argv.index('--jobs') + 1]) if '--jobs' in sys.argv else None
    build('--release' in sys.argv, '--linux' in sys.argv, '--clean' in sys.argv, jobs)

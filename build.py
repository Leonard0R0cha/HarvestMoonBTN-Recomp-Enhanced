#!/usr/bin/env python3
"""Build the native port from YOUR OWN copy of Harvest Moon: Back to Nature (USA, SLUS-01115).

  1. reads the game executable and its code overlays from your disc image -> extract/
  2. statically recompiles the PlayStation (MIPS) code to C               -> generated/
  3. compiles the runtime + the generated code into a native program      -> build/
  4. puts a ready-to-play folder together                                  -> dist/

usage: python build.py [--disc PATH] [--lang pt] [--pak] [--zip] [--into DIR] [--linux] [--debug] [--jobs N] [--clean]

  --disc PATH  your disc image (.cue or .bin); default: the first .cue (or .bin) in disc/
  --lang pt    the game's text in Brazilian Portuguese: downloads a fan translation's patch, applies it to
               your disc image (the result goes into disc-pt/ beside the game) and builds from that
  --pak        also turn your disc image into the compact data pack HarvestMoonBTN.pak and put it
               in the dist folder, so the game runs without the disc image
  --zip        also create a .zip of the dist folder
  --into DIR   put the game into DIR (a folder you play from) instead of dist/; nothing there is
               deleted (the launcher's "Build from my disc" page uses it)
  --linux      on Windows: cross-build the Linux version (run "python tools/fetch_deps.py --linux")
  --debug      unoptimised build with a console window, for development
  --clean      rebuild everything from scratch

Windows needs the toolchain in deps/ first:  python tools/fetch_deps.py
Linux needs a C compiler and the SDL2 development package (e.g. apt install build-essential libsdl2-dev).
"""
import glob, hashlib, os, platform, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import compile as native            # noqa: E402
from iso import Disc, find_file, resolve_bin   # noqa: E402
from package import package         # noqa: E402

# the files the recompiled code is generated from, and their SHA-256 for the USA release
GAME_FILES = {
    'SLUS_011.15': '34e3b35dcd3fb1cd9a7d4813b8a9ef987915826e3d436f1908ecbe2d4fdf1444',
    'A_FILE.HDT': '0cae44d6bdb29e078a182d7cd9a780c21e35dc54f55eb94c2d9ea8deb20c82dd',
    'A_FILE.BIN': '612f9ac8279a364c2fdd19cb75e63df2e892a6f52b6f1d8081ea8560b18decd4',
}
# inputs of the recompiler; generated/ is redone when any of them changes
RECOMP_INPUTS = ['tools/recomp.py', 'tools/mips.py', 'tools/overlays.json', 'tools/extra_entries.json']

# --lang: fan translations made for the USA disc. They are not in this repository: the build downloads the
# patch from its author's release, applies it to your disc image (tools/vcdiff.py) and recompiles the game
# from the translated image, because a translation also changes some of the game's code. The translated
# image goes into disc-<lang>/ next to the game, which the game built this way reads.
TRANSLATIONS = {
    'pt': {
        'name': 'Brazilian Portuguese, fan translation by GabriPires (github.com/GabriPires/traducao-hm-btn-ptbr)',
        'url': 'https://github.com/GabriPires/traducao-hm-btn-ptbr/releases/download/v0.1.5/'
               'HarvestMoon-BTN-PTBR-v0.1.5.zip',
        'sha256': '90f158d65f53ff8cb5988faea307957d22e3e6b5b434bbb1ac0f068f7e6eab5b',
        'patch': 'HarvestMoon-BTN-PTBR-v0.1.5.xdelta',
        'image': 'HarvestMoon-BTN-PTBR',
        'image_sha256': '7f704d0810025fe2a8808cf26912c4dc0a1b7bf98e096d609ec8d91b7dfe7ee7',
        'files': {
            'SLUS_011.15': 'b75abdb32cb5699d8c4cae100a3078d11b6e9b15219a82f8606b12f9e86b8a24',
            'A_FILE.HDT': '0cae44d6bdb29e078a182d7cd9a780c21e35dc54f55eb94c2d9ea8deb20c82dd',
            'A_FILE.BIN': '968ba9d3849529c035563c654ffa613a7f6febeb8948fbbcbf7553cee06e00d1',
        },
    },
}


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def extracted(out=os.path.join(ROOT, 'extract'), files=GAME_FILES):
    """the game files are in extract/ (or out) already, verified"""
    return all(os.path.exists(os.path.join(out, n)) and sha256_file(os.path.join(out, n)) == h
               for n, h in files.items())


def find_disc(arg, need=True):
    if arg:
        if not os.path.isfile(arg):
            sys.exit('disc image not found: ' + arg)
        return os.path.abspath(arg)
    for ext in ('*.cue', '*.bin'):
        hits = sorted(glob.glob(os.path.join(ROOT, 'disc', ext)))
        if hits:
            return hits[0]
    if not need:
        return None                 # a rebuild without the disc image, from extract/
    sys.exit('No disc image found.\n'
             'Put your own Harvest Moon: Back to Nature (USA) .cue + .bin into the "disc" folder,\n'
             'or pass it with:  python build.py --disc "path/to/Harvest Moon - Back to Nature.cue"')


def extract(disc_path):
    """copy the game files the recompiler needs out of the disc image and verify them"""
    out = os.path.join(ROOT, 'extract')
    os.makedirs(out, exist_ok=True)
    if extracted():
        return
    binpath = resolve_bin(disc_path)
    if not os.path.isfile(binpath):
        sys.exit('the .bin file named in the .cue sheet is missing: ' + binpath)
    if os.path.getsize(binpath) % 2352:
        sys.exit('%s is not a raw Mode2/2352 image (.bin from a .cue/.bin dump)' % binpath)
    disc = Disc(binpath)
    print('reading game files from', binpath)
    for name, want in GAME_FILES.items():
        e = find_file(disc, name)
        if not e:
            sys.exit('%s is not on this disc - this is not Harvest Moon: Back to Nature (USA)' % name)
        data = disc.read_extent(e['lba'], e['size'])
        if hashlib.sha256(data).hexdigest() != want:
            sys.exit('%s on this disc does not match the USA release (SLUS-01115).\n'
                     'Only the original USA version is supported; check that your dump is good.' % name)
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)


def translated_image(lang, out):
    """the translated disc image in out/disc-<lang>/ (.bin), or None when it is not there yet"""
    t = TRANSLATIONS[lang]
    img = os.path.join(out, 'disc-' + lang, t['image'] + '.bin')
    return img if os.path.isfile(img) and sha256_file(img) == t['image_sha256'] else None


def make_translated_image(lang, disc_path, out):
    """download the translation's patch (kept in build/) and apply it to the disc image -> out/disc-<lang>/"""
    import io, urllib.request, zipfile, vcdiff
    t = TRANSLATIONS[lang]
    cache = os.path.join(ROOT, 'build', 'translations', os.path.basename(t['url']))
    if not (os.path.isfile(cache) and sha256_file(cache) == t['sha256']):
        print('downloading the translation:', t['url'])
        os.makedirs(os.path.dirname(cache), exist_ok=True)
        req = urllib.request.Request(t['url'], headers={'User-Agent': 'HarvestMoonBTN-build'})
        with urllib.request.urlopen(req, timeout=120) as r:
            data = r.read()
        if hashlib.sha256(data).hexdigest() != t['sha256']:
            sys.exit('the translation download is damaged or was changed (checksum); try again later')
        with open(cache, 'wb') as f:
            f.write(data)
    with zipfile.ZipFile(cache) as z:
        patch = z.read(t['patch'])
    src = resolve_bin(disc_path)
    print('applying the translation to', src)
    with open(src, 'rb') as f:
        original = f.read()
    try:
        data = vcdiff.apply(original, patch)
    except ValueError:
        data = None
    if data is None or hashlib.sha256(data).hexdigest() != t['image_sha256']:
        sys.exit('The translation is made for one particular dump of the USA disc: a single-track .bin of\n'
                 '213,954,384 bytes (MD5 424f9fbda58e95f597e39b4bdde239b1). Yours is a different one,\n'
                 'so the patch does not fit it; build in English, or use that dump.')
    d = os.path.join(out, 'disc-' + lang)
    os.makedirs(d, exist_ok=True)
    with open(os.path.join(d, t['image'] + '.bin'), 'wb') as f:
        f.write(data)
    with open(os.path.join(d, t['image'] + '.cue'), 'w') as f:
        f.write('FILE "%s.bin" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n' % t['image'])
    return os.path.join(d, t['image'] + '.bin')


def extract_translated(lang, img):
    """the translated disc's game files -> extract-<lang>/, verified"""
    files = TRANSLATIONS[lang]['files']
    out = os.path.join(ROOT, 'extract-' + lang)
    if extracted(out, files):
        return out
    os.makedirs(out, exist_ok=True)
    disc = Disc(img)
    for name, want in files.items():
        data = disc.read_extent(find_file(disc, name)['lba'], find_file(disc, name)['size'])
        if hashlib.sha256(data).hexdigest() != want:
            sys.exit('%s on the translated image is not what it should be' % name)
        with open(os.path.join(out, name), 'wb') as f:
            f.write(data)
    return out


def recompile(clean, lang=None, extract_dir=None):
    h = hashlib.sha256()
    files = TRANSLATIONS[lang]['files'] if lang else GAME_FILES
    for n in sorted(files):
        h.update(files[n].encode())
    if lang:
        h.update(lang.encode())
    for p in RECOMP_INPUTS:
        # as text: Windows line ends (a copy not checked out with git) must not change the key, which
        # goes into the co-op build id (tools/compile.py)
        h.update(open(os.path.join(ROOT, p), 'rb').read().replace(b'\r\n', b'\n'))
    key = h.hexdigest()
    stamp = os.path.join(ROOT, 'generated', '.stamp')
    if not clean and os.path.exists(stamp) and open(stamp).read() == key:
        return
    print('recompiling the game code (MIPS -> C) ...')
    os.makedirs(os.path.join(ROOT, 'generated'), exist_ok=True)
    os.makedirs(os.path.join(ROOT, 'build'), exist_ok=True)
    for old in glob.glob(os.path.join(ROOT, 'generated', '*')):
        os.remove(old)
    subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'recomp.py')] +
                          (['--extract', extract_dir, '--lang', lang] if lang else []))
    with open(stamp, 'w') as f:
        f.write(key)


def make_pak(disc_path):
    pak = os.path.join(ROOT, 'build', 'HarvestMoonBTN.pak')
    src = resolve_bin(disc_path)
    stamp = pak + '.src'
    key = '%s|%d|%d' % (os.path.abspath(src), os.path.getsize(src), int(os.path.getmtime(src)))
    if not (os.path.exists(pak) and os.path.exists(stamp) and open(stamp).read() == key):
        print('building the data pack from your disc image ...')
        subprocess.check_call([sys.executable, os.path.join(ROOT, 'tools', 'makepak.py'), disc_path, pak])
        with open(stamp, 'w') as f:
            f.write(key)
    return pak


def behind_main():
    """warn when this git checkout lacks commits of main on GitHub: online co-op only connects to a
    game built from the same sources, so a build of an older branch can't play with the others'"""
    git = lambda *a: subprocess.run(['git', '-C', ROOT, *a], capture_output=True, text=True, timeout=30,
                                    creationflags=native.NO_WINDOW)
    try:
        top = git('rev-parse', '--show-toplevel').stdout.strip()
        if not top or not os.path.samefile(top, ROOT):
            return                                  # not a checkout of this repository (e.g. the launcher's
                                                    # build-files sitting inside some other clone's folder)
        remotes = git('remote').stdout.split()
        remote = 'upstream' if 'upstream' in remotes else 'origin' if 'origin' in remotes else None
        if not remote or git('fetch', '--quiet', remote, 'main').returncode:
            return                                  # offline, or no main there: nothing to compare with
        missing = git('log', '--oneline', '--no-merges', 'HEAD..%s/main' % remote).stdout.splitlines()
    except (OSError, subprocess.SubprocessError):
        return                                      # no git
    if missing:
        print('\nWARNING: this checkout lacks %d commit(s) of %s/main:' % (len(missing), remote))
        for line in missing[:8]:
            print('   ', line)
        if len(missing) > 8:
            print('    ...')
        print('Online co-op only connects to a game built from the same sources: a friend who builds main\n'
              'gets another version. To have them: git merge %s/main\n' % remote)


def build_launcher():
    """(re)build dist-template/HarvestMoonBTN-Launcher.exe for the game folder when it is missing or older
    than what it carries (tools/build_launcher.py). Not in git: releases are built on GitHub"""
    if getattr(sys, 'frozen', False):
        return                          # the launcher's own Build page: that launcher is already there
    exe = os.path.join(ROOT, 'dist-template', 'HarvestMoonBTN-Launcher.exe')
    srcs = [os.path.join(ROOT, 'build.py')]
    for top in ('dist-template', 'runtime', 'tools'):
        for d, dirs, names in os.walk(os.path.join(ROOT, top)):
            dirs[:] = [n for n in dirs if n != '__pycache__']
            srcs += [os.path.join(d, n) for n in names if n != os.path.basename(exe)]
    if os.path.exists(exe) and os.path.getmtime(exe) >= max(map(os.path.getmtime, srcs)):
        return
    import importlib.util
    if not importlib.util.find_spec('PyInstaller'):
        print('\nNote: the game folder gets %s launcher exe (launcher.pyw, which runs with Python, is there).\n'
              'To build the exe too: pip install pyinstaller' % ('an older' if os.path.exists(exe) else 'no'))
        return
    print('building the launcher (PyInstaller) ...')
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'build_launcher.py')], capture_output=True,
                       text=True, creationflags=native.NO_WINDOW)
    if r.returncode:
        print('WARNING: the launcher exe could not be built:\n' + (r.stdout + r.stderr)[-1500:])


def main():
    args = sys.argv[1:]
    if '-h' in args or '--help' in args:
        print(__doc__)
        return
    opt = lambda n: n in args
    disc_arg = args[args.index('--disc') + 1] if '--disc' in args else None
    jobs = int(args[args.index('--jobs') + 1]) if '--jobs' in args else None
    linux = opt('--linux') or platform.system() != 'Windows'

    lang = args[args.index('--lang') + 1] if '--lang' in args else 'en'
    if lang != 'en' and lang not in TRANSLATIONS:
        sys.exit('--lang: en%s' % ''.join(', ' + k for k in TRANSLATIONS))
    lang = None if lang == 'en' else lang
    name = 'HarvestMoonBTN-%s-x64%s' % ('Linux' if linux else 'Windows', '-debug' if opt('--debug') else '')
    into = args[args.index('--into') + 1] if '--into' in args else None
    out_dir = os.path.abspath(into) if into else os.path.join(ROOT, 'dist', name)   # where package() puts it

    behind_main()
    img = translated_image(lang, out_dir) if lang else None
    disc_path = find_disc(disc_arg, need=opt('--pak') or not extracted() or bool(lang and not img))
    print('disc image:', disc_path or 'none (the game files from it are in extract/)')
    extract(disc_path)
    extract_dir = None
    if lang:
        print('game text:', TRANSLATIONS[lang]['name'])
        img = img or make_translated_image(lang, disc_path, out_dir)
        extract_dir = extract_translated(lang, img)
    recompile(opt('--clean'), lang, extract_dir)
    exe = native.build(release=not opt('--debug'), linux_cross=opt('--linux'), clean=opt('--clean'), jobs=jobs)
    pak = make_pak(img[:-4] + '.cue' if lang else disc_path) if opt('--pak') else None
    if not linux:
        build_launcher()

    files = [(exe, os.path.basename(exe))]
    if exe.endswith('.exe'):
        files.append((os.path.join(os.path.dirname(exe), 'SDL2.dll'), 'SDL2.dll'))
    # remember the disc image location in hmbtn.ini, unless the build is for another machine (a translated
    # game reads its own image in disc-<lang>/ first)
    out = package(name, files, pak=pak, disc_path=None if opt('--linux') else disc_path, make_zip=opt('--zip'),
                  into=into)
    keep_language(out, exe, lang or 'en')
    print('\nDone. Play:  %s' % os.path.join(out, os.path.basename(exe)))


def source_key():
    """what the game is built from besides the disc: a game of each language made from these is the same version"""
    h = hashlib.sha256()
    paths = [os.path.join(ROOT, 'build.py')]
    for top in ('runtime', 'tools'):
        for d, dirs, names in os.walk(os.path.join(ROOT, top)):
            dirs[:] = sorted(n for n in dirs if n not in ('__pycache__', 'tests'))
            paths += [os.path.join(d, n) for n in sorted(names) if n.endswith(('.c', '.h', '.py', '.json', '.rc'))]
    for p in paths:
        h.update(os.path.relpath(p, ROOT).replace('\\', '/').encode())
        h.update(open(p, 'rb').read().replace(b'\r\n', b'\n'))
    return h.hexdigest()


def keep_language(out, exe, lang):
    """a copy of the game of each language in out/languages/, so the launcher switches between them at once;
    those built from other sources are removed (they would be another version)"""
    d = os.path.join(out, 'languages')
    os.makedirs(d, exist_ok=True)
    key = source_key()
    for n in ['en'] + sorted(TRANSLATIONS):
        stamp = os.path.join(d, n + '.key')
        if n != lang and os.path.exists(stamp) and open(stamp).read().strip() != key:
            for old in glob.glob(os.path.join(d, 'HarvestMoonBTN-%s*' % n)) + [stamp]:
                os.remove(old)
    name, ext = os.path.splitext(os.path.basename(exe))
    shutil.copy2(exe, os.path.join(d, '%s-%s%s' % (name, lang, ext)))
    with open(os.path.join(d, lang + '.key'), 'w') as f:
        f.write(key + '\n')


if __name__ == '__main__':
    main()

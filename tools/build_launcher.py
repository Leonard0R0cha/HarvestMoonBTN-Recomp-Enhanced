#!/usr/bin/env python3
"""Build dist-template/HarvestMoonBTN-Launcher.exe (on Linux: HarvestMoonBTN-Launcher-Linux) from
dist-template/launcher.pyw (PyInstaller; it builds for the system it runs on).

The exe carries the build scripts and the runtime sources (nothing of the game), so its
"Build from my disc" page can make the game anywhere: give a friend the launcher alone and they
build from their own disc. Rebuild it after changing launcher.pyw, the runtime or the tools.

usage: python tools/build_launcher.py      (needs: pip install pyinstaller)
"""
import ast, glob, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TPL = os.path.join(ROOT, 'dist-template')
NAME = 'HarvestMoonBTN-Launcher' if os.name == 'nt' else 'HarvestMoonBTN-Launcher-Linux'


def script_imports():
    """standard-library modules build.py and tools/*.py import: the exe runs those scripts with the
    Python it carries, and PyInstaller only packs what launcher.pyw itself imports, so a module only
    a script uses would be missing for whoever builds from the launcher alone"""
    mods = set()
    for path in [os.path.join(ROOT, 'build.py')] + glob.glob(os.path.join(ROOT, 'tools', '*.py')):
        with open(path, encoding='utf-8') as f:
            tree = ast.parse(f.read(), path)
        for node in ast.walk(tree):
            if isinstance(node, ast.Import):
                mods.update(a.name for a in node.names)
            elif isinstance(node, ast.ImportFrom) and node.module and not node.level:
                mods.add(node.module)
    return sorted(m for m in mods if m.split('.')[0] in sys.stdlib_module_names)


def main():
    work = os.path.join(ROOT, 'build', 'launcher')
    src = os.path.join(work, 'src')
    shutil.rmtree(work, ignore_errors=True)
    skip = shutil.ignore_patterns('__pycache__', 'tests', 'HarvestMoonBTN-Launcher.exe', 'HarvestMoonBTN-Launcher-Linux')
    shutil.copytree(os.path.join(ROOT, 'tools'), os.path.join(src, 'tools'), ignore=skip)
    shutil.copytree(os.path.join(ROOT, 'runtime'), os.path.join(src, 'runtime'), ignore=skip)
    shutil.copytree(TPL, os.path.join(src, 'dist-template'), ignore=skip)
    shutil.copy2(os.path.join(ROOT, 'build.py'), src)
    hidden = [arg for m in script_imports() for arg in ('--hidden-import', m)]
    subprocess.check_call([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--onefile', '--windowed',
                           '--name', NAME, *hidden,
                           '--icon', os.path.join(TPL, 'launcher-assets', 'icon.ico'),
                           '--add-data', src + os.pathsep + 'src', '--distpath', TPL, '--workpath', work,
                           '--specpath', work, os.path.join(TPL, 'launcher.pyw')])
    print('built', os.path.join(TPL, NAME + ('.exe' if os.name == 'nt' else '')))


if __name__ == '__main__':
    main()

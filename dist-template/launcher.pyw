#!/usr/bin/env python3
"""Settings launcher: edits hmbtn.ini next to this file and starts the game.

Needs Python 3 with tkinter (part of the python.org installer). Double-click it, or run
"python launcher.pyw". Only options you change are written; comments in hmbtn.ini stay.
"""
import os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
INI = os.path.join(HERE, 'hmbtn.ini')
EXE = os.path.join(HERE, 'HarvestMoonBTN.exe' if os.name == 'nt' else 'HarvestMoonBTN')

BUTTONS = ['up', 'down', 'left', 'right', 'cross', 'circle', 'square', 'triangle',
           'l1', 'r1', 'l2', 'r2', 'start', 'select']
# (tab, ini section, key, kind, label)   kind: file, dir, spinN (1..N), bool, text, choice:a|b|c
OPTIONS = [
    ('Game', 'general', 'disc', 'file', 'Disc image (.cue/.bin) or data pack (.pak)'),
    ('Game', 'general', 'savedir', 'dir', 'Memory card folder'),
    ('Game', 'behaviour', 'fastforward', 'bool', 'Allow holding Tab to fast-forward'),
    ('Game', 'behaviour', 'fastcd', 'bool', 'Shorter loading pauses'),
    ('Video', 'video', 'scale', 'spin8', 'Window size (multiple of 320x240)'),
    ('Video', 'video', 'upscale', 'spin4', 'Internal resolution (1 = original, 2-4 = smoother 3D)'),
    ('Video', 'video', 'pgxp', 'bool', 'PGXP: precise 3D vertices, no wobble (needs resolution 2+)'),
    ('Video', 'video', 'perspective', 'bool', 'Perspective-correct textures (needs PGXP)'),
    ('Video', 'video', 'sprite_depth', 'bool', 'Scale 2D objects with depth, like the 3D ground (needs PGXP)'),
    ('Video', 'video', 'widescreen', 'choice:off|16:9|16:10|21:9', 'Widescreen 3D view (resolution 2+ keeps 2D objects in shape)'),
    ('Video', 'video', 'fullscreen', 'bool', 'Start in fullscreen (F11 toggles)'),
    ('Video', 'video', 'scaling', 'choice:sharp-bilinear|sharp|smooth', 'Scaling to the screen'),
    ('Video', 'video', 'aspect43', 'bool', '4:3 picture like a CRT TV'),
] + [('Keyboard', 'keyboard', 'key_' + b, 'text', b.upper()) for b in BUTTONS] \
  + [('Controller', 'controller', 'pad_' + b, 'text', b.upper()) for b in BUTTONS] \
  + [('Controller', 'controller', 'stick_as_dpad', 'bool', 'Left stick also works as the D-pad')]
HINTS = {'Keyboard': 'SDL key names, comma-separated (e.g. X,Space,K)',
         'Controller': 'SDL controller buttons: a b x y back start leftshoulder rightshoulder\n'
                       'lefttrigger righttrigger dpup dpdown dpleft dpright leftstick rightstick'}

LINE = re.compile(r'^\s*([#;]?)\s*([A-Za-z][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$')


def read_ini(text):
    """key -> value; active lines win over commented examples ('#key = value')"""
    vals, active = {}, set()
    for line in text.splitlines():
        m = LINE.match(line)
        if not m:
            continue
        hashed, k, v = m.groups()
        if not hashed:
            vals[k] = v
            active.add(k)
        elif k not in active:
            vals[k] = v
    return vals, active


def set_ini(text, section, key, value):
    """set (or with value '' comment out) one option, keeping everything else as it is"""
    lines = text.splitlines()
    for i, line in enumerate(lines):
        m = LINE.match(line)
        if m and m.group(2) == key and not m.group(1):
            lines[i] = '%s = %s' % (key, value) if value else '#' + line.lstrip()
            return '\n'.join(lines) + '\n'
    if not value:
        return text
    for i, line in enumerate(lines):          # above the commented example, if there is one
        m = LINE.match(line)
        if m and m.group(2) == key:
            lines.insert(i, '%s = %s' % (key, value))
            return '\n'.join(lines) + '\n'
    head = '[%s]' % section
    if head not in (l.strip() for l in lines):
        lines += ['', head]
    i = [l.strip() for l in lines].index(head)
    lines.insert(i + 1, '%s = %s' % (key, value))
    return '\n'.join(lines) + '\n'


def selftest():
    t = '[video]\n# 1 = smooth\nsmooth = 0\n[keyboard]\n#key_up = Up\n'
    v, act = read_ini(t)
    assert v == {'smooth': '0', 'key_up': 'Up'} and act == {'smooth'}, v
    assert set_ini(t, 'video', 'smooth', '1') == t.replace('smooth = 0', 'smooth = 1')
    assert set_ini(t, 'keyboard', 'key_up', 'W') == t.replace('#key_up', 'key_up = W\n#key_up')
    assert set_ini(t, 'video', 'smooth', '') == t.replace('smooth = 0', '#smooth = 0')
    assert set_ini(t, 'general', 'disc', 'a.cue').endswith('[general]\ndisc = a.cue\n')
    assert read_ini(set_ini(t, 'keyboard', 'key_up', 'W'))[0]['key_up'] == 'W'
    print('selftest ok')


def main():
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox

    text = open(INI, encoding='utf-8').read() if os.path.exists(INI) else ''
    loaded, active = read_ini(text)
    root = tk.Tk()
    root.title('Harvest Moon: Back to Nature - Settings')
    tabs = ttk.Notebook(root)
    tabs.pack(fill='both', expand=True, padx=8, pady=8)
    frames, rows, vars_ = {}, {}, {}

    for tab, section, key, kind, label in OPTIONS:
        if tab not in frames:
            frames[tab] = ttk.Frame(tabs, padding=10)
            tabs.add(frames[tab], text=tab)
            rows[tab] = 0
            if tab in HINTS:
                ttk.Label(frames[tab], text=HINTS[tab], foreground='gray').grid(
                    row=0, column=0, columnspan=3, sticky='w', pady=(0, 6))
                rows[tab] = 1
        f, r = frames[tab], rows[tab]
        # a path from a commented example ("#disc = C:\Games\...") is not a real setting
        start = loaded.get(key, '') if (key in active or kind not in ('file', 'dir')) else ''
        if kind.startswith('choice:') and not start:
            start = kind[7:].split('|')[0]       # the game's default
        var = vars_[key] = tk.StringVar(value=start)
        if kind == 'bool':
            ttk.Checkbutton(f, text=label, variable=var, onvalue='1', offvalue='0').grid(
                row=r, column=0, columnspan=3, sticky='w', pady=2)
        else:
            ttk.Label(f, text=label).grid(row=r, column=0, sticky='w', padx=(0, 8), pady=2)
            if kind.startswith('spin'):
                ttk.Spinbox(f, from_=1, to=int(kind[4:]), width=5, textvariable=var).grid(row=r, column=1, sticky='w')
            elif kind.startswith('choice:'):
                ttk.Combobox(f, values=kind[7:].split('|'), state='readonly', width=16,
                             textvariable=var).grid(row=r, column=1, sticky='w')
            else:
                ttk.Entry(f, textvariable=var, width=40 if kind != 'text' else 28).grid(row=r, column=1, sticky='we')
            if kind in ('file', 'dir'):
                def browse(var=var, kind=kind):
                    p = (filedialog.askopenfilename(filetypes=[('Game data', '*.cue *.bin *.pak'), ('All files', '*')])
                         if kind == 'file' else filedialog.askdirectory())
                    if p:
                        var.set(os.path.normpath(p))
                ttk.Button(f, text='Browse...', command=browse).grid(row=r, column=2, padx=(6, 0))
        rows[tab] += 1

    def save():
        nonlocal text
        for tab, section, key, kind, label in OPTIONS:
            v = vars_[key].get().strip()
            if kind.startswith('spin') and v.isdigit():     # typed numbers too: keep them in range
                v = str(min(max(int(v), 1), int(kind[4:])))
                vars_[key].set(v)
            if v != (loaded.get(key, '') if (key in active or kind not in ('file', 'dir')) else ''):
                text = set_ini(text, section, key, v)
        with open(INI, 'w', encoding='utf-8') as f:
            f.write(text)
        loaded.update(read_ini(text)[0])
        active.update(read_ini(text)[1])

    def play():
        save()
        if not os.path.exists(EXE):
            messagebox.showerror('Not found', EXE + ' is missing.')
            return
        subprocess.Popen([EXE], cwd=HERE)
        root.destroy()

    bar = ttk.Frame(root, padding=(8, 0, 8, 8))
    bar.pack(fill='x')
    ttk.Button(bar, text='Save && Play', command=play).pack(side='right')
    ttk.Button(bar, text='Save', command=save).pack(side='right', padx=6)
    ttk.Button(bar, text='Close', command=root.destroy).pack(side='left')
    root.mainloop()


if __name__ == '__main__':
    selftest() if '--selftest' in sys.argv else main()

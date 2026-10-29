#!/usr/bin/env python3
"""Settings launcher: edits hmbtn.ini next to this file and starts the game.

Needs Python 3 with tkinter (part of the python.org installer). Double-click it, or run
"python launcher.pyw". Only options you change are written; comments in hmbtn.ini stay.
English or Portuguese, as the system (PT / EN switches).
"""
import os, re, struct, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
INI = os.path.join(HERE, 'hmbtn.ini')
EXE = os.path.join(HERE, 'HarvestMoonBTN.exe' if os.name == 'nt' else 'HarvestMoonBTN')

BUTTONS = ['up', 'down', 'left', 'right', 'cross', 'circle', 'square', 'triangle',
           'l1', 'r1', 'l2', 'r2', 'start', 'select']
# (page, ini section, key, kind)   kind: file, dir, spinN (1..N), bool, text, choice:a|b|c,
# edit:a|b|c (a choice that can also be typed)
OPTIONS = [
    ('video', 'video', 'upscale', 'choice:1|2|3|4'),
    ('video', 'video', 'widescreen', 'choice:off|16:9|16:10|21:9'),
    ('video', 'video', 'pgxp', 'bool'),
    ('video', 'video', 'perspective', 'bool'),
    ('video', 'video', 'sprite_depth', 'bool'),
    ('video', 'video', 'interp', 'bool'),
    ('video', 'video', 'texturepack', 'bool'),
    ('video', 'video', 'dumptextures', 'bool'),
    ('video', 'video', 'drawthreads', 'choice:auto|0|1|2|3|4'),
    ('video', 'video', 'scale', 'spin8'),
    ('video', 'video', 'fullscreen', 'bool'),
    ('video', 'video', 'scaling', 'choice:sharp-bilinear|sharp|smooth'),
    ('video', 'video', 'aspect43', 'bool'),
    ('game', 'general', 'disc', 'file'),
    ('game', 'general', 'savedir', 'dir'),
    ('game', 'behaviour', 'autoslot', 'bool'),
    ('game', 'behaviour', 'fastcard', 'bool'),
    ('game', 'behaviour', 'quickui', 'bool'),
    ('game', 'behaviour', 'fastload', 'bool'),
    ('game', 'behaviour', 'fastcd', 'choice:0|1|8|16'),
    ('game', 'behaviour', 'textspeed', 'spin16'),
    ('game', 'behaviour', 'skipscenes', 'bool'),
    ('game', 'behaviour', 'fastforward', 'bool'),
    ('game', 'behaviour', 'overclock', 'edit:100|150|200|300'),
    ('game', 'behaviour', 'emptybag', 'bool'),
    ('game', 'behaviour', 'record', 'bool'),
] + [('keyboard', 'keyboard', 'key_' + b, 'text') for b in BUTTONS] \
  + [('controller', 'controller', 'pad_' + b, 'text') for b in BUTTONS] \
  + [('controller', 'controller', 'stick_as_dpad', 'bool')]

# Video presets by processor: the picture is drawn by the processor (no graphics card needed), and
# the internal resolution is what costs. Every effect stays on; only the resolution changes.
PRESETS = [
    ('preset2', {'upscale': '2'}),
    ('preset4', {'upscale': '3'}),
    ('preset6', {'upscale': '4'}),
]
PRESET_ALL = {'pgxp': '1', 'perspective': '1', 'sprite_depth': '1', 'interp': '1'}

# option -> (what it needs, as a text key): greyed out until it holds
def _up2(v):
    return v('upscale') not in ('', '1')
REQUIRES = {
    'pgxp': (_up2, 'need_up2'),
    'perspective': (lambda v: _up2(v) and v('pgxp') == '1', 'need_pgxp'),
    'sprite_depth': (lambda v: _up2(v) and v('pgxp') == '1', 'need_pgxp'),
    'interp': (lambda v: _up2(v) and v('pgxp') == '1', 'need_pgxp'),
    'drawthreads': (_up2, 'need_up2'),
    'texturepack': (_up2, 'need_up2'),
    'dumptextures': (_up2, 'need_up2'),
}

# option -> (title, explanation)
TEXT = {
    'en': {
        'upscale': ('Internal resolution', 'Draws the 3D 2, 3 or 4 times larger than the original 320x240, '
                    'for smoother edges (1 = original). The processor draws the picture, so each step costs a '
                    'lot more: 2 for 2 cores, 3 for 4 cores, 4 for 6 or more.'),
        'widescreen': ('Widescreen', 'Shows more of the farm and town at the sides. Title, menus and videos stay '
                       '4:3. With resolution 2 or more, people and the HUD keep their shape.'),
        'pgxp': ('Precise 3D (PGXP)', 'No more wobbling polygons, the PlayStation\'s trademark shimmer.'),
        'perspective': ('Perspective-correct textures', 'No warping on large floors and walls. Costs some '
                        'speed.'),
        'sprite_depth': ('Objects shrink with distance', 'Stumps, rocks and people get smaller further away, '
                         'like the ground under them.'),
        'interp': ('60 fps camera', 'The camera and scenery move 60 times a second (the game itself still runs '
                   'at 30). Drawn on spare processor cores; F9 switches it while playing.'),
        'texturepack': ('Texture pack', 'Draws the player with the images in the textures folder (for now only '
                        'the player). See docs/GRAPHICS.md.'),
        'dumptextures': ('Dump textures', 'Writes each picture of the player the game shows to textures/dump, '
                         'to draw a texture pack from.'),
        'drawthreads': ('Drawing threads', 'Processor threads that draw the 3D together, each a share of the '
                        'picture (the picture is the same). auto = by processor; 0 = the game\'s thread alone.'),
        'scale': ('Window size', 'Multiple of 320x240, when not in fullscreen.'),
        'fullscreen': ('Start in fullscreen', 'F11 or Alt+Enter switches at any time.'),
        'scaling': ('Scaling to the screen', 'sharp-bilinear: sharp, even pixels (recommended). sharp: plain '
                    'nearest pixel. smooth: soft.'),
        'aspect43': ('4:3 picture', 'Like a CRT TV. Off: square pixels.'),
        'disc': ('Game disc', 'Your copy of the game: a .cue/.bin image or a .pak data pack. Empty: the disc '
                 'folder or HarvestMoonBTN.pak next to the game.'),
        'savedir': ('Memory card folder', 'Where the memory cards (card1.mcd ... card1_4.mcd) are kept. Empty: '
                    'the saves folder.'),
        'autoslot': ('Memory card slot 1 directly', 'Saving and loading skip the slot question.'),
        'fastcard': ('Fast memory card', 'No waiting while the card is searched, read or written.'),
        'quickui': ('Quick menus', 'Windows open at once, shorter fades.'),
        'fastload': ('Fast loading', 'Full speed on "Now Loading" and through doors.'),
        'fastcd': ('Disc speed', 'Shorter loading pauses: 0 = original, 1 = 4x, 8 = 8x, 16 = 16x. The music '
                   'is not affected.'),
        'textspeed': ('Text speed', 'How fast dialogue is typed: 1 = original, up to 16.'),
        'skipscenes': ('Fast-forward cutscenes', 'Hold START during a cutscene.'),
        'fastforward': ('Fast-forward with Tab', 'Hold Tab to run the game at full speed.'),
        'overclock': ('Processor speed', 'In percent: 100 = the original PlayStation (slows down in busy '
                      'scenes), 200 = no slowdowns.'),
        'emptybag': ('Empty rucksack: R1 / R2 do nothing', 'So running doesn\'t stop when you press them by '
                     'accident.'),
        'record': ('Record sessions', 'Keeps your button presses in the recordings folder, for bug reports.'),
        'stick_as_dpad': ('Left stick moves too', 'The left stick works as the D-pad.'),
    },
    'pt': {
        'upscale': ('Resolução interna', 'Desenha o 3D 2, 3 ou 4 vezes maior que o original 320x240, com bordas '
                    'mais lisas (1 = original). Quem desenha é o processador, então cada nível pesa bem mais: '
                    '2 para 2 núcleos, 3 para 4 núcleos, 4 para 6 ou mais.'),
        'widescreen': ('Tela larga', 'Mostra mais da fazenda e da cidade nas laterais. Título, menus e vídeos '
                       'continuam 4:3. Com resolução 2 ou mais, pessoas e o HUD mantêm a forma.'),
        'pgxp': ('3D preciso (PGXP)', 'Acaba com os polígonos tremendo, o tremido típico do PlayStation.'),
        'perspective': ('Texturas com perspectiva correta', 'Sem distorção em chãos e paredes grandes. Pesa um '
                        'pouco.'),
        'sprite_depth': ('Objetos diminuem com a distância', 'Tocos, pedras e pessoas ficam menores ao longe, '
                         'como o chão embaixo deles.'),
        'interp': ('Câmera a 60 fps', 'A câmera e o cenário se movem 60 vezes por segundo (o jogo continua a '
                   '30). Usa núcleos livres do processador; F9 liga e desliga durante o jogo.'),
        'texturepack': ('Pacote de texturas', 'Desenha o jogador com as imagens da pasta textures (por enquanto '
                        'só o jogador). Veja docs/GRAPHICS.md.'),
        'dumptextures': ('Exportar texturas', 'Grava cada imagem do jogador que o jogo mostra em textures/dump, '
                         'para criar um pacote de texturas.'),
        'drawthreads': ('Threads de desenho', 'Threads do processador que desenham o 3D juntas, cada uma uma '
                        'parte da imagem (a imagem é a mesma). auto = pelo processador; 0 = só a do jogo.'),
        'scale': ('Tamanho da janela', 'Múltiplo de 320x240, fora da tela cheia.'),
        'fullscreen': ('Começar em tela cheia', 'F11 ou Alt+Enter alterna a qualquer momento.'),
        'scaling': ('Ajuste à tela', 'sharp-bilinear: pixels nítidos e iguais (recomendado). sharp: pixel mais '
                    'próximo. smooth: suave.'),
        'aspect43': ('Imagem 4:3', 'Como numa TV de tubo. Desligado: pixels quadrados.'),
        'disc': ('Disco do jogo', 'Sua cópia do jogo: imagem .cue/.bin ou pacote .pak. Vazio: a pasta disc ou '
                 'o HarvestMoonBTN.pak ao lado do jogo.'),
        'savedir': ('Pasta dos memory cards', 'Onde ficam os memory cards (card1.mcd ... card1_4.mcd). Vazio: a '
                    'pasta saves.'),
        'autoslot': ('Usar o slot 1 direto', 'Salvar e carregar sem perguntar o slot do memory card.'),
        'fastcard': ('Memory card rápido', 'Sem espera enquanto o card é procurado, lido ou gravado.'),
        'quickui': ('Menus rápidos', 'Janelas abrem na hora, transições mais curtas.'),
        'fastload': ('Carregamento rápido', 'Velocidade máxima no "Now Loading" e ao passar por portas.'),
        'fastcd': ('Velocidade do disco', 'Pausas de carregamento menores: 0 = original, 1 = 4x, 8 = 8x, '
                   '16 = 16x. A música não é afetada.'),
        'textspeed': ('Velocidade do texto', 'Quão rápido o diálogo aparece: 1 = original, até 16.'),
        'skipscenes': ('Acelerar cenas', 'Segure START durante uma cena.'),
        'fastforward': ('Acelerar com Tab', 'Segure Tab para o jogo rodar em velocidade máxima.'),
        'overclock': ('Velocidade do processador', 'Em porcentagem: 100 = o PlayStation original (fica lento em '
                      'cenas cheias), 200 = sem lentidão.'),
        'emptybag': ('Mochila vazia: R1 / R2 não fazem nada', 'Para não parar de correr ao apertar sem querer.'),
        'record': ('Gravar sessões', 'Guarda os botões apertados na pasta recordings, para relatar bugs.'),
        'stick_as_dpad': ('Analógico esquerdo também anda', 'O analógico esquerdo funciona como o direcional.'),
    },
}
BUTTON_NAMES = {
    'en': {'up': 'Up', 'down': 'Down', 'left': 'Left', 'right': 'Right', 'cross': 'Cross (X)',
           'circle': 'Circle (O)', 'square': 'Square', 'triangle': 'Triangle', 'l1': 'L1', 'r1': 'R1',
           'l2': 'L2', 'r2': 'R2', 'start': 'START', 'select': 'SELECT'},
    'pt': {'up': 'Cima', 'down': 'Baixo', 'left': 'Esquerda', 'right': 'Direita', 'cross': 'Xis (X)',
           'circle': 'Círculo (O)', 'square': 'Quadrado', 'triangle': 'Triângulo', 'l1': 'L1', 'r1': 'R1',
           'l2': 'L2', 'r2': 'R2', 'start': 'START', 'select': 'SELECT'},
}
UI = {
    'en': {
        'subtitle': 'PC port settings', 'start': 'Start', 'video': 'Video', 'game': 'Game',
        'keyboard': 'Keyboard', 'controller': 'Controller',
        'welcome': 'Welcome to the farm!', 'welcome_sub': 'Choose the setup for your PC, then press Play. '
        'Everything can be fine-tuned on the other pages.',
        'data_ok': 'Game found: %s', 'data_missing': 'Game disc not found. Put the .cue/.bin in the disc folder '
        'or choose it on the Game page.',
        'pc': 'This PC: %d cores, %d processor threads. No graphics card needed: the processor draws the picture.',
        'quick': 'Quick setup', 'quick_sub': 'All effects stay on (precise 3D, perspective, 60 fps camera); only '
        'the resolution changes.',
        'preset2': 'PC with 2 cores', 'preset4': 'PC with 4 cores', 'preset6': 'PC with 6 or more cores',
        'preset2_d': 'Resolution 2 (640x480). Smooth on simple or older processors.',
        'preset4_d': 'Resolution 3 (960x720). Sharp, and smooth on most PCs.',
        'preset6_d': 'Resolution 4 (1280x960). The sharpest; needs a strong processor.',
        'use': 'Use this', 'in_use': 'In use', 'recommended': 'Recommended for this PC',
        'keys': 'Handy keys', 'keys_list': [
            ('F1 - F4', 'Memory card 1 - 4 in slot 1 (shown in the window title)'),
            ('L1 / R1  (Q / W)', 'On a diary list: previous / next memory card'),
            ('Triangle (hold)', 'On a diary list: hold 2 seconds to delete that diary'),
            ('START (hold)', 'Fast-forward a cutscene'),
            ('Tab (hold)', 'Fast-forward the game'),
            ('F9', '60 fps camera on / off'),
            ('F11  /  Alt+Enter', 'Fullscreen'),
            ('F12', 'Screenshot (screenshots folder)')],
        'applied': '%s chosen. Save to keep it.',
        'video_sub': 'The picture is drawn by the processor: resolution is what costs the most.',
        'game_sub': 'Game files, memory cards and shortcuts that save time.',
        'keyboard_sub': 'Key names, comma-separated (e.g. X,Space,K). Empty: the default.',
        'controller_sub': 'Controller buttons: a b x y back start leftshoulder rightshoulder lefttrigger '
        'righttrigger dpup dpdown dpleft dpright leftstick rightstick. Empty: the default.',
        'need_up2': 'Needs resolution 2 or more', 'need_pgxp': 'Needs Precise 3D (PGXP)',
        'on': 'On', 'off': 'Off', 'browse': 'Browse...', 'default': 'default: %s',
        'close': 'Close', 'save': 'Save', 'play': 'Save and play',
        'unsaved': 'Unsaved changes', 'saved': 'Saved', 'ask_save': 'Save your changes?',
        'missing': '%s is missing.',
    },
    'pt': {
        'subtitle': 'Configurações do port para PC', 'start': 'Início', 'video': 'Vídeo', 'game': 'Jogo',
        'keyboard': 'Teclado', 'controller': 'Controle',
        'welcome': 'Bem-vindo à fazenda!', 'welcome_sub': 'Escolha a configuração do seu PC e clique em '
        'Jogar. Tudo pode ser ajustado nas outras páginas.',
        'data_ok': 'Jogo encontrado: %s', 'data_missing': 'Disco do jogo não encontrado. Coloque o .cue/.bin na '
        'pasta disc ou escolha na página Jogo.',
        'pc': 'Este PC: %d núcleos, %d threads. Não precisa de placa de vídeo: o processador desenha a imagem.',
        'quick': 'Configuração rápida', 'quick_sub': 'Todos os efeitos continuam ligados (3D preciso, '
        'perspectiva, câmera a 60 fps); só a resolução muda.',
        'preset2': 'PC com 2 núcleos', 'preset4': 'PC com 4 núcleos', 'preset6': 'PC com 6 ou mais núcleos',
        'preset2_d': 'Resolução 2 (640x480). Liso em processadores simples ou antigos.',
        'preset4_d': 'Resolução 3 (960x720). Nítido e liso na maioria dos PCs.',
        'preset6_d': 'Resolução 4 (1280x960). O mais nítido; pede um processador forte.',
        'use': 'Usar este', 'in_use': 'Em uso', 'recommended': 'Recomendado para este PC',
        'keys': 'Teclas úteis', 'keys_list': [
            ('F1 - F4', 'Memory card 1 - 4 no slot 1 (aparece no título da janela)'),
            ('L1 / R1  (Q / W)', 'Na lista de diários: memory card anterior / seguinte'),
            ('Triângulo (segurar)', 'Na lista de diários: segure 2 segundos para apagar o diário'),
            ('START (segurar)', 'Acelera uma cena'),
            ('Tab (segurar)', 'Acelera o jogo'),
            ('F9', 'Liga / desliga a câmera a 60 fps'),
            ('F11  /  Alt+Enter', 'Tela cheia'),
            ('F12', 'Captura de tela (pasta screenshots)')],
        'applied': '%s escolhido. Salve para manter.',
        'video_sub': 'A imagem é desenhada pelo processador: a resolução é o que mais pesa.',
        'game_sub': 'Arquivos do jogo, memory cards e atalhos que poupam tempo.',
        'keyboard_sub': 'Nomes das teclas separados por vírgula (ex.: X,Space,K). Vazio: o padrão.',
        'controller_sub': 'Botões do controle: a b x y back start leftshoulder rightshoulder lefttrigger '
        'righttrigger dpup dpdown dpleft dpright leftstick rightstick. Vazio: o padrão.',
        'need_up2': 'Precisa de resolução 2 ou mais', 'need_pgxp': 'Precisa do 3D preciso (PGXP)',
        'on': 'Ligado', 'off': 'Desligado', 'browse': 'Procurar...', 'default': 'padrão: %s',
        'close': 'Fechar', 'save': 'Salvar', 'play': 'Salvar e jogar',
        'unsaved': 'Alterações não salvas', 'saved': 'Salvo', 'ask_save': 'Salvar as alterações?',
        'missing': '%s não foi encontrado.',
    },
}

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
    assert cpu_cores()[0] >= 1 and language() in UI and preset_for(2) == 0 and preset_for(8) == 2
    assert all(set(TEXT[l]) == set(TEXT['en']) and set(UI[l]) == set(UI['en']) for l in UI)
    print('selftest ok')


def cpu_cores():
    """(physical cores, logical processors)"""
    threads = os.cpu_count() or 1
    try:
        if os.name == 'nt':
            import ctypes
            from ctypes import wintypes
            k32 = ctypes.windll.kernel32
            n = wintypes.DWORD(0)
            k32.GetLogicalProcessorInformationEx(0, None, ctypes.byref(n))     # 0: RelationProcessorCore
            buf = ctypes.create_string_buffer(n.value)
            if k32.GetLogicalProcessorInformationEx(0, buf, ctypes.byref(n)):
                cores = off = 0
                while off < n.value:
                    rel, size = struct.unpack_from('<II', buf.raw, off)
                    cores += rel == 0
                    off += size
                if cores:
                    return cores, threads
        else:
            ids = set()
            phys = None
            for line in open('/proc/cpuinfo'):
                k, _, v = line.partition(':')
                if k.strip() == 'physical id':
                    phys = v.strip()
                elif k.strip() == 'core id':
                    ids.add((phys, v.strip()))
            if ids:
                return len(ids), threads
    except Exception:
        pass
    return max(1, threads // 2) if threads > 2 else threads, threads


def preset_for(cores):
    return 0 if cores <= 2 else 1 if cores <= 5 else 2


def language():
    try:
        if os.name == 'nt':
            import ctypes
            return 'pt' if ctypes.windll.kernel32.GetUserDefaultUILanguage() & 0x3FF == 0x16 else 'en'
        import locale
        lang = locale.getlocale()[0] or os.environ.get('LANG', '')
        return 'pt' if lang.lower().startswith('pt') else 'en'
    except Exception:
        return 'en'


def game_data(disc):
    """the game data the game will use (as it looks for it), or None"""
    if disc:
        p = disc if os.path.isabs(disc) else os.path.join(HERE, disc)
        if os.path.exists(p):
            return p
    pak = os.path.join(HERE, 'HarvestMoonBTN.pak')
    if os.path.exists(pak):
        return pak
    d = os.path.join(HERE, 'disc')
    if os.path.isdir(d):
        for f in sorted(os.listdir(d)):
            if f.lower().endswith(('.cue', '.bin')):
                return os.path.join(d, f)
    return None


# colours: a farm in the morning
BG, CARD, LINE_C = '#f5efe0', '#fffdf7', '#e3d8bd'
GREEN, GREEN_HI, GREEN_LO = '#2f5d3a', '#3f7a4d', '#24482d'
ACCENT, ACCENT_HI = '#e08a2e', '#c97520'
TEXT_C, MUTED, WARN, OK = '#2b2a26', '#6e685a', '#b5651d', '#2e7d32'


def main():
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox

    text = open(INI, encoding='utf-8').read() if os.path.exists(INI) else ''
    loaded, active = read_ini(text)
    cores, threads = cpu_cores()
    state = {'lang': language(), 'page': 'start', 'dirty': False, 'note': ''}

    if os.name == 'nt':             # sharp text on scaled screens (Tk then sizes fonts for the DPI)
        try:
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except Exception:
            pass
    root = tk.Tk()
    k = max(1.0, root.winfo_fpixels('1i') / 96.0)     # pixel sizes below are for 96 DPI

    def px(n):
        return int(n * k)
    root.title('Harvest Moon: Back to Nature')
    root.geometry('%dx%d' % (px(940), px(700)))
    root.minsize(px(780), px(560))
    root.configure(bg=BG)
    fam = 'Segoe UI' if os.name == 'nt' else 'TkDefaultFont'
    F = {'h1': (fam, 18, 'bold'), 'h2': (fam, 13, 'bold'), 'b': (fam, 10, 'bold'), 'n': (fam, 10),
         's': (fam, 9), 'nav': (fam, 11)}

    st = ttk.Style(root)
    st.theme_use('clam')
    st.configure('TCombobox', fieldbackground='white', background=CARD, arrowcolor=GREEN, padding=4)
    st.map('TCombobox', fieldbackground=[('readonly', 'white'), ('disabled', '#eee9dc')])
    st.configure('TEntry', fieldbackground='white', padding=4)
    st.configure('TSpinbox', fieldbackground='white', arrowcolor=GREEN, padding=4)
    st.configure('Vertical.TScrollbar', background=LINE_C, troughcolor=BG, bordercolor=BG, arrowcolor=GREEN)
    root.option_add('*TCombobox*Listbox.font', F['n'])

    def T(k):
        return UI[state['lang']][k]

    # one variable per option, kept across rebuilds (language switch)
    vars_ = {}
    for page, section, key, kind in OPTIONS:
        # a path from a commented example ("#disc = C:\Games\...") is not a real setting
        start = loaded.get(key, '') if (key in active or kind not in ('file', 'dir')) else ''
        if kind.startswith('choice:') and not start:
            start = kind[7:].split('|')[0]       # the game's default
        vars_[key] = tk.StringVar(value=start)

    def val(k):
        return vars_[k].get().strip()

    widgets = {}                                 # key -> (set_enabled, note label)

    def refresh():
        for key, (cond, why) in REQUIRES.items():
            if key in widgets:
                ok = cond(val)
                widgets[key][0](ok)
                widgets[key][1].configure(text='' if ok else T(why))
        status.configure(text=state['note'] or (T('unsaved') if state['dirty'] else ''),
                         fg=WARN if state['dirty'] else OK)

    def changed(*_):
        state['dirty'] = True
        state['note'] = ''
        refresh()

    # ---- widgets ----
    def button(parent, label, cmd, accent=False, small=False):
        bg, hi = (ACCENT, ACCENT_HI) if accent else (CARD, '#efe7d2')
        b = tk.Label(parent, text=label, bg=bg, fg='white' if accent else TEXT_C, cursor='hand2',
                     font=F['b'] if accent else F['n'], padx=10 if small else 16, pady=4 if small else 7,
                     highlightthickness=0 if accent else 1, highlightbackground=LINE_C)
        b.bind('<Button-1>', lambda e: cmd())
        b.bind('<Enter>', lambda e: b.configure(bg=hi))
        b.bind('<Leave>', lambda e: b.configure(bg=bg))
        return b

    def toggle(parent, var):
        """an on/off switch for a '1'/'0' variable"""
        f = tk.Frame(parent, bg=CARD)
        c = tk.Canvas(f, width=px(46), height=px(24), bg=CARD, highlightthickness=0, cursor='hand2')
        c.pack(side='left')
        lab = tk.Label(f, bg=CARD, font=F['s'], fg=MUTED, width=9, anchor='w')
        lab.pack(side='left', padx=(6, 0))
        en = [True]

        def draw(*_):
            on = var.get() == '1'
            c.delete('all')
            col = (GREEN_HI if on else '#c9bfa6') if en[0] else '#e2dccb'
            c.create_oval(px(1), px(1), px(23), px(23), fill=col, outline=col)
            c.create_oval(px(23), px(1), px(45), px(23), fill=col, outline=col)
            c.create_rectangle(px(12), px(1), px(34), px(23), fill=col, outline=col)
            x = px(24) if on else px(3)
            c.create_oval(x, px(3), x + px(18), px(21), fill='white', outline='white')
            lab.configure(text=T('on') if on else T('off'))

        def click(e):
            if en[0]:
                var.set('0' if var.get() == '1' else '1')
        c.bind('<Button-1>', click)
        var.trace_add('write', draw)
        draw()

        def enable(ok):
            en[0] = ok
            c.configure(cursor='hand2' if ok else 'arrow')
            draw()
        return f, enable

    def scroll_page(parent):
        """a page that scrolls; returns its inner frame"""
        outer = tk.Frame(parent, bg=BG)
        cv = tk.Canvas(outer, bg=BG, highlightthickness=0)
        sb = ttk.Scrollbar(outer, orient='vertical', command=cv.yview)
        inner = tk.Frame(cv, bg=BG)
        win = cv.create_window(0, 0, window=inner, anchor='nw')
        cv.configure(yscrollcommand=sb.set)
        inner.bind('<Configure>', lambda e: cv.configure(scrollregion=cv.bbox('all')))
        cv.bind('<Configure>', lambda e: cv.itemconfigure(win, width=e.width))
        cv.pack(side='left', fill='both', expand=True)
        sb.pack(side='right', fill='y')

        def wheel(e):
            if inner.winfo_height() > cv.winfo_height():
                cv.yview_scroll(int(-e.delta / 120) or (-1 if e.delta > 0 else 1), 'units')
        cv.bind('<Enter>', lambda e: root.bind_all('<MouseWheel>', wheel))
        cv.bind('<Leave>', lambda e: root.unbind_all('<MouseWheel>'))
        return outer, inner

    def card(parent, pad=16):
        f = tk.Frame(parent, bg=CARD, highlightthickness=1, highlightbackground=LINE_C, padx=pad, pady=pad - 4)
        f.pack(fill='x', padx=24, pady=(0, 12))
        return f

    def page_head(parent, title, sub):
        tk.Label(parent, text=title, font=F['h1'], bg=BG, fg=GREEN, anchor='w').pack(fill='x', padx=24,
                                                                                    pady=(20, 0))
        tk.Label(parent, text=sub, font=F['n'], bg=BG, fg=MUTED, anchor='w', justify='left',
                 wraplength=px(640)).pack(fill='x', padx=24, pady=(2, 14))

    def option_row(parent, key, kind, title, desc, first):
        if not first:
            tk.Frame(parent, bg=LINE_C, height=1).pack(fill='x', pady=8)
        row = tk.Frame(parent, bg=CARD)
        row.pack(fill='x')
        row.columnconfigure(0, weight=1)
        txt = tk.Frame(row, bg=CARD)
        txt.grid(row=0, column=0, sticky='w')
        tk.Label(txt, text=title, font=F['b'], bg=CARD, fg=TEXT_C, anchor='w').pack(fill='x')
        if desc:
            d = tk.Label(txt, text=desc, font=F['s'], bg=CARD, fg=MUTED, anchor='w', justify='left',
                         wraplength=px(430))
            d.pack(fill='x')
            # lines break where the control beside it begins
            row.bind('<Configure>', lambda e, d=d: d.configure(
                wraplength=max(px(200), e.width - ctl.winfo_reqwidth() - px(28))))
        note = tk.Label(txt, text='', font=F['s'], bg=CARD, fg=WARN, anchor='w')
        note.pack(fill='x')
        ctl = tk.Frame(row, bg=CARD)
        ctl.grid(row=0, column=1, sticky='e', padx=(16, 0))
        var = vars_[key]
        if kind == 'bool':
            w, enable = toggle(ctl, var)
            w.pack()
        elif kind.startswith(('choice:', 'edit:')):
            w = ttk.Combobox(ctl, values=kind.split(':', 1)[1].split('|'), width=16, font=F['n'],
                             textvariable=var, state='readonly' if kind.startswith('choice:') else 'normal')
            w.pack()
            enable = lambda ok, w=w, ro=kind.startswith('choice:'): w.configure(
                state=('readonly' if ro else 'normal') if ok else 'disabled')
        elif kind.startswith('spin'):
            w = ttk.Spinbox(ctl, from_=1, to=int(kind[4:]), width=6, font=F['n'], textvariable=var)
            w.pack()
            enable = lambda ok, w=w: w.configure(state='normal' if ok else 'disabled')
        else:
            w = ttk.Entry(ctl, textvariable=var, width=34 if kind in ('file', 'dir') else 22, font=F['n'])
            w.pack(side='left')
            enable = lambda ok, w=w: w.configure(state='normal' if ok else 'disabled')
            if kind in ('file', 'dir'):
                def browse(kind=kind):
                    p = (filedialog.askopenfilename(filetypes=[('Game data', '*.cue *.bin *.pak'), ('*', '*')])
                         if kind == 'file' else filedialog.askdirectory())
                    if p:
                        var.set(os.path.normpath(p))
                button(ctl, T('browse'), browse, small=True).pack(side='left', padx=(6, 0))
        widgets[key] = (enable, note)

    def options_page(parent, page):
        rows = [(key, kind) for pg, sec, key, kind in OPTIONS if pg == page]
        c = card(parent)
        for i, (key, kind) in enumerate(rows):
            if page in ('keyboard', 'controller') and key[:4] in ('key_', 'pad_'):
                b = key[4:]
                dflt = loaded.get(key, '') if key not in active else ''
                option_row(c, key, kind, BUTTON_NAMES[state['lang']][b],
                           T('default') % dflt if dflt else '', i == 0)
            else:
                title, desc = TEXT[state['lang']][key]
                option_row(c, key, kind, title, desc, i == 0)

    def start_page(parent):
        page_head(parent, T('welcome'), T('welcome_sub'))
        c = card(parent)
        data = game_data(val('disc'))
        if data and os.path.abspath(data).startswith(os.path.abspath(HERE)):
            data = os.path.relpath(data, HERE)
        tk.Label(c, text=('\u2714  ' + T('data_ok') % data) if data else ('\u26a0  ' + T('data_missing')),
                 font=F['n'], bg=CARD, fg=OK if data else WARN, anchor='w', justify='left',
                 wraplength=px(800)).pack(fill='x')
        tk.Label(c, text='\U0001f5a5  ' + T('pc') % (cores, threads), font=F['n'], bg=CARD, fg=TEXT_C,
                 anchor='w', justify='left', wraplength=px(800)).pack(fill='x', pady=(6, 0))

        tk.Label(parent, text=T('quick'), font=F['h2'], bg=BG, fg=GREEN, anchor='w').pack(fill='x', padx=24,
                                                                                         pady=(8, 0))
        tk.Label(parent, text=T('quick_sub'), font=F['s'], bg=BG, fg=MUTED, anchor='w', justify='left',
                 wraplength=px(800)).pack(fill='x', padx=24, pady=(0, 8))
        row = tk.Frame(parent, bg=BG)
        row.pack(fill='x', padx=24)
        rec = preset_for(cores)
        for i, (name, vals) in enumerate(PRESETS):
            row.columnconfigure(i, weight=1, uniform='p')
            in_use = all(val(k) == v for k, v in {**PRESET_ALL, **vals}.items())
            f = tk.Frame(row, bg=CARD, highlightthickness=2 if i == rec else 1,
                         highlightbackground=ACCENT if i == rec else LINE_C, padx=14, pady=12)
            f.grid(row=0, column=i, sticky='nsew', padx=(0 if i == 0 else 6, 0))
            tk.Label(f, text=T('recommended') if i == rec else ' ', font=F['s'], bg=CARD,
                     fg=ACCENT_HI, anchor='w').pack(fill='x')
            labs = [tk.Label(f, text=T(name), font=F['h2'], bg=CARD, fg=TEXT_C, anchor='w', justify='left'),
                    tk.Label(f, text=T(name + '_d'), font=F['s'], bg=CARD, fg=MUTED, anchor='w', justify='left')]
            labs[0].pack(fill='x')
            labs[1].pack(fill='x', pady=(4, 10))
            # lines break at the card's width, whatever the window's
            f.bind('<Configure>', lambda e, labs=labs: [l.configure(wraplength=max(px(80), e.width - px(34)))
                                                         for l in labs])
            if in_use:
                tk.Label(f, text='\u2714 ' + T('in_use'), font=F['b'], bg=CARD, fg=OK, anchor='w').pack(fill='x')
            else:
                def use(name=name, vals=vals):
                    for k, v in {**PRESET_ALL, **vals}.items():
                        vars_[k].set(v)
                    state['note'] = T('applied') % T(name)
                    show('start')
                    refresh()
                button(f, T('use'), use, accent=i == rec, small=True).pack(anchor='w')

        tk.Label(parent, text=T('keys'), font=F['h2'], bg=BG, fg=GREEN, anchor='w').pack(fill='x', padx=24,
                                                                                        pady=(18, 8))
        c = card(parent)
        for i, (key, what) in enumerate(T('keys_list')):
            tk.Label(c, text=key, font=F['b'], bg=CARD, fg=GREEN, anchor='w').grid(row=i, column=0, sticky='w',
                                                                                  pady=2)
            tk.Label(c, text=what, font=F['n'], bg=CARD, fg=TEXT_C, anchor='w').grid(row=i, column=1, sticky='w',
                                                                                    padx=(18, 0), pady=2)

    # ---- window ----
    body = tk.Frame(root, bg=BG)
    body.pack(fill='both', expand=True)
    foot = tk.Frame(root, bg=CARD, highlightthickness=1, highlightbackground=LINE_C)
    foot.pack(fill='x', side='bottom')
    status = tk.Label(foot, text='', font=F['n'], bg=CARD, anchor='w')

    def save():
        nonlocal text
        for page, section, key, kind in OPTIONS:
            v = val(key)
            if kind.startswith('spin') and v.isdigit():     # typed numbers too: keep them in range
                v = str(min(max(int(v), 1), int(kind[4:])))
                vars_[key].set(v)
            was = loaded.get(key, '') if (key in active or kind not in ('file', 'dir')) else ''
            if not was and kind.startswith('choice:'):
                was = kind[7:].split('|')[0]        # the game's default: nothing to write
            if v != was:
                text = set_ini(text, section, key, v)
        with open(INI, 'w', encoding='utf-8') as f:
            f.write(text)
        loaded.update(read_ini(text)[0])
        active.update(read_ini(text)[1])
        state['dirty'] = False
        state['note'] = '\u2714 ' + T('saved')
        refresh()

    def play():
        save()
        if not os.path.exists(EXE):
            messagebox.showerror('Harvest Moon', T('missing') % EXE)
            return
        subprocess.Popen([EXE], cwd=HERE)
        root.destroy()

    def close():
        if state['dirty']:
            a = messagebox.askyesnocancel('Harvest Moon', T('ask_save'))
            if a is None:
                return
            if a:
                save()
        root.destroy()
    root.protocol('WM_DELETE_WINDOW', close)

    def build():
        for w in body.winfo_children():
            w.destroy()
        for w in foot.winfo_children():
            w.destroy() if w is not status else w.pack_forget()
        widgets.clear()
        # header
        head = tk.Frame(body, bg=GREEN, padx=24, pady=12)
        head.pack(fill='x')
        tk.Label(head, text='\U0001f33e  Harvest Moon: Back to Nature', font=F['h1'], bg=GREEN, fg='white'
                 ).pack(side='left')
        tk.Label(head, text=T('subtitle'), font=F['n'], bg=GREEN, fg='#cfe3c9').pack(side='left', padx=(12, 0),
                                                                                   pady=(6, 0))
        langs = tk.Frame(head, bg=GREEN)
        langs.pack(side='right')
        for l in ('pt', 'en'):
            b = tk.Label(langs, text=l.upper(), font=F['b'], bg=GREEN_HI if l == state['lang'] else GREEN,
                         fg='white', padx=8, pady=2, cursor='hand2')
            b.pack(side='left', padx=1)
            b.bind('<Button-1>', lambda e, l=l: (state.update(lang=l), build()))
        # side menu and pages
        main_ = tk.Frame(body, bg=BG)
        main_.pack(fill='both', expand=True)
        side = tk.Frame(main_, bg=GREEN_LO, width=px(170))
        side.pack(side='left', fill='y')
        side.pack_propagate(False)
        holder = tk.Frame(main_, bg=BG)
        holder.pack(side='left', fill='both', expand=True)
        pages, navs = {}, {}
        icons = {'start': '\U0001f3e0', 'video': '\U0001f5bc', 'game': '\U0001f3ae', 'keyboard': '\u2328',
                 'controller': '\U0001f579'}
        for name in ('start', 'video', 'game', 'keyboard', 'controller'):
            n = tk.Label(side, text='  %s  %s' % (icons[name], T(name)), font=F['nav'], bg=GREEN_LO,
                         fg='white', anchor='w', padx=14, pady=10, cursor='hand2')
            n.pack(fill='x')
            n.bind('<Button-1>', lambda e, name=name: show(name))
            navs[name] = n
            outer, inner = scroll_page(holder)
            pages[name] = outer
            if name == 'start':
                start_page(inner)
            else:
                page_head(inner, T(name), T(name + '_sub'))
                options_page(inner, name)
            tk.Frame(inner, bg=BG, height=12).pack()
        state['pages'], state['navs'] = pages, navs
        # footer
        status.pack(side='left', padx=16)
        button(foot, T('play'), play, accent=True).pack(side='right', padx=(6, 16), pady=10)
        button(foot, T('save'), save).pack(side='right', padx=6, pady=10)
        button(foot, T('close'), close).pack(side='right', padx=6, pady=10)
        show(state['page'])
        refresh()

    def show(name):
        if name == 'start' and 'pages' in state:   # the presets show what is in use now
            old = state['pages'].pop('start')
            outer, inner = scroll_page(old.master)
            start_page(inner)
            state['pages']['start'] = outer
            old.destroy()
        state['page'] = name
        for n, p in state['pages'].items():
            p.pack_forget()
            state['navs'][n].configure(bg=GREEN_HI if n == name else GREEN_LO)
        state['pages'][name].pack(fill='both', expand=True)

    for v in vars_.values():
        v.trace_add('write', changed)
    build()
    root.mainloop()


if __name__ == '__main__':
    selftest() if '--selftest' in sys.argv else main()

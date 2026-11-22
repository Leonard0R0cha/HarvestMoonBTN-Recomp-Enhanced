#!/usr/bin/env python3
"""Put the text of a translated disc onto a copy of the USA disc image (build.py --lang, translations
that come as a whole disc image instead of a patch for the USA disc).

Such a disc may carry its own changes to the game's code, which this port can't run, so only the
text is taken, from where the USA game keeps it:
  - the dialogue: the text half of every 8 KB record of the A_FILE.BIN subfiles 02-06
    (the portrait half stays the USA one)
  - the texts of the executable (menus, items, shops, memory card): written in place, every text
    starting where it starts on the USA disc and padded with spaces up to its own length, so the
    texts the game finds by counting the FFFF ends stay where they were
  - the names of the weekdays and seasons (fixed width)
  - with --font (a translation that draws its own letters, like the Spanish one): the font, the first
    8 KB of subfile 00 (the font image and the palettes of the text's TIMs)
The files keep their size, so only the changed sectors are written (with a new EDC/ECC).

The European disc (SLES-02781, English and German) works the same way for German: its subfiles 07-11
are the German dialogue, laid out like the English 02-06 (which match the USA ones), and its font is
the USA one. Its executable keeps its texts elsewhere, so the menus and items stay in English.

usage: python tools/transplant.py [--font] USA.bin|.cue TRANSLATED.zip|.iso|.bin OUT.bin
       python tools/transplant.py --patch [--font] USA.bin|.cue TRANSLATED.zip|.iso|.bin OUT.hmtxt
         (only the bytes the text changes, which the build downloads instead of the translated disc)
"""
import hashlib, io, os, struct, sys, zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from iso import Disc, find_file, resolve_bin   # noqa: E402

SECTOR = 2352
EXE_BASE, EXE_HEADER = 0x80010000, 0x800
# the text blocks of SLUS_011.15 (USA): every text ends with FFFF
EXE_TEXT = ((0x8005175C, 0x80054BBA), (0x800556B6, 0x800558A2))
# texts the game puts into dialogue by address ({400F}: "Honey"/"Darling"): a padding space would show
EXE_TEXT_EXACT = (0x80054396, 0x800543A2)
# the season names (6 letters each) and the weekday names (3 letters each): fixed width, no FFFF
EXE_NAMES = (0x8004908C, 0x800490E6)
DIALOGUE_SUBFILES = (2, 3, 4, 5, 6)
PAL_EXE, PAL_GERMAN = 'SLES_027.81', (7, 8, 9, 10, 11)   # the European disc: its German dialogue
RECORD, TEXT_AT = 0x2000, 0x1800
FONT_SUBFILE, FONT_SIZE = 0, 0x2000      # --font: the start of subfile 00 (font image + text palettes)
END, SPACE = 0xFFFF, 0x0FFD


# ---- CD-ROM Mode 2 Form 1 error detection / correction (ECMA-130) ----

def _edc_table():
    t = []
    for i in range(256):
        e = i
        for _ in range(8):
            e = (e >> 1) ^ (0xD8018001 if e & 1 else 0)
        t.append(e)
    return t


EDC = _edc_table()
ECC_F = [((i << 1) ^ (0x11D if i & 0x80 else 0)) & 0xFF for i in range(256)]
ECC_B = [0] * 256
for _i in range(256):
    ECC_B[_i ^ ECC_F[_i]] = _i


def edc(data):
    e = 0
    for b in data:
        e = EDC[(e ^ b) & 0xFF] ^ (e >> 8)
    return e


def _ecc_part(src, major_count, minor_count, major_mult, minor_inc, out, out_at):
    size = major_count * minor_count
    for major in range(major_count):
        index = (major >> 1) * major_mult + (major & 1)
        a = b = 0
        for _ in range(minor_count):
            t = src[index]
            index += minor_inc
            if index >= size:
                index -= size
            a ^= t
            b ^= t
            a = ECC_F[a]
        a = ECC_B[ECC_F[a] ^ b]
        out[out_at + major] = a
        out[out_at + major + major_count] = a ^ b


def fix_form1(sector):
    """recompute the EDC and ECC of a Mode 2 Form 1 sector (bytearray of 2352) after its data changed"""
    struct.pack_into('<I', sector, 0x818, edc(sector[0x10:0x818]))
    # the ECC is computed with the header (address + mode) taken as zero
    work = bytearray(sector[0x0C:0x930])
    work[0:4] = b'\0\0\0\0'
    p = bytearray(172)
    _ecc_part(work, 86, 24, 2, 86, p, 0)
    work[0x81C - 0x0C:0x8C8 - 0x0C] = p
    q = bytearray(104)
    _ecc_part(work, 52, 43, 86, 88, q, 0)
    sector[0x81C:0x8C8] = p
    sector[0x8C8:0x930] = q


# ---- reading the translated disc ----

def read_files(path, names, optional=()):
    """{name: bytes} of the game files on a disc image (.bin/.iso/.cue) or in a .zip with one inside
    (the optional ones only when they are there)"""
    if path.lower().endswith('.zip'):
        with zipfile.ZipFile(path) as z:
            inner = [n for n in z.namelist() if n.lower().endswith(('.bin', '.iso'))]
            if not inner:
                raise SystemExit('no disc image (.bin/.iso) inside ' + path)
            with z.open(max(inner, key=lambda n: z.getinfo(n).file_size)) as f:
                data = f.read()
        disc = Disc.__new__(Disc)
        disc.f = io.BytesIO(data)
        disc.nsect = len(data) // SECTOR
    else:
        disc = Disc(resolve_bin(path))
    out = {}
    for n in tuple(names) + tuple(optional):
        e = find_file(disc, n)
        if not e:
            if n in optional:
                continue
            raise SystemExit('%s is not on %s' % (n, path))
        out[n] = disc.read_extent(e['lba'], e['size'])
    return out


def afile_split(hdt, pack):
    offs = struct.unpack('<%dI' % (len(hdt) // 4), hdt)
    return [(offs[i], offs[i + 1]) for i in range(len(offs) - 1)]


def words(b, at):
    return struct.unpack_from('<H', b, at)[0]


# ---- the transplant ----

def dialogue(us_hdt, us_pack, tr_hdt, tr_pack, from_subs=DIALOGUE_SUBFILES, fit=False):
    """the USA A_FILE.BIN with the text half of each dialogue record from the translated one (its subfiles
    from_subs, in the order of the USA 02-06; with fit, as many records as both have)"""
    out = bytearray(us_pack)
    us_parts, tr_parts = afile_split(us_hdt, us_pack), afile_split(tr_hdt, tr_pack)
    taken = 0
    for sub, tr_sub in zip(DIALOGUE_SUBFILES, from_subs):
        (ua, uz), (ta, tz) = us_parts[sub], tr_parts[tr_sub]
        if uz - ua != tz - ta and not fit:
            raise SystemExit('dialogue subfile %02d has another size on the translated disc' % sub)
        for rec in range(min(uz - ua, tz - ta) // RECORD):
            src = tr_pack[ta + rec * RECORD + TEXT_AT:ta + (rec + 1) * RECORD]
            # a text that does not end in its 2 KB is not one this game can show: keep the USA one
            if not any(words(src, p) == END for p in range(0, len(src), 2)):
                continue
            at = ua + rec * RECORD + TEXT_AT
            if out[at:at + len(src)] != src:
                taken += 1
            out[at:at + len(src)] = src
    return bytes(out), taken


def exe_texts(us_exe, tr_exe):
    """the USA executable with the translated texts written in place"""
    out = bytearray(us_exe)
    fo = lambda a: a - EXE_BASE + EXE_HEADER
    exact = {fo(a) for a in EXE_TEXT_EXACT}
    taken = 0
    for a, z in EXE_TEXT:
        begin = p = fo(a)
        while p < fo(z):
            if words(us_exe, p) == END:
                q = begin                       # the translation: from the same start, up to its FFFF
                while q < p and words(tr_exe, q) != END:
                    q += 2
                new = bytearray(tr_exe[begin:q])
                if begin in exact and q < p:    # shorter: ends early, the rest stays as it was
                    new += struct.pack('<H', END)
                    new += us_exe[begin + len(new):p]
                else:
                    new += struct.pack('<H', SPACE) * ((p - q) // 2)
                if bytes(new) != us_exe[begin:p]:
                    taken += 1
                out[begin:p] = new
                begin = p + 2
            p += 2
    a, z = fo(EXE_NAMES[0]), fo(EXE_NAMES[1])
    if any(words(tr_exe, p) == END for p in range(a, z, 2)):
        raise SystemExit('the weekday and season names have another layout on the translated disc')
    out[a:z] = tr_exe[a:z]
    return bytes(out), taken


def write_file(img, disc, name, data, prefix=False):
    """overwrite a file of the same size (or, with prefix, its start) on the image (bytearray), fixing
    each changed sector"""
    e = find_file(disc, name)
    if e['size'] != len(data) and not (prefix and len(data) <= e['size']):
        raise SystemExit('%s: size changed' % name)
    for i in range((len(data) + 2047) // 2048):
        chunk = data[i * 2048:(i + 1) * 2048]
        at = (e['lba'] + i) * SECTOR
        old = img[at + 24:at + 24 + len(chunk)]
        if old != chunk:
            sector = bytearray(img[at:at + SECTOR])
            sector[24:24 + len(chunk)] = chunk
            fix_form1(sector)
            img[at:at + SECTOR] = sector


NAMES = ('SLUS_011.15', 'A_FILE.HDT', 'A_FILE.BIN')


# ---- the Japanese disc (SLPS-02489 Rev 1): its dialogue, its font and the texts of its executable ----
#
# Its dialogue subfiles 02-06 have the USA records one for one (the USA 02 and 03 have a few more at the
# end, which stay English). Its font is another: 12x12 cells, 735 of them (kana, Latin capitals, digits,
# kanji), the first 0xCAE0 bytes of its subfile 00 (13 entries of the packed-image list for VRAM 896,0
# and 960,0); it goes at the start of DUMMY.DAT (padding the game never reads) for the port to load in
# place of the USA one (runtime/japanese.c). Its executable keeps its texts in the order of the USA ones
# (some more, some fewer): JA_TEXTS says which one goes into each USA text; a few are shortened to fit.

JA_EXE = 'SLPS_024.89'
JA_TEXT = (0x80053600, 0x80055E00)       # the texts of the Japanese executable (every one ends with FFFF)
JA_SEASONS, JA_WEEKDAYS = (67, 68, 69, 70), 66   # its season names and weekday names (one glyph each)
JA_NAMES = 1, 33                         # its 33 names, for the USA list of 39 (EXE_TEXT[1])
JA_OWN_NAMES = {30: (31, ((0, 3),))}   # ... one of them too long: the start of it (see JA_TEXTS)
JA_FONT_BYTES, JA_FONT_MAGIC = 0xCAE0, b'HMBTN JA FONT 1\0'
JA_DUMMY = 'DUMMY.DAT'
# the font's first glyphs, for the texts written here: {hex} is a code, ' ' a space, '/' a new line
JA_KANA = ('あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれ'
           'ろわをんがぎぐげござじずぜぞだぢづでどばびぶべぼぱぴぷぺぽぁぃぅぇぉゃゅょっアイウエ'
           'オカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲン'
           'ガギグゲゴザジズゼゾダヂヅデドバビブベボパピプペポヴァィゥェォャュョッABCDEFG'
           'HIJKLMNOPQRSTUVWXYZ0123456789。、ー「」？！…・：＋－（')
JA_LATIN, JA_DIGITS = 161, 187           # A and 0 in the font
# for each USA text of EXE_TEXT[0], in order: the number of the Japanese text that goes there, None
# (nothing), 'latin' (the USA text in the font's capitals: the name entry's letters), (number, cuts) for
# one too long for the USA place (the glyphs [a:b] of each cut, joined: the same words, shorter) or a
# few words of its own
JA_TEXTS = [
    (214, ((0, 2), (3, 9))), None, None, None, None, 34, 35, 36, (37, ((0, 11), (15, 16))), 38, 39, 40, 41, 42,
    43, 'まんびきした', '{216}の{1d2}をうたがった', (46, ((6, 13),)), 47, 48, (49, ((0, 5), (11, 18))), 50, 51,
    (52, ((3, 19),)), (53, ((0, 11), (15, 16))), (54, ((0, 6), (9, 12), (15, 17))), (55, ((0, 11), (18, 19))),
    56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 71, 72, 73, 74, 75, 76, 77, 80, 81,
    (83, ((0, 5), (6, 12))), 84, 85, (86, ((3, 11),)), 87, 88, 96, 97, 135, 136,
    137, 162, 163, (164, ((0, 1), (2, 7))), 165, 166, 167, 89, 90, 91, 92, 98, 112, 120, 122, 144, 150, 151, 93, 94,
    95, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 113, 114, 115, 116, 117, 118,
    119, 121, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 138, 139, 140, 141, 142, 143,
    145, 146, 147, 148, 149, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 169, 170, 171, 172, 178,
    179, 185, 186, 187, 188, 193, 196, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208,
    'latin', 'latin', 212, 213, 216, (217, ((7, 10),)), 218, 219, (220, ((0, 2),)), 221, 222, 223, 224, 225, 226, 227, 228,
    229, 231, 233, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 246, 247, 248, 249, 250,
    (251, ((0, 3),)), 252, 253, 254, 255, 256, 257, 258, 259, 260, 261, 262, 263, 264, (265, ((4, 7),)), 'きゅうりづけ', 267, 268,
    269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284, 285, 286, 287, 288,
    289, 290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 300, 301, 302, 303, 304, 305, 306, 307, 308,
    309, 310, 311, 312, (313, ((0, 3),)), 314, 315, 316, 317, 318, 319, 320, (321, ((3, 7),)), 322, 323, 324, 326, 325,
    327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340, 341, 342, 343, 344, 345, 346,
    347, 348, 349, 350, 351, 352, 353, 354, 355, 356, 357, 358, 359, 360, 361, 362, 363, 364, 365, 366,
    367, 368, 369, 370, 371, 'パスタ', 373, 374, 375, 376, 377, 378, 379, 380, 381, 382, 383, 384, 385,
    386, 387, 388, 389, 390, 391, 392, 393, 394, 395, 396, 397, 398, 399, 400, 401, 402, 403, 404, 405,
    406, 407, 408, 409, 410, 411, 412,
]


def ja_codes(text):
    out, p = [], 0
    while p < len(text):
        if text[p] == '{':
            q = text.index('}', p)
            out.append(int(text[p + 1:q], 16))
            p = q + 1
            continue
        out.append(SPACE if text[p] == ' ' else 0xFFFE if text[p] == '/' else JA_KANA.index(text[p]))
        p += 1
    return out


def ja_latin(codes):
    """a USA text with its letters and digits in the Japanese font (capitals; other glyphs a space)"""
    out = []
    for w in codes:
        if w < 26 or 0x20 <= w < 0x3A:
            out.append(JA_LATIN + (w & 0x1F))
        elif 0x40 <= w < 0x4A:
            out.append(JA_DIGITS + w - 0x40)
        else:
            out.append(w if w >= 0x0F00 else SPACE)
    return out


def text_list(exe, a, z):
    """[(offset, [codes])] of the texts from a to z of an executable (each ending with FFFF)"""
    out, begin = [], a - EXE_BASE + EXE_HEADER
    for p in range(begin, z - EXE_BASE + EXE_HEADER, 2):
        if words(exe, p) == END:
            out.append((begin, [words(exe, q) for q in range(begin, p, 2)]))
            begin = p + 2
    return out


def write_texts(exe, slots, new):
    """new[i] (codes) over the text slots[i] (offset, codes) of exe (bytearray), padded with spaces (the
    ones the game finds by address end early instead)"""
    exact = {a - EXE_BASE + EXE_HEADER for a in EXE_TEXT_EXACT}
    for (at, old), codes in zip(slots, new):
        if len(codes) > len(old):
            raise SystemExit('a Japanese text does not fit (%d glyphs for %d)' % (len(codes), len(old)))
        if at in exact and len(codes) < len(old):
            codes = codes + [END]
        else:
            codes = codes + [SPACE] * (len(old) - len(codes))
        exe[at:at + 2 * len(codes)] = struct.pack('<%dH' % len(codes), *codes)


def japanese_exe(us_exe, ja_exe):
    """the USA executable with the Japanese texts"""
    ja = [c for _, c in text_list(ja_exe, *JA_TEXT)]
    out = bytearray(us_exe)
    slots = text_list(us_exe, *EXE_TEXT[0])
    if len(slots) != len(JA_TEXTS):
        raise SystemExit('the USA executable has %d texts, not %d' % (len(slots), len(JA_TEXTS)))
    def text(t, old):
        if t is None: return []
        if t == 'latin': return ja_latin(old)
        if isinstance(t, str): return ja_codes(t)
        if isinstance(t, tuple): return [g for a, b in t[1] for g in ja[t[0]][a:b]]
        return ja[t]
    write_texts(out, slots, [text(t, old) for (_, old), t in zip(slots, JA_TEXTS)])
    slots = text_list(us_exe, *EXE_TEXT[1])
    first, count = JA_NAMES
    write_texts(out, slots, [text(JA_OWN_NAMES[i], None) if i in JA_OWN_NAMES else ja[first + i % count]
                             for i in range(len(slots))])
    names = []
    for s in JA_SEASONS:
        names += ja[s][:1] + [SPACE] * 5
    for g in ja[JA_WEEKDAYS][:7]:
        names += [g, SPACE, SPACE]
    a = EXE_NAMES[0] - EXE_BASE + EXE_HEADER
    out[a:a + 2 * len(names)] = struct.pack('<%dH' % len(names), *names)
    return bytes(out)


def japanese_files(us, tr):
    """{name: bytes} of the USA game files with the Japanese disc's text, and the start of DUMMY.DAT with
    its font"""
    pack, n_lines = dialogue(us['A_FILE.HDT'], us['A_FILE.BIN'], tr['A_FILE.HDT'], tr['A_FILE.BIN'],
                             fit=True)
    fa = afile_split(tr['A_FILE.HDT'], tr['A_FILE.BIN'])[FONT_SUBFILE][0]
    font = tr['A_FILE.BIN'][fa:fa + JA_FONT_BYTES]
    if font[:4] != b'\x0d\x10\x0d\x00':
        raise SystemExit("the Japanese disc's font is not where it should be")
    print('  %d dialogue texts and the texts of the executable taken' % n_lines)
    return dict(us, **{'A_FILE.BIN': pack, 'SLUS_011.15': japanese_exe(us['SLUS_011.15'], tr[JA_EXE]),
                       JA_DUMMY: JA_FONT_MAGIC + font})



def usa_files(usa):
    disc = Disc(resolve_bin(usa))
    return disc, {n: disc.read_extent(find_file(disc, n)['lba'], find_file(disc, n)['size']) for n in NAMES}


def take_font(us_hdt, pack, tr_hdt, tr_pack):
    """pack with the font of the translated disc"""
    (ua, _), (ta, _) = afile_split(us_hdt, pack)[FONT_SUBFILE], afile_split(tr_hdt, tr_pack)[FONT_SUBFILE]
    out = bytearray(pack)
    out[ua:ua + FONT_SIZE] = tr_pack[ta:ta + FONT_SIZE]
    return bytes(out)


def translated_files(us, translated, font=False):
    """{name: bytes} of the USA game files with the text of the translated disc (and its font)"""
    tr = read_files(translated, ('A_FILE.HDT', 'A_FILE.BIN'), optional=('SLUS_011.15', PAL_EXE, JA_EXE))
    if JA_EXE in tr:                                    # the Japanese disc
        return japanese_files(us, tr)
    if 'SLUS_011.15' not in tr and PAL_EXE in tr:         # the European disc: its German dialogue only
        pack, n_lines = dialogue(us['A_FILE.HDT'], us['A_FILE.BIN'], tr['A_FILE.HDT'], tr['A_FILE.BIN'],
                                 PAL_GERMAN)
        exe, n_texts = us['SLUS_011.15'], 0
    elif 'SLUS_011.15' in tr:
        pack, n_lines = dialogue(us['A_FILE.HDT'], us['A_FILE.BIN'], tr['A_FILE.HDT'], tr['A_FILE.BIN'])
        exe, n_texts = exe_texts(us['SLUS_011.15'], tr['SLUS_011.15'])
    else:
        raise SystemExit('%s is not a Harvest Moon: Back to Nature disc' % translated)
    if font:
        pack = take_font(us['A_FILE.HDT'], pack, tr['A_FILE.HDT'], tr['A_FILE.BIN'])
    print('  %d dialogue texts and %d texts of the executable taken' % (n_lines, n_texts))
    return dict(us, **{'A_FILE.BIN': pack, 'SLUS_011.15': exe})


def write_image(usa, disc, us, files, out):
    """the USA image with the changed game files -> out; {name: sha256} of its game files"""
    with open(resolve_bin(usa), 'rb') as f:
        img = bytearray(f.read())
    for n in NAMES:
        if files[n] != us[n]:
            write_file(img, disc, n, files[n])
    if JA_DUMMY in files:                   # the Japanese font: the start of the padding file
        write_file(img, disc, JA_DUMMY, files[JA_DUMMY], prefix=True)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'wb') as f:
        f.write(img)
    return {n: hashlib.sha256(d).hexdigest() for n, d in files.items() if n in NAMES}


def transplant(usa, translated, out, font=False):
    """-> {name: sha256} of the game files on the new image"""
    disc, us = usa_files(usa)
    return write_image(usa, disc, us, translated_files(us, translated, font), out)


# ---- the text patch: only the bytes the text changed, so the translation can be downloaded without
# the translated disc. Applied to the USA game files (the build checks what comes out by its SHA-256).
# HMTXT1, then zlib: per file, its name, the number of runs and each run (offset, length, bytes)

PATCH_MAGIC = b'HMTXT1'


def make_patch(us, files):
    body = bytearray()
    for n in NAMES:
        a, b = us[n], files[n]
        runs, p = [], 0
        while p < len(a):
            if a[p:p + 4096] == b[p:p + 4096]:
                p += 4096
                continue
            if a[p] == b[p]:
                p += 1
                continue
            q = p
            while q < len(a) and (a[q] != b[q] or a[q:q + 16] != b[q:q + 16]):
                q += 1                      # equal stretches shorter than 16 bytes stay in the run
            runs.append((p, b[p:q]))
            p = q
        if not runs:
            continue
        body += bytes([len(n)]) + n.encode() + struct.pack('<I', len(runs))
        for at, data in runs:
            body += struct.pack('<II', at, len(data)) + data
    import zlib
    return PATCH_MAGIC + zlib.compress(bytes(body), 9)


def apply_patch(us, patch):
    import zlib
    if not patch.startswith(PATCH_MAGIC):
        raise SystemExit('not a text patch')
    body, p = zlib.decompress(patch[len(PATCH_MAGIC):]), 0
    files = dict(us)
    while p < len(body):
        n = body[p + 1:p + 1 + body[p]].decode()
        p += 1 + body[p]
        out = bytearray(files[n])
        (count,) = struct.unpack_from('<I', body, p)
        p += 4
        for _ in range(count):
            at, size = struct.unpack_from('<II', body, p)
            out[at:at + size] = body[p + 8:p + 8 + size]
            p += 8 + size
        files[n] = bytes(out)
    return files


def patch_image(usa, patch, out):
    """the text patch onto a copy of the USA disc image -> {name: sha256} of its game files"""
    disc, us = usa_files(usa)
    return write_image(usa, disc, us, apply_patch(us, patch), out)


def source_hashes(path, names=('SLUS_011.15', 'A_FILE.BIN')):
    """{name: sha256} of the game files of a translated disc, to know it is the one expected"""
    return {n: hashlib.sha256(d).hexdigest() for n, d in read_files(path, names).items()}


if __name__ == '__main__':
    font = '--font' in sys.argv
    if font:
        sys.argv.remove('--font')
    if len(sys.argv) == 5 and sys.argv[1] == '--patch':
        # the text patch the build downloads: python tools/transplant.py --patch [--font] USA TRANSLATED OUT.hmtxt
        _, us = usa_files(sys.argv[2])
        files = translated_files(us, sys.argv[3], font)
        data = make_patch(us, files)
        if apply_patch(us, data) != files:
            sys.exit('the patch does not give the same files back')
        with open(sys.argv[4], 'wb') as f:
            f.write(data)
        print('%s: %d bytes, sha256 %s' % (sys.argv[4], len(data), hashlib.sha256(data).hexdigest()))
        for n, d in sorted(files.items()):
            print(n, hashlib.sha256(d).hexdigest())
    elif len(sys.argv) == 4:
        for n, h in sorted(transplant(*sys.argv[1:], font=font).items()):
            print(n, h)
    else:
        sys.exit(__doc__)

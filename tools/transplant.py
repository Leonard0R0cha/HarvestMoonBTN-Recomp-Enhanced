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

def dialogue(us_hdt, us_pack, tr_hdt, tr_pack, from_subs=DIALOGUE_SUBFILES):
    """the USA A_FILE.BIN with the text half of each dialogue record from the translated one (its subfiles
    from_subs, in the order of the USA 02-06)"""
    out = bytearray(us_pack)
    us_parts, tr_parts = afile_split(us_hdt, us_pack), afile_split(tr_hdt, tr_pack)
    taken = 0
    for sub, tr_sub in zip(DIALOGUE_SUBFILES, from_subs):
        (ua, uz), (ta, tz) = us_parts[sub], tr_parts[tr_sub]
        if uz - ua != tz - ta:
            raise SystemExit('dialogue subfile %02d has another size on the translated disc' % sub)
        for rec in range((uz - ua) // RECORD):
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


def write_file(img, disc, name, data):
    """overwrite a file of the same size on the image (bytearray), fixing each changed sector"""
    e = find_file(disc, name)
    if e['size'] != len(data):
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
    tr = read_files(translated, ('A_FILE.HDT', 'A_FILE.BIN'), optional=('SLUS_011.15', PAL_EXE))
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
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'wb') as f:
        f.write(img)
    return {n: hashlib.sha256(d).hexdigest() for n, d in files.items()}


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

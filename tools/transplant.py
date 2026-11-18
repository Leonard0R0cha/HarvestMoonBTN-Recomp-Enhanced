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
The files keep their size, so only the changed sectors are written (with a new EDC/ECC).

usage: python tools/transplant.py USA.bin|.cue TRANSLATED.zip|.iso|.bin OUT.bin
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
RECORD, TEXT_AT = 0x2000, 0x1800
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

def read_files(path, names):
    """{name: bytes} of the game files on a disc image (.bin/.iso/.cue) or in a .zip with one inside"""
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
    for n in names:
        e = find_file(disc, n)
        if not e:
            raise SystemExit('%s is not on %s' % (n, path))
        out[n] = disc.read_extent(e['lba'], e['size'])
    return out


def afile_split(hdt, pack):
    offs = struct.unpack('<%dI' % (len(hdt) // 4), hdt)
    return [(offs[i], offs[i + 1]) for i in range(len(offs) - 1)]


def words(b, at):
    return struct.unpack_from('<H', b, at)[0]


# ---- the transplant ----

def dialogue(us_hdt, us_pack, tr_hdt, tr_pack):
    """the USA A_FILE.BIN with the text half of each dialogue record from the translated one"""
    out = bytearray(us_pack)
    us_parts, tr_parts = afile_split(us_hdt, us_pack), afile_split(tr_hdt, tr_pack)
    taken = 0
    for sub in DIALOGUE_SUBFILES:
        (ua, uz), (ta, tz) = us_parts[sub], tr_parts[sub]
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


def transplant(usa, translated, out):
    """-> {name: sha256} of the game files on the new image"""
    names = ('SLUS_011.15', 'A_FILE.HDT', 'A_FILE.BIN')
    tr = read_files(translated, names)
    src = resolve_bin(usa)
    disc = Disc(src)
    us = {n: disc.read_extent(find_file(disc, n)['lba'], find_file(disc, n)['size']) for n in names}
    pack, n_lines = dialogue(us['A_FILE.HDT'], us['A_FILE.BIN'], tr['A_FILE.HDT'], tr['A_FILE.BIN'])
    exe, n_texts = exe_texts(us['SLUS_011.15'], tr['SLUS_011.15'])
    print('  %d dialogue texts and %d texts of the executable taken' % (n_lines, n_texts))
    with open(src, 'rb') as f:
        img = bytearray(f.read())
    write_file(img, disc, 'A_FILE.BIN', pack)
    write_file(img, disc, 'SLUS_011.15', exe)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'wb') as f:
        f.write(img)
    files = dict(us, **{'A_FILE.BIN': pack, 'SLUS_011.15': exe})
    return {n: hashlib.sha256(d).hexdigest() for n, d in files.items()}


def source_hashes(path):
    """{name: sha256} of the game files of a translated disc, to know it is the one expected"""
    return {n: hashlib.sha256(d).hexdigest()
            for n, d in read_files(path, ('SLUS_011.15', 'A_FILE.BIN')).items()}


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    for n, h in sorted(transplant(*sys.argv[1:]).items()):
        print(n, h)

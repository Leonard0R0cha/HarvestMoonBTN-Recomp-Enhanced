#!/usr/bin/env python3
"""Make a sample texture pack from textures you dumped (--dumptextures): each dumped image is
enlarged 4x with Scale2x applied twice (smooth diagonals instead of 4x4 blocks) and written as the
replacement for that texture, next to the dump folder (textures/<key>.png).

usage: python tools/texpack_sample.py DUMPED.png [...]
   e.g. python tools/texpack_sample.py dist/HarvestMoonBTN-Windows-x64/textures/dump/*.png
"""
import glob, os, struct, sys, zlib


def read_dump(path):
    """the RGBA pixels of a PNG written by --dumptextures (8-bit RGBA, no row filters)"""
    b = open(path, 'rb').read()
    w, h, depth, ctype = struct.unpack('>IIBB', b[16:26])
    assert b[:8] == b'\x89PNG\r\n\x1a\n' and depth == 8 and ctype == 6, path + ': not a dumped texture'
    data, pos = b'', 8
    while pos < len(b):
        n, kind = struct.unpack('>I4s', b[pos:pos + 8])
        if kind == b'IDAT':
            data += b[pos + 8:pos + 8 + n]
        pos += 12 + n
    raw = zlib.decompress(data)
    rows = [raw[y * (w * 4 + 1):(y + 1) * (w * 4 + 1)] for y in range(h)]
    assert all(r[0] == 0 for r in rows), path + ': row filters (save it as the game dumped it)'
    return w, h, [[r[1 + 4 * x:5 + 4 * x] for x in range(w)] for r in rows]


def scale2x(w, h, p):
    out = [[None] * (2 * w) for _ in range(2 * h)]
    for y in range(h):
        for x in range(w):
            P = p[y][x]
            A, D = p[max(y - 1, 0)][x], p[min(y + 1, h - 1)][x]
            C, B = p[y][max(x - 1, 0)], p[y][min(x + 1, w - 1)]
            out[2 * y][2 * x] = A if C == A and C != D and A != B else P
            out[2 * y][2 * x + 1] = B if A == B and A != C and B != D else P
            out[2 * y + 1][2 * x] = C if D == C and D != B and C != A else P
            out[2 * y + 1][2 * x + 1] = D if B == D and B != A and D != C else P
    return 2 * w, 2 * h, out


def write_png(path, w, h, p):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    raw = b''.join(b'\0' + b''.join(row) for row in p)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    files = [f for a in sys.argv[1:] for f in glob.glob(a)]
    if not files:
        sys.exit(__doc__)
    for f in files:
        img = scale2x(*scale2x(*read_dump(f)))
        out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(f))), os.path.basename(f))
        write_png(out, *img)
        print('%s (%dx%d)' % (out, img[0], img[1]))


if __name__ == '__main__':
    main()

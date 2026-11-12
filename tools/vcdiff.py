#!/usr/bin/env python3
"""Apply a VCDIFF patch (RFC 3284, the .xdelta files xdelta3 makes) without xdelta3: the default code
table, no secondary compression (xdelta3 -S none, as translation patches usually are), with xdelta3's
Adler-32 window checksums. Used by build.py --lang to make the translated disc image.

usage: python tools/vcdiff.py SOURCE PATCH OUTPUT
"""
import sys, zlib


def _code_table():
    """(type1, size1, mode1, type2, size2, mode2) for each of the 256 instruction codes;
    types: 0 none, 1 ADD, 2 RUN, 3 COPY (RFC 3284 section 5.6)"""
    t = [(2, 0, 0, 0, 0, 0)]
    t += [(1, s, 0, 0, 0, 0) for s in range(18)]
    for m in range(9):
        t += [(3, 0, m, 0, 0, 0)] + [(3, s, m, 0, 0, 0) for s in range(4, 19)]
    for m in range(6):
        t += [(1, a, 0, 3, c, m) for a in range(1, 5) for c in range(4, 7)]
    for m in range(6, 9):
        t += [(1, a, 0, 3, 4, m) for a in range(1, 5)]
    t += [(3, 4, m, 1, 1, 0) for m in range(9)]
    assert len(t) == 256
    return t


TABLE = _code_table()


class _Reader:
    def __init__(self, data):
        self.b, self.p = data, 0

    def byte(self):
        self.p += 1
        return self.b[self.p - 1]

    def int(self):
        v = 0
        while True:
            c = self.byte()
            v = (v << 7) | (c & 0x7F)
            if not c & 0x80:
                return v

    def take(self, n):
        self.p += n
        return self.b[self.p - n:self.p]


def apply(source, patch):
    """the target that patch makes from source (bytes)"""
    p = _Reader(patch)
    if p.take(4) != b'\xd6\xc3\xc4\x00':
        raise ValueError('not a VCDIFF (.xdelta) patch')
    hdr = p.byte()
    if hdr & 3:
        raise ValueError('this patch uses a secondary compressor or its own code table, which is not supported')
    if hdr & 4:
        p.take(p.int())                                 # application header (file names)
    out = bytearray()
    while p.p < len(p.b):
        win = p.byte()
        seg, slen = b'', 0
        if win & 3:
            slen, spos = p.int(), p.int()
            seg = source[spos:spos + slen] if win & 1 else bytes(out[spos:spos + slen])
        dlen = p.int()
        dstart = p.p
        tlen = p.int()
        if p.byte():
            raise ValueError('compressed patch sections are not supported')
        ldata, linst, laddr = p.int(), p.int(), p.int()
        cksum = int.from_bytes(p.take(4), 'big') if win & 4 else None
        data, inst, addr = _Reader(p.take(ldata)), _Reader(p.take(linst)), _Reader(p.take(laddr))
        if p.p != dstart + dlen:
            raise ValueError('damaged patch (window length)')
        tgt = bytearray()
        near, same, ni = [0] * 4, [0] * 768, 0

        def copy_addr(mode, here):
            nonlocal ni
            if mode == 0:
                a = addr.int()
            elif mode == 1:
                a = here - addr.int()
            elif mode < 6:
                a = near[mode - 2] + addr.int()
            else:
                a = same[(mode - 6) * 256 + addr.byte()]
            near[ni] = a
            ni = (ni + 1) % 4
            same[a % 768] = a
            return a

        def run(kind, size, mode):
            if not kind:
                return
            if not size:
                size = inst.int()
            if kind == 1:
                tgt.extend(data.take(size))
            elif kind == 2:
                tgt.extend(data.take(1) * size)
            else:
                a = copy_addr(mode, slen + len(tgt))
                if a + size <= slen:
                    tgt.extend(seg[a:a + size])
                else:                                   # reaches into the target being made
                    for i in range(size):
                        x = a + i
                        tgt.append(seg[x] if x < slen else tgt[x - slen])

        while inst.p < len(inst.b):
            k1, s1, m1, k2, s2, m2 = TABLE[inst.byte()]
            run(k1, s1, m1)
            run(k2, s2, m2)
        if len(tgt) != tlen:
            raise ValueError('damaged patch (window size)')
        if cksum is not None and zlib.adler32(bytes(tgt)) != cksum:
            raise ValueError('the patch does not fit this file (checksum)')
        out += tgt
    return bytes(out)


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    with open(sys.argv[1], 'rb') as f:
        src = f.read()
    with open(sys.argv[2], 'rb') as f:
        res = apply(src, f.read())
    with open(sys.argv[3], 'wb') as f:
        f.write(res)
    print('%s: %d bytes' % (sys.argv[3], len(res)))

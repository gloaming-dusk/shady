#!/usr/bin/env python3
"""png-compare.py A B: exit 0 when two 8-bit RGB(A) PNGs have the same pixels.
Otherwise print how many differ and the box around them."""
import struct
import sys
import zlib


def decode(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", f"{path}: not a PNG"
    pos, idat, width = 8, b"", None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour = struct.unpack(">IIBB", chunk[:10])
            assert depth == 8 and colour in (2, 6), f"{path}: unsupported PNG"
            bpp = 4 if colour == 6 else 3
        elif kind == b"IDAT":
            idat += chunk
    raw = zlib.decompress(idat)
    stride = width * bpp
    rows, prev = [], bytearray(stride)
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return width, height, bpp, rows


def main():
    wa, ha, ba, ra = decode(sys.argv[1])
    wb, hb, bb, rb = decode(sys.argv[2])
    if (wa, ha, ba) != (wb, hb, bb):
        print(f"png-compare: sizes differ: {wa}x{ha} vs {wb}x{hb}")
        return 1
    count, box = 0, [wa, ha, -1, -1]
    for y in range(ha):
        if ra[y] == rb[y]:
            continue
        for x in range(wa):
            if ra[y][x * ba:(x + 1) * ba] != rb[y][x * bb:(x + 1) * bb]:
                count += 1
                box = [min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y)]
    if count:
        print(f"png-compare: {count} pixels differ, within x={box[0]}..{box[2]} y={box[1]}..{box[3]}")
        return 1
    return 0


sys.exit(main())

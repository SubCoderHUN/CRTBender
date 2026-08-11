#!/usr/bin/env python3
"""Generates res/app.ico for CRTBender.

No third-party dependencies: the shape is evaluated analytically with 3x3
supersampling, then packed into an .ico (BMP entries for the small sizes, a PNG
entry for 256x256 so the file stays a sane size).

The icon shows four lattice lines, the topmost one bowed upwards - which is
exactly the defect the program exists to cancel out.
"""

import os
import struct
import zlib

SIZES = [16, 24, 32, 48, 64, 128, 256]
SS = 3  # supersampling factor per axis

BG = (26, 29, 35)
GREEN = (86, 208, 130)
AMBER = (245, 176, 66)

LINE_Y = [0.30, 0.47, 0.64, 0.81]
LINE_X0, LINE_X1 = 0.17, 0.83
LINE_HALF = 0.030
BOW_AMPLITUDE = 0.085
BOW_HALF_WIDTH = 0.34


def inside_rounded(x, y, lo, hi, r):
    if x < lo or x > hi or y < lo or y > hi:
        return False
    cx = min(max(x, lo + r), hi - r)
    cy = min(max(y, lo + r), hi - r)
    dx, dy = x - cx, y - cy
    return dx * dx + dy * dy <= r * r


def shade(fx, fy):
    """Returns (r, g, b, a) for a point in the unit square."""
    if not inside_rounded(fx, fy, 0.04, 0.96, 0.20):
        return (0, 0, 0, 0)

    color = BG
    for index, base in enumerate(LINE_Y):
        y = base
        if index == 0:
            t = (fx - 0.5) / BOW_HALF_WIDTH
            if abs(t) <= 1.0:
                y = base - BOW_AMPLITUDE * (1.0 - t * t)
        if LINE_X0 <= fx <= LINE_X1 and abs(fy - y) <= LINE_HALF:
            color = AMBER if index == 0 else GREEN
    return color + (255,)


def render(size):
    """Returns a row-major list of (r, g, b, a) tuples, top row first."""
    pixels = []
    step = 1.0 / (size * SS)
    for py in range(size):
        for px in range(size):
            r = g = b = a = 0
            for sy in range(SS):
                for sx in range(SS):
                    fx = (px * SS + sx + 0.5) * step
                    fy = (py * SS + sy + 0.5) * step
                    cr, cg, cb, ca = shade(fx, fy)
                    # Premultiply so transparent samples do not darken the edge.
                    r += cr * ca
                    g += cg * ca
                    b += cb * ca
                    a += ca
            n = SS * SS
            if a == 0:
                pixels.append((0, 0, 0, 0))
            else:
                pixels.append((round(r / a), round(g / a), round(b / a), round(a / n)))
    return pixels


def bmp_entry(size, pixels):
    """32bpp BITMAPINFOHEADER + bottom-up BGRA + a padded AND mask."""
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0,
                         0, 0, 0, 0, 0)
    xor = bytearray()
    for row in range(size - 1, -1, -1):
        for col in range(size):
            r, g, b, a = pixels[row * size + col]
            xor += bytes((b, g, r, a))

    mask_stride = ((size + 31) // 32) * 4
    and_mask = bytearray()
    for row in range(size - 1, -1, -1):
        bits = bytearray(mask_stride)
        for col in range(size):
            if pixels[row * size + col][3] == 0:
                bits[col // 8] |= 0x80 >> (col % 8)
        and_mask += bits

    return header + bytes(xor) + bytes(and_mask)


def png_entry(size, pixels):
    raw = bytearray()
    for row in range(size):
        raw.append(0)  # filter type: none
        for col in range(size):
            raw += bytes(pixels[row * size + col])

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload +
                struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def main():
    images = []
    for size in SIZES:
        pixels = render(size)
        blob = png_entry(size, pixels) if size >= 128 else bmp_entry(size, pixels)
        images.append((size, blob))

    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    offset = 6 + 16 * len(images)
    for size, blob in images:
        dim = 0 if size >= 256 else size
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    for _, blob in images:
        out += blob

    target = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "app.ico")
    target = os.path.normpath(target)
    os.makedirs(os.path.dirname(target), exist_ok=True)
    with open(target, "wb") as handle:
        handle.write(out)
    print(f"wrote {target} ({len(out)} bytes, {len(images)} sizes)")


if __name__ == "__main__":
    main()

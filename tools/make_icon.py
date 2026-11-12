# SPDX-License-Identifier: GPL-3.0-or-later
"""Draws the SoundFunnel app icon (resources/icon.png) with the standard library only.

A rounded blue square with a white funnel: three small "sound" dots (the client PCs)
above the funnel mouth, one stream leaving the spout (the server).
Run:  python tools/make_icon.py
"""
import math
import os
import struct
import zlib

SIZE = 1024          # output size
SS = 3               # supersampling per axis (anti-aliasing)


def rounded_rect(x, y, s, r):
    # inside test for a rounded square [0,s]^2 with corner radius r
    cx = min(max(x, r), s - r)
    cy = min(max(y, r), s - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def in_poly(x, y, pts):
    inside = False
    n = len(pts)
    j = n - 1
    for i in range(n):
        xi, yi = pts[i]
        xj, yj = pts[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi:
            inside = not inside
        j = i
    return inside


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def shade(u, v):
    """Colour (r, g, b, a) at normalised coordinates u, v in [0, 1]."""
    s = 1.0
    if not rounded_rect(u, v, s, 0.2):
        return (0, 0, 0, 0)

    # background: vertical gradient
    col = lerp((52, 120, 246), (24, 62, 168), v)

    # funnel body: wide mouth narrowing into a spout
    funnel = [(0.20, 0.40), (0.80, 0.40), (0.56, 0.64), (0.56, 0.80),
              (0.44, 0.80), (0.44, 0.64)]
    if in_poly(u, v, funnel):
        col = (255, 255, 255)
    # rim of the mouth
    if 0.36 <= v <= 0.42 and 0.17 <= u <= 0.83:
        col = (255, 255, 255)

    # three sources above the mouth
    for cx, c in ((0.30, (255, 196, 61)), (0.50, (98, 222, 148)), (0.70, (255, 120, 140))):
        if (u - cx) ** 2 + (v - 0.24) ** 2 <= 0.065 ** 2:
            col = c

    # the stream out of the spout
    if 0.475 <= u <= 0.525 and 0.80 <= v <= 0.88:
        col = (255, 255, 255)
    return col + (255,)


def render():
    rows = []
    n = SS * SS
    for y in range(SIZE):
        row = bytearray()
        for x in range(SIZE):
            r = g = b = a = 0
            for sy in range(SS):
                for sx in range(SS):
                    u = (x + (sx + 0.5) / SS) / SIZE
                    v = (y + (sy + 0.5) / SS) / SIZE
                    cr, cg, cb, ca = shade(u, v)
                    r += cr * ca
                    g += cg * ca
                    b += cb * ca
                    a += ca
            if a:
                row += bytes((r // a, g // a, b // a, a // n))
            else:
                row += b"\0\0\0\0"
        rows.append(bytes(row))
    return rows


def write_png(path, rows):
    raw = b"".join(b"\0" + r for r in rows)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", ihdr))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources", "icon.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    write_png(out, render())
    print("wrote", os.path.normpath(out))

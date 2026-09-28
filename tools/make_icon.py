"""Draws the Peekabyte app icon (app/icon.png): two eyes peeking over a ledge.

Pure Python, no image libraries: every pixel is supersampled 3x3 and written as a PNG.
"""
import struct
import zlib
from pathlib import Path

SIZE = 512
SS = 3
OUT = Path(__file__).resolve().parent.parent / "app" / "icon.png"


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def in_rrect(x, y, x0, y0, x1, y1, r):
    if x < x0 or x > x1 or y < y0 or y > y1:
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def in_ellipse(x, y, cx, cy, rx, ry):
    return ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1


def shade(x, y):
    # background: vertical gradient, deep navy to violet
    t = y / SIZE
    col = lerp((22, 28, 74), (64, 34, 110), t)
    # soft glow behind the eyes
    d = ((x - 256) ** 2 + (y - 230) ** 2) ** 0.5
    if d < 230:
        col = lerp(col, (70, 140, 220), (1 - d / 230) ** 2 * 0.45)
    for ex in (178, 334):
        if in_ellipse(x, y, ex, 238, 74, 92):
            col = (236, 247, 255)
            if in_ellipse(x, y, ex + 12, 250, 34, 36):
                col = (10, 12, 24)
                if in_ellipse(x, y, ex + 0, 236, 11, 11) or in_ellipse(x, y, ex + 26, 268, 5, 5):
                    col = (255, 255, 255)
    # the ledge the eyes peek over, with a glowing rim
    if in_rrect(x, y, 34, 300, 478, 560, 60):
        col = (20, 24, 58)
        if not in_rrect(x, y, 44, 310, 468, 560, 52):
            col = lerp((94, 231, 255), (181, 140, 255), x / SIZE)
    return col


def main():
    rows = []
    for y in range(SIZE):
        row = bytearray(b"\x00")
        for x in range(SIZE):
            r = g = b = 0
            for sy in range(SS):
                for sx in range(SS):
                    c = shade(x + (sx + 0.5) / SS, y + (sy + 0.5) / SS)
                    r += c[0]
                    g += c[1]
                    b += c[2]
            n = SS * SS
            row += bytes((r // n, g // n, b // n))
        rows.append(bytes(row))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b""))
    OUT.write_bytes(png)
    print("wrote", OUT, len(png), "bytes")


if __name__ == "__main__":
    main()

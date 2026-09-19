"""Genera res/app.ico (impresora minimalista) sin dependencias externas.

Uso:  python scripts/make_icon.py
Dibuja con supermuestreo (antialias) en 16, 24, 32, 48, 64 y 256 px y escribe
un .ico con imágenes PNG embebidas.
"""
import os
import struct
import zlib

ACCENT = (36, 88, 214)      # azul de la paleta clara (#2458D6)
BODY = (38, 42, 51)         # cuerpo de la impresora
PAPER = (255, 255, 255)
SLOT = (20, 22, 27)


def rounded_rect_cov(x, y, r0, r1, radius):
    """Cobertura (0/1) de un punto dentro de un rectángulo redondeado."""
    x0, y0 = r0
    x1, y1 = r1
    if x < x0 or x > x1 or y < y0 or y > y1:
        return 0
    cx = min(max(x, x0 + radius), x1 - radius)
    cy = min(max(y, y0 + radius), y1 - radius)
    return 1 if (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2 else 0


def shapes():
    """Formas en coordenadas normalizadas 0..1 (orden de pintado)."""
    return [
        # hoja que entra (arriba)
        (PAPER, (0.28, 0.08), (0.72, 0.40), 0.03, True),
        # cuerpo
        (BODY, (0.08, 0.32), (0.92, 0.74), 0.10, False),
        # franja de acento
        (ACCENT, (0.08, 0.32), (0.92, 0.42), 0.06, False),
        # ranura de salida
        (SLOT, (0.20, 0.60), (0.80, 0.66), 0.02, False),
        # hoja que sale (abajo)
        (PAPER, (0.26, 0.63), (0.74, 0.94), 0.03, True),
        # líneas de texto en la hoja
        (ACCENT, (0.34, 0.74), (0.66, 0.78), 0.01, False),
        (ACCENT, (0.34, 0.82), (0.58, 0.86), 0.01, False),
    ]


def render(size, ss=4):
    pixels = []
    for py in range(size):
        row = []
        for px in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    x = (px + (sx + 0.5) / ss) / size
                    y = (py + (sy + 0.5) / ss) / size
                    color = None
                    for fill, a, b, rad, outlined in shapes():
                        if rounded_rect_cov(x, y, a, b, rad):
                            color = fill
                            # borde fino de la hoja para que se vea sobre fondos claros
                            if outlined and size >= 24:
                                edge = 0.018
                                inner = rounded_rect_cov(x, y, (a[0] + edge, a[1] + edge),
                                                         (b[0] - edge, b[1] - edge), max(rad - edge, 0))
                                if not inner:
                                    color = (150, 156, 168)
                    if color:
                        acc[0] += color[0]
                        acc[1] += color[1]
                        acc[2] += color[2]
                        acc[3] += 1
            n = ss * ss
            if acc[3] == 0:
                row.append((0, 0, 0, 0))
            else:
                row.append((int(acc[0] / acc[3]), int(acc[1] / acc[3]), int(acc[2] / acc[3]),
                            int(255 * acc[3] / n)))
        pixels.append(row)
    return pixels


def png_bytes(pixels):
    size = len(pixels)
    raw = b"".join(b"\x00" + bytes(c for p in row for c in p) for row in pixels)

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    sizes = [16, 24, 32, 48, 64, 256]
    images = [png_bytes(render(s, 4 if s <= 64 else 2)) for s in sizes]
    out = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    for s, data in zip(sizes, images):
        dim = 0 if s == 256 else s
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    out += b"".join(images)
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    path = os.path.join(root, "res", "app.ico")
    with open(path, "wb") as f:
        f.write(out)
    print("Escrito", path, len(out), "bytes")


if __name__ == "__main__":
    main()

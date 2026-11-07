"""Genera res/app.ico (impresora minimalista) sin dependencias externas.

Uso:  python scripts/make_icon.py
Dibuja con supermuestreo (antialias) en 16, 24, 32, 48, 64 y 256 px y escribe
un .ico con imágenes PNG embebidas.

El ícono resultante se incluye en el ejecutable desde res/app.rc (recurso 1)
y lo usan la ventana, la bandeja y el diálogo "Acerca de". Solo hace falta
volver a correr este script si se cambia el diseño; res/app.ico ya está en
el repositorio.

Todo se hace con la biblioteca estándar: el dibujo es un rasterizador de
rectángulos redondeados, el PNG se arma a mano (struct + zlib) y el .ico es
un encabezado ICONDIR con una entrada por tamaño que apunta a cada PNG.
"""
import os
import struct
import zlib

# Colores (R, G, B) del dibujo.
ACCENT = (36, 88, 214)      # azul de la paleta clara (#2458D6)
BODY = (38, 42, 51)         # cuerpo de la impresora
PAPER = (255, 255, 255)     # hojas de papel
SLOT = (20, 22, 27)         # ranura de salida (más oscura que el cuerpo)


def rounded_rect_cov(x, y, r0, r1, radius):
    """Cobertura (0/1) de un punto dentro de un rectángulo redondeado."""
    x0, y0 = r0
    x1, y1 = r1
    # Fuera de la caja que lo contiene: seguro que no.
    if x < x0 or x > x1 or y < y0 or y > y1:
        return 0
    # Punto más cercano del rectángulo "interior" (encogido en el radio). Si
    # (x, y) está a menos de `radius` de él, cae dentro de la forma; en las
    # esquinas eso dibuja el cuarto de círculo y en los lados es siempre cierto.
    cx = min(max(x, x0 + radius), x1 - radius)
    cy = min(max(y, y0 + radius), y1 - radius)
    return 1 if (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2 else 0


def shapes():
    """Formas en coordenadas normalizadas 0..1 (orden de pintado)."""
    # Cada forma: (color, esquina superior izquierda, esquina inferior
    # derecha, radio de las esquinas, con borde). Las posteriores tapan a las
    # anteriores (la última que cubre un punto define su color).
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
    """Dibuja el ícono de size×size px; devuelve filas de píxeles RGBA.

    ss = supermuestreo: cada píxel se evalúa en ss×ss puntos y se promedia,
    lo que suaviza los bordes (antialias). La transparencia (alfa) es la
    fracción de muestras que cayeron sobre alguna forma.
    """
    pixels = []
    for py in range(size):
        row = []
        for px in range(size):
            # Suma de R, G, B de las muestras cubiertas y cuántas lo fueron.
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    # Centro de la submuestra, en coordenadas normalizadas 0..1.
                    x = (px + (sx + 0.5) / ss) / size
                    y = (py + (sy + 0.5) / ss) / size
                    color = None
                    for fill, a, b, rad, outlined in shapes():
                        if rounded_rect_cov(x, y, a, b, rad):
                            color = fill
                            # borde fino de la hoja para que se vea sobre fondos claros
                            # (en 16 px no: se comería la hoja entera).
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
                # Ninguna muestra cubierta: píxel totalmente transparente.
                row.append((0, 0, 0, 0))
            else:
                # Color promedio de las muestras cubiertas (sin mezclar con el
                # fondo: eso lo hace Windows con el alfa) y alfa = cobertura.
                row.append((int(acc[0] / acc[3]), int(acc[1] / acc[3]), int(acc[2] / acc[3]),
                            int(255 * acc[3] / n)))
        pixels.append(row)
    return pixels


def png_bytes(pixels):
    """Codifica las filas RGBA como un archivo PNG (8 bits por canal)."""
    size = len(pixels)
    # Datos crudos: cada fila empieza con el byte de filtro 0 (sin filtro)
    # seguido de los bytes R, G, B, A de cada píxel.
    raw = b"".join(b"\x00" + bytes(c for p in row for c in p) for row in pixels)

    def chunk(kind, data):
        # Bloque PNG: longitud (big endian), tipo, datos y CRC32 de tipo + datos.
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    # IHDR: ancho, alto, 8 bits, tipo de color 6 (RGBA), compresión,
    # filtro y entrelazado por defecto (0).
    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    # Firma PNG + IHDR + IDAT (datos comprimidos con zlib) + IEND.
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    sizes = [16, 24, 32, 48, 64, 256]
    # 256 px con supermuestreo 2 (no 4) para que el script no tarde demasiado:
    # a ese tamaño los bordes ya se ven suaves.
    images = [png_bytes(render(s, 4 if s <= 64 else 2)) for s in sizes]
    # ICONDIR: reservado (0), tipo 1 = ícono, cantidad de imágenes.
    out = struct.pack("<HHH", 0, 1, len(images))
    # Los PNG van después del ICONDIR (6 bytes) y de una ICONDIRENTRY de 16
    # bytes por imagen.
    offset = 6 + 16 * len(images)
    for s, data in zip(sizes, images):
        # ICONDIRENTRY: ancho y alto en un byte (0 significa 256), colores de
        # paleta (0), reservado, planos (1), bits por píxel (32), tamaño de
        # los datos y su posición en el archivo.
        dim = 0 if s == 256 else s
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    out += b"".join(images)
    # La raíz del repositorio es la carpeta padre de scripts/.
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    path = os.path.join(root, "res", "app.ico")
    with open(path, "wb") as f:
        f.write(out)
    print("Escrito", path, len(out), "bytes")


if __name__ == "__main__":
    main()

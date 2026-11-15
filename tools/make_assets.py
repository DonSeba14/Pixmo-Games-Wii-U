"""Genera el icono y las pantallas de carga del .wuhb de Pixmo Games, con la
marca de Pixmo Games (assets/fuente/): la mascota, la letra de píxeles 5x7 y el
fondo neón.

Salida: assets/icon.png (128x128), assets/splash_tv.png (1280x720),
assets/splash_drc.png (854x480) y src/pixmo_icono.h (el icono en PNG dentro
del programa, para la barra superior). Uso: py -3 tools/make_assets.py
"""
import io
import math
import os
import random

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "assets")
SRC = os.path.join(OUT, "fuente")

# Paleta de Pixmo Games (Proyecto/src/Interfaz/Ficha.cs)
NOCHE = (13, 2, 33)
ROSA = (255, 42, 109)
CIAN = (5, 217, 232)
HIELO = (209, 247, 255)
VIOLETA = (119, 0, 255)

# Letras de 5x7 píxeles, como los ojos de Pixmo (Proyecto/src/Interfaz/Marca.cs)
GLIFOS = {
    "P": ["XXXX.", "X...X", "X...X", "XXXX.", "X....", "X....", "X...."],
    "I": ["XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "XXXXX"],
    "X": ["X...X", "X...X", ".X.X.", "..X..", ".X.X.", "X...X", "X...X"],
    "M": ["X...X", "XX.XX", "X.X.X", "X.X.X", "X...X", "X...X", "X...X"],
    "O": [".XXX.", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX."],
    "G": [".XXX.", "X...X", "X....", "X.XXX", "X...X", "X...X", ".XXXX"],
    "A": [".XXX.", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X"],
    "E": ["XXXXX", "X....", "X....", "XXXX.", "X....", "X....", "XXXXX"],
    "S": [".XXXX", "X....", "X....", ".XXX.", "....X", "....X", "XXXX."],
    " ": ["....."] * 7,
}


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def pixel_text(d, text, x, y, cell, colors):
    """Draws text with the 5x7 pixel letters; colors[i] for letter i."""
    for i, ch in enumerate(text):
        g = GLIFOS[ch]
        for row in range(7):
            for col in range(5):
                if g[row][col] == "X":
                    px, py = x + (i * 6 + col) * cell, y + row * cell
                    d.rectangle([px, py, px + cell - 1, py + cell - 1], fill=colors[i])


def neon_background(w, h, sol=0.5):
    """The loading-screen background of Pixmo Games (FondoNeon): night sky, stars,
    striped sun, pink perspective grid and a cyan horizon."""
    k = h / 720
    img = Image.new("RGB", (w, h), NOCHE)
    d = ImageDraw.Draw(img, "RGBA")
    hz = int(h * 0.64)
    top, bot = NOCHE, mix(NOCHE, VIOLETA, 0.45)
    for y in range(hz):
        d.line([(0, y), (w, y)], fill=mix(top, bot, y / hz))
    g0, g1 = mix(NOCHE, VIOLETA, 0.25), NOCHE
    for y in range(hz, h):
        d.line([(0, y), (w, y)], fill=mix(g0, g1, (y - hz) / max(1, h - hz)))
    rnd = random.Random(7)
    for _ in range(90):
        sx, sy, ph = rnd.random() * w, rnd.random() * hz * 0.92, rnd.random() * math.pi * 2
        a = 0.25 + 0.55 * (0.5 + 0.5 * math.sin(ph))
        s = max(2, round(3 * k))
        d.rectangle([sx, sy, sx + s - 1, sy + s - 1], fill=HIELO + (int(a * 255),))
    cx, cy, r = w / 2, hz - 10 * k, h * 0.26
    for kk in range(6, 0, -1):
        rr = r + kk * 18 * k
        d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=ROSA + (int(0.025 * sol * 255),))
    y = -r
    step, period = 4 * k, 26 * k
    while y < 0:
        rel = (y + r) / r
        gap = (rel - 0.45) * 22 * k if rel > 0.45 else 0
        if (y + r) % period >= gap:
            half = math.sqrt(max(0, r * r - y * y))
            col = mix(ROSA, VIOLETA, rel * 0.8)
            d.rectangle([cx - half, cy + y, cx + half, cy + y + step], fill=col + (int(sol * 255),))
        y += step
    for i in range(-16, 17):
        x = w / 2 + i * w * 0.13
        d.line([(w / 2, hz), (x, h)], fill=ROSA + (140,), width=max(1, round(2 * k)))
    for i in range(14):
        z = i / 14
        yy = hz + (h - hz) * z * z
        d.line([(0, yy), (w, yy)], fill=ROSA + (int((0.15 + 0.6 * z) * 255),), width=max(1, round((1 + 2 * z) * k)))
    d.line([(0, hz), (w, hz)], fill=CIAN + (217,), width=max(2, round(3 * k)))
    return img


def make_icon():
    logo = Image.open(os.path.join(SRC, "pixmo_logo.png")).convert("RGB")
    return logo.resize((128, 128), Image.LANCZOS)


def make_splash(w, h):
    bg = neon_background(w, h, sol=0.45).convert("RGBA")
    # the mascot (rounded icon) over the sun, with a soft violet glow behind it
    ic = Image.open(os.path.join(SRC, "pixmo_icono.png")).convert("RGBA")
    size = int(h * 0.34)
    ic = ic.resize((size, size), Image.LANCZOS)
    x, y = (w - size) // 2, int(h * 0.1)
    glow = Image.new("RGBA", bg.size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    pad = int(size * 0.18)
    gd.rounded_rectangle([x - pad, y - pad, x + size + pad, y + size + pad], radius=size // 3,
                         fill=VIOLETA + (150,))
    glow = glow.filter(ImageFilter.GaussianBlur(size // 6))
    bg = Image.alpha_composite(bg, glow)
    bg.alpha_composite(ic, (x, y))
    # PIXMO GAMES in pixel letters, with a dark drop shadow so the grid does not hide it
    d = ImageDraw.Draw(bg)
    cell = max(4, round(h * 0.0135))
    text = "PIXMO GAMES"
    tw = (len(text) * 6 - 1) * cell
    tx, ty = (w - tw) // 2, int(h * 0.53)
    shadow = Image.new("RGBA", bg.size, (0, 0, 0, 0))
    pixel_text(ImageDraw.Draw(shadow), text, tx + cell, ty + cell, cell, [NOCHE + (230,)] * len(text))
    shadow = shadow.filter(ImageFilter.GaussianBlur(cell * 0.6))
    bg = Image.alpha_composite(bg, shadow)
    d = ImageDraw.Draw(bg)
    pixel_text(d, text, tx, ty, cell, [HIELO] * 5 + [HIELO] + [CIAN] * 5)
    # no tagline: the console shows this image as is, whatever its language
    return bg.convert("RGB")


def write_icon_header(path):
    """The rounded icon as a PNG inside the program (brand.c)."""
    ic = Image.open(os.path.join(SRC, "pixmo_icono.png")).convert("RGBA").resize((160, 160), Image.LANCZOS)
    buf = io.BytesIO()
    ic.save(buf, "PNG", optimize=True)
    data = buf.getvalue()
    lines = ["// Generado por tools/make_assets.py: icono de Pixmo Games (PNG 160x160).",
             "#pragma once", "", f"static const unsigned char k_pixmo_icono_png[{len(data)}] = {{"]
    for i in range(0, len(data), 20):
        lines.append("    " + ", ".join(str(b) for b in data[i:i + 20]) + ",")
    lines.append("};")
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return len(data)


def main():
    os.makedirs(OUT, exist_ok=True)
    make_icon().save(os.path.join(OUT, "icon.png"))
    make_splash(1280, 720).save(os.path.join(OUT, "splash_tv.png"))
    make_splash(854, 480).save(os.path.join(OUT, "splash_drc.png"))
    n = write_icon_header(os.path.join(ROOT, "src", "pixmo_icono.h"))
    print("assets listos en", OUT, f"(icono embebido: {n} bytes)")


if __name__ == "__main__":
    main()

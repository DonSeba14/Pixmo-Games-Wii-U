"""Genera títulos de prueba para la versión de PC de Pixmo Games.

Crea pcdata/titles/<titleid>/meta/{meta.xml,iconTex.tga,bootTvTex.tga} con
juegos ficticios (nombres y arte inventados) para probar la interfaz sin
consola. Uso:  py -3 tools/fake_titles.py [carpeta pcdata]
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "pcdata")
FONT = os.path.join(ROOT, "CafeStd.ttf")

GAMES = [
    # (title id low, name, publisher, product code, colors, device, shape, update version, dlc)
    ("10100A00", "Galaxia Turbo", "Estudio Cometa", "WUP-P-AGTE", ((60, 40, 160), (20, 160, 230)), "mlc", "star", 32, False),
    ("10101B00", "Bosque Encantado", "Hojas Games", "WUP-P-ABEE", ((30, 120, 60), (170, 220, 90)), "usb", "circle", 0, False),
    ("10102C00", "Rally de Montaña", "Polvo Racing", "WUP-P-ARME", ((200, 90, 20), (250, 200, 60)), "usb", "tri", 80, True),
    ("10103D00", "Escuadrón Tinta", "Splat Works", "WUP-P-AETE", ((220, 30, 140), (250, 230, 40)), "mlc", "blob", 288, True),
    ("10104E00", "Leyendas del Mar", "Ola Azul", "WUP-P-ALME", ((10, 90, 120), (60, 200, 200)), "usb", "wave", 0, False),
    ("10105F00", "Pixel Ninja", "8 Bits Co.", "WUP-N-APNE", ((150, 20, 30), (240, 80, 60)), "mlc", "square", 16, False),
    ("10106000", "Castillo de Cristal", "Prisma", "WUP-P-ACCE", ((110, 90, 200), (220, 200, 250)), "usb", "tri", 0, False),
    ("10107100", "Fiesta de Monstruos", "Risas Studio", "WUP-P-AFME", ((230, 80, 150), (255, 190, 220)), "usb", "blob", 0, True),
    ("10108200", "Fútbol Estelar", "Deportes U", "WUP-P-AFEE", ((20, 120, 40), (120, 200, 80)), "mlc", "circle", 48, False),
    ("10109300", "Detective Búho", "Misterio Lab", "WUP-N-ADBE", ((90, 60, 40), (210, 170, 110)), "usb", "circle", 0, False),
    ("1010A400", "Robots en la Nieve", "Frío Digital", "WUP-P-ARNE", ((60, 120, 180), (220, 240, 255)), "usb", "square", 0, False),
    ("1010B500", "Volcán Express", "Lava Games", "WUP-P-AVEE", ((160, 20, 10), (255, 140, 0)), "mlc", "tri", 0, False),
    ("1010C600", "Jardín Zen", "Calma", "WUP-N-AJZE", ((90, 140, 90), (230, 240, 210)), "usb", "circle", 0, False),
    ("1010D700", "Ciudad Neón", "Synth Corp", "WUP-P-ACNE", ((30, 10, 60), (240, 40, 200)), "usb", "square", 64, False),
    ("101C9500", "Super Plataformas U", "Prueba de descarga", "WUP-P-ARPE", ((240, 200, 40), (230, 60, 40)), "mlc", "star", 208, True),
    ("1010E800", "Ajedrez Real", "Tablero", "WUP-N-AARE", ((70, 70, 80), (200, 200, 210)), "usb", "square", 0, False),
    ("1010F900", "Batalla de Globos", "Nube 9", "WUP-P-ABGE", ((40, 140, 230), (180, 230, 255)), "usb", "circle", 0, False),
    ("10110A00", "Dragones de Papel", "Origami Soft", "WUP-P-ADPE", ((170, 20, 40), (250, 200, 80)), "mlc", "tri", 0, False),
    ("10111B00", "Las Crónicas del Reino Olvidado\nEdición Definitiva", "Gran Saga", "WUP-P-ACRE", ((40, 30, 80), (200, 160, 60)), "usb", "star", 112, True),
    ("10112C00", "Karts Galácticos", "Estudio Cometa", "WUP-P-AKGE", ((20, 20, 90), (60, 220, 160)), "usb", "circle", 0, False),
]


def font(size):
    try:
        return ImageFont.truetype(FONT, size)
    except OSError:
        return ImageFont.load_default()


def gradient(w, h, c1, c2, diag=True):
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            t = ((x / w) * 0.45 + (y / h) * 0.55) if diag else y / h
            px[x, y] = tuple(int(c1[i] + (c2[i] - c1[i]) * t) for i in range(3))
    return img


def shape(draw, kind, cx, cy, r, fill):
    if kind == "circle":
        draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=fill)
    elif kind == "square":
        draw.rounded_rectangle([cx - r, cy - r, cx + r, cy + r], radius=r // 4, fill=fill)
    elif kind == "tri":
        draw.polygon([(cx, cy - r), (cx + r, cy + r * 0.8), (cx - r, cy + r * 0.8)], fill=fill)
    elif kind == "star":
        pts = []
        for i in range(10):
            a = -math.pi / 2 + i * math.pi / 5
            rr = r if i % 2 == 0 else r * 0.45
            pts.append((cx + math.cos(a) * rr, cy + math.sin(a) * rr))
        draw.polygon(pts, fill=fill)
    elif kind == "wave":
        for k in range(3):
            draw.arc([cx - r, cy - r + k * r * 0.4, cx + r, cy + r + k * r * 0.4], 200, 340, fill=fill, width=int(max(4, r // 6)))
    else:  # blob
        for k in range(6):
            a = k * math.pi / 3
            draw.ellipse([cx + math.cos(a) * r * 0.5 - r * 0.55, cy + math.sin(a) * r * 0.5 - r * 0.55,
                          cx + math.cos(a) * r * 0.5 + r * 0.55, cy + math.sin(a) * r * 0.5 + r * 0.55], fill=fill)


def make_icon(name, colors, kind):
    s = 512
    img = gradient(s, s, colors[0], colors[1])
    d = ImageDraw.Draw(img)
    shape(d, kind, s * 0.5, s * 0.42, s * 0.3, (255, 255, 255))
    shape(d, kind, s * 0.5, s * 0.42, s * 0.2, colors[0])
    words = name.replace("\n", " ").split()
    initials = "".join(w[0] for w in words if w[0].isalpha())[:2].upper()
    f = font(150)
    d.text((s * 0.5, s * 0.82), initials, font=f, fill=(255, 255, 255), anchor="mm",
           stroke_width=8, stroke_fill=colors[0])
    return img.resize((128, 128), Image.LANCZOS).convert("RGBA")


def make_boot(name, colors, kind, seed):
    rnd = random.Random(seed)
    w, h = 1280, 720
    img = gradient(w, h, colors[0], colors[1])
    d = ImageDraw.Draw(img, "RGBA")
    for _ in range(14):
        cx, cy, r = rnd.randint(0, w), rnd.randint(0, h), rnd.randint(30, 160)
        shape(d, kind, cx, cy, r, (255, 255, 255, rnd.randint(25, 70)))
    title = name.split("\n")[0]
    f = font(96 if len(title) < 18 else 72)
    d.text((w / 2, h * 0.45), title, font=f, fill=(255, 255, 255), anchor="mm", stroke_width=10,
           stroke_fill=tuple(int(c * 0.6) for c in colors[0]))
    if "\n" in name:
        d.text((w / 2, h * 0.6), name.split("\n")[1], font=font(44), fill=(255, 240, 200), anchor="mm")
    d.text((w - 40, h - 40), "© Estudio ficticio", font=font(22), fill=(255, 255, 255, 180), anchor="rs")
    return img.filter(ImageFilter.SMOOTH)


def meta_xml(name, publisher, product, ver, tid):
    esc = lambda s: s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    langs = ["ja", "en", "fr", "de", "it", "es", "zhs", "ko", "nl", "pt", "ru", "zht"]
    out = ['<?xml version="1.0" encoding="utf-8"?>', '<menu type="complex" access="777">',
           '  <version type="unsignedInt" length="4">33</version>',
           f'  <title_id type="hexBinary" length="8">{tid}</title_id>',
           f'  <group_id type="hexBinary" length="4">0000{tid[12:16]}</group_id>',
           '  <os_version type="hexBinary" length="8">000500101000400A</os_version>',
           '  <boss_id type="hexBinary" length="8">0000000000000000</boss_id>',
           '  <app_size type="hexBinary" length="8">0000000000000000</app_size>',
           f'  <product_code type="string" length="32">{product}</product_code>',
           '  <company_code type="string" length="8">0001</company_code>',
           '  <region type="hexBinary" length="4">00000002</region>',
           '  <ext_dev_urcc type="unsignedInt" length="4">1</ext_dev_urcc>',
           '  <ext_dev_nunchaku type="unsignedInt" length="4">0</ext_dev_nunchaku>',
           '  <drc_use type="unsignedInt" length="4">65537</drc_use>',
           f'  <title_version type="unsignedInt" length="4">{ver}</title_version>']
    for l in langs:
        out.append(f'  <longname_{l} type="string" length="512">{esc(name)}</longname_{l}>')
        out.append(f'  <shortname_{l} type="string" length="256">{esc(name.splitlines()[0])}</shortname_{l}>')
        out.append(f'  <publisher_{l} type="string" length="256">{esc(publisher)}</publisher_{l}>')
    out.append("</menu>")
    return "\n".join(out)


def main():
    titles = os.path.join(ROOT, "titles")
    os.makedirs(titles, exist_ok=True)
    for i, (low, name, pub, product, colors, dev, kind, upd, dlc) in enumerate(GAMES):
        tid = "00050000" + low
        base = os.path.join(titles, tid)
        meta = os.path.join(base, "meta")
        os.makedirs(meta, exist_ok=True)
        with open(os.path.join(meta, "meta.xml"), "w", encoding="utf-8") as f:
            f.write(meta_xml(name, pub, product, 0, tid))
        make_icon(name, colors, kind).save(os.path.join(meta, "iconTex.tga"))
        make_boot(name, colors, kind, i).save(os.path.join(meta, "bootTvTex.tga"))
        with open(os.path.join(base, "device.txt"), "w") as f:
            f.write(dev)
        if upd:
            ub = os.path.join(titles, "0005000E" + low)
            os.makedirs(ub, exist_ok=True)
            with open(os.path.join(ub, "version.txt"), "w") as f:
                f.write(str(upd))
        if dlc:
            os.makedirs(os.path.join(titles, "0005000C" + low), exist_ok=True)
    print(f"{len(GAMES)} títulos de prueba en {titles}")


if __name__ == "__main__":
    main()

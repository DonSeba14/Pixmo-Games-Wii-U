"""Revisa la traducción al inglés de la interfaz (src/i18n.c).

Busca en src/*.c los textos en español (cadenas con letras que no son rutas,
formatos internos ni mensajes del log) y avisa:
  - si alguno no tiene traducción en la tabla de src/i18n.c,
  - si la tabla tiene entradas que ya no se usan,
  - si una traducción cambia los %d/%s del formato,
  - si una traducción es a la vez una clave con otra traducción (el texto se
    traduce al dibujar, y a veces pasa dos veces por tr()).

Uso: py -3 tools/check_i18n.py            (sale con código 1 si hay problemas)
     py -3 tools/check_i18n.py --list     (además lista los textos encontrados)
"""
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SRC = os.path.join(ROOT, "src")

# files whose strings are never shown (formats, parsing, platform internals)
SKIP_FILES = {"i18n.c", "adpcm.c", "gfx.c", "image.c", "meta.c", "net.c", "wuhb.c", "platform_pc.c",
              "brand.c", "font.c", "audio.c"}
# a literal on a line with any of these is not UI text
SKIP_LINE = re.compile(r"log_msg|LOG\(|OSReport|#include|fopen|fprintf|sscanf|strcmp\(c->cmd|getenv|"
                       r"data_path\(|plat_thread_start|OSDynLoad|http_get|strtok_r|OUT\(|file_write_atomic|"
                       r"static const char msg\[\]|static const char mark\[\]|str_copy\(t->device|"
                       r"ini_value|xml_|snprintf\(path|snprintf\(rel|snprintf\(names|snprintf\(buf, n, \"%s/%s\"")
LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')
# the same in both languages
NEUTRAL = {"Color", "Español", "English", "No", "Arcade", "Relax", "Australia", "China", "Editor", "Title ID",
           "DLC", "GamePad", "Wii U", "Wii", "GameCube"}
# month and day names: util.c picks the English ones itself
SKIP_LINE_DATES = re.compile(r"k_months|k_days|^\s*\"jul\"|^\s*\"Jul\"")
SPANISH_HINT = re.compile(r"[áéíóúñÁÉÍÓÚÑ¿¡«»]|\b(de|del|la|las|los|el|un|una|juego|juegos|categor|"
                          r"Jugar|Ajustes|Buscar|Cancelar|Aceptar|Hace|Hoy|Ayer|Nunca|Favorit|Todos|"
                          r"Recientes|Vista|Tema|Nombre|Editor|Claro|Oscuro|Cine|Vitrina|Mando|Disparos|"
                          r"Aventura|Carreras|Peleas|Puzles|Estrella|Deportes|Terror|Trofeo|Corona|"
                          r"Multijugador|Rol|Familia|Joya|Fuego|Espacio|Fiesta|Plataformas|Historia|Reloj|"
                          r"Opciones|Atrás|Editar|Nueva|Borrar|Mover|Cambiar|Crear|Reiniciar|Iniciando|"
                          r"Oculto|NUEVO|Instalado|No|Ninguno|Ninguna|Datos|Icono|Color|Región|Ubicación|"
                          r"Versión|Mandos|Código|Iniciado|Cargando|Leyendo|Descargando|Sin|Modo|Abrir|"
                          r"Volver|Salir|Gestionar|Controles|Acerca|Mostrar|Leer|Descargar|Efectos|Música|"
                          r"Fondo|Ordenar|Marcar|Saltar|Ir|Pantalla|Con|Elige|Logo|Imágenes|Copia|Pulsa|"
                          r"Esta|Aún|Todavía|Los|Se|Tu|Homebrew|Consola|Disco|Memoria|Título|semana|mes|"
                          r"vez|veces|minutos|días|ene|feb|mar|abr|may|jun|jul|ago|sep|oct|nov|dic|"
                          r"dom|lun|mié|jue|vie|sáb|Juego|Ninguna)\b")


def table():
    txt = open(os.path.join(SRC, "i18n.c"), encoding="utf-8").read()
    body = txt[txt.index("k_en[]"):]
    body = body[:body.index("};")]
    pairs = re.findall(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}', body)
    return pairs


def literals():
    found = {}
    for name in sorted(os.listdir(SRC)):
        if not name.endswith(".c") or name in SKIP_FILES:
            continue
        lines = open(os.path.join(SRC, name), encoding="utf-8").read().split("\n")
        in_comment = False
        for no, line in enumerate(lines, 1):
            code = line
            if in_comment:
                if "*/" not in code:
                    continue
                code = code[code.index("*/") + 2:]
                in_comment = False
            # comment markers are looked for outside string literals
            blank = LIT.sub(lambda m: '"' + " " * len(m.group(1)) + '"', code)
            pos = blank.find("//")
            if pos >= 0:
                code, blank = code[:pos], blank[:pos]
            pos = blank.find("/*")
            if pos >= 0 and "*/" not in blank[pos:]:
                code = code[:pos]
                in_comment = True
            stripped = code.strip()
            if not stripped or stripped.startswith("*") or stripped.startswith("#"):
                continue
            if SKIP_LINE.search(code) or SKIP_LINE_DATES.search(code):
                continue
            for m in LIT.finditer(code):
                s = m.group(1)
                if not re.search(r"[A-Za-zÁÉÍÓÚáéíóúñÑ]", s):
                    continue
                if s in NEUTRAL or not SPANISH_HINT.search(s):
                    continue
                found.setdefault(s, f"{name}:{no}")
    return found


def fmt_specs(s):
    return re.findall(r"%[-0-9.]*(?:ll|l|z)?[dusxXfc%]", s)


def main():
    pairs = table()
    keys = {}
    problems = 0
    for es, en in pairs:
        if es in keys:
            print(f"repetida en la tabla: {es!r}")
            problems += 1
        keys[es] = en
        if fmt_specs(es) != fmt_specs(en):
            print(f"formato distinto: {es!r} -> {en!r}")
            problems += 1
    for es, en in pairs:
        if en in keys and keys[en] != en:
            print(f"la traducción {en!r} (de {es!r}) también es una clave: se traduciría dos veces")
            problems += 1
    found = literals()
    if "--list" in sys.argv:
        for s, where in sorted(found.items(), key=lambda kv: kv[1]):
            print(f"{where:24} {s}")
    for s, where in sorted(found.items(), key=lambda kv: kv[1]):
        if s not in keys:
            print(f"sin traducción: {where}: {s!r}")
            problems += 1
    allsrc = ""
    for name in os.listdir(SRC):
        if name.endswith((".c", ".h")) and name != "i18n.c":
            allsrc += open(os.path.join(SRC, name), encoding="utf-8").read()
    for es, _ in pairs:
        if f'"{es}"' not in allsrc and es not in allsrc:
            print(f"entrada sin uso: {es!r}")
    print(f"{len(found)} textos en el código, {len(pairs)} en la tabla, {problems} problemas")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())

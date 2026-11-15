"""Convierte canciones (mp3, ogg, wav, flac, m4a, opus) al formato de música de
Pixmo Games en la Wii U: WAV IMA ADPCM, 32 kHz, estéreo (~32 KB por segundo).
La consola lo decodifica casi sin gastar CPU. El título de cada canción va en
el propio archivo (se muestra al empezar a sonar); los nombres de archivo
quedan sin acentos para la SD.

Necesita ffmpeg (Linux/WSL: apt install ffmpeg; Windows: ffmpeg en el PATH). Uso:
  python3 tools/musica_consola.py <carpeta con tus canciones> musica
y copiar la carpeta "musica" a SD:/wiiu/apps/pixmogames/musica/.

Converts songs to the Wii U music format of Pixmo Games (needs ffmpeg). Usage:
  python3 tools/musica_consola.py <folder with your songs> musica
then copy the "musica" folder to SD:/wiiu/apps/pixmogames/musica/.
"""
import os
import re
import subprocess
import sys
import unicodedata

EXT = {".mp3", ".ogg", ".wav", ".flac", ".m4a", ".opus"}


def slug(name):
    s = unicodedata.normalize("NFKD", name)
    s = "".join(c for c in s if not unicodedata.combining(c))
    s = re.sub(r"[^A-Za-z0-9]+", "-", s).strip("-").lower()
    return s[:60] or "cancion"


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    made = kept = 0
    wanted = set()
    for name in sorted(os.listdir(src)):
        base, ext = os.path.splitext(name)
        if ext.lower() not in EXT:
            continue
        inp = os.path.join(src, name)
        out = os.path.join(dst, slug(base) + ".wav")
        wanted.add(os.path.basename(out))
        if os.path.exists(out) and os.path.getmtime(out) >= os.path.getmtime(inp):
            kept += 1
            continue
        tmp = out[:-4] + ".tmp.wav"
        cmd = ["ffmpeg", "-v", "error", "-y", "-i", inp, "-vn", "-map_metadata", "-1",
               "-metadata", "title=" + base, "-ac", "2", "-ar", "32000", "-c:a", "adpcm_ima_wav", tmp]
        subprocess.run(cmd, check=True)
        os.replace(tmp, out)
        made += 1
        print(f"  {os.path.getsize(out) / 1e6:5.1f} MB  {base}")
    # songs removed from the source folder go away here too
    removed = 0
    for name in os.listdir(dst):
        if name.endswith(".wav") and name not in wanted:
            os.remove(os.path.join(dst, name))
            removed += 1
    print(f"música: {made} convertidas, {kept} ya estaban, {removed} quitadas")


if __name__ == "__main__":
    main()

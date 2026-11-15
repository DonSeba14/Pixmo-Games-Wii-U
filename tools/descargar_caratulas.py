"""Descarga carátulas de GameTDB a la tarjeta SD desde el PC.

Útil si la consola no tiene internet. Lee la lista de juegos que Pixmo Games
guardó en la SD (cache.txt) y baja las carátulas que falten a covers/.

Uso:  py -3 tools/descargar_caratulas.py G:\\          (letra de la SD)
      py -3 tools/descargar_caratulas.py G:\\ --region ES   (preferir portadas españolas PAL)
"""
import argparse
import os
import sys
import time
import urllib.error
import urllib.request

REGIONS = {
    "E": ["US", "EN"], "N": ["US", "EN"], "J": ["JA"], "K": ["KO"], "D": ["DE"], "F": ["FR"],
    "S": ["ES"], "I": ["IT"], "H": ["NL"], "U": ["AU", "EN"],
    "P": ["EN", "ES", "FR", "DE", "IT"], "X": ["EN", "ES", "FR", "DE"], "Y": ["EN", "ES"], "Z": ["EN", "ES"],
}


def read_ids(cache_path):
    ids = []
    with open(cache_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if not line.startswith("m "):
                continue
            head = line.split("\t")[0].split()
            # m <tid> <region> <ext> <drc> <product> <company> <dlchecked> <savetime> <publisher...>
            if len(head) < 7:
                continue
            product, company = head[5], head[6]
            if product == "-" or len(product) < 4:
                continue
            code = product[-4:]
            cc = company[-2:] if company != "-" and len(company) >= 2 else "01"
            if code.isalnum():
                ids.append((code + cc).upper())
    return sorted(set(ids))


def fetch(url):
    req = urllib.request.Request(url, headers={"User-Agent": "PixmoGames/1.0 (PC helper)"})
    with urllib.request.urlopen(req, timeout=20) as r:
        return r.read()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sd", help="raíz de la tarjeta SD (ej. G:\\)")
    ap.add_argument("--region", help="región preferida de GameTDB (US, EN, ES, FR, DE, IT, JA...)")
    args = ap.parse_args()

    base = os.path.join(args.sd, "wiiu", "apps", "pixmogames")
    cache = os.path.join(base, "cache.txt")
    if not os.path.exists(cache):
        sys.exit("No encuentro %s: abre Pixmo Games una vez en la consola para crearlo." % cache)
    covers = os.path.join(base, "covers")
    os.makedirs(covers, exist_ok=True)

    ids = read_ids(cache)
    print("%d juegos con ID de GameTDB" % len(ids))
    ok = skipped = missing = 0
    for gid in ids:
        if any(os.path.exists(os.path.join(covers, gid + ext)) for ext in (".jpg", ".png")):
            skipped += 1
            continue
        regions = list(REGIONS.get(gid[3], ["US", "EN"]))
        if args.region:
            regions.insert(0, args.region.upper())
        data = None
        for kind in ("coverM", "cover"):
            for reg in regions:
                url = "https://art.gametdb.com/wiiu/%s/%s/%s.jpg" % (kind, reg, gid)
                try:
                    data = fetch(url)
                    break
                except urllib.error.HTTPError:
                    continue
                except urllib.error.URLError as e:
                    sys.exit("Sin conexión: %s" % e)
            if data:
                break
        if data and data[:2] == b"\xff\xd8":
            with open(os.path.join(covers, gid + ".jpg"), "wb") as f:
                f.write(data)
            ok += 1
            print("  OK  ", gid)
        else:
            missing += 1
            print("  --  ", gid, "(GameTDB no tiene carátula)")
        time.sleep(0.3)
    print("Listo: %d descargadas, %d ya estaban, %d sin carátula." % (ok, skipped, missing))


if __name__ == "__main__":
    main()

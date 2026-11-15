// Paquetes .wuhb (formato de wuhbtool, de wut): cabecera "WUHB" de 0x50 bytes y
// tablas de directorios y archivos al estilo del RomFS de 3DS, todo big-endian y
// con desplazamientos de 64 bits:
//   0x18 tabla de directorios, 0x20 su tamaño, 0x38 tabla de archivos, 0x40 su
//   tamaño, 0x48 inicio de los datos.
//   Directorio: padre, hermano, primer hijo, primer archivo, hash, largo, nombre.
//   Archivo: padre (u32), hermano (u32), desplazamiento (u64), tamaño (u64), hash, largo, nombre.
#include "wuhb.h"

#include "stb_image.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static uint32_t be32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

static uint64_t be64(const uint8_t *p)
{
    return (uint64_t)be32(p) << 32 | be32(p + 4);
}

uint64_t wuhb_title_id(const char *rel)
{
    uint32_t h = 2166136261u; // FNV-1a
    for (const unsigned char *p = (const unsigned char *)rel; *p; p++)
        h = (h ^ *p) * 16777619u;
    return 0x0005000F00000000ull | h;
}

static void add_bundle(PlatTitle **list, int *n, int *cap, const char *rel)
{
    if (*n == *cap) {
        int nc = *cap ? *cap * 2 : 16;
        PlatTitle *nl = realloc(*list, sizeof(PlatTitle) * (size_t)nc);
        if (!nl)
            return;
        *list = nl;
        *cap = nc;
    }
    PlatTitle *t = &(*list)[(*n)++];
    memset(t, 0, sizeof(*t));
    t->tid = wuhb_title_id(rel);
    t->kind = TITLE_GAME;
    t->homebrew = true;
    str_copy(t->path, sizeof(t->path), rel);
    str_copy(t->device, sizeof(t->device), "sd");
}

static bool is_wuhb(const char *name)
{
    size_t l = strlen(name);
    return l > 5 && name[0] != '.' && strcasecmp(name + l - 5, ".wuhb") == 0;
}

void wuhb_scan(const char *sd_root, PlatTitle **list, int *n, int *cap)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/wiiu/apps", sd_root);
    DIR *d = opendir(dir);
    if (!d)
        return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        char rel[256];
        if (is_wuhb(e->d_name)) {
            snprintf(rel, sizeof(rel), "wiiu/apps/%s", e->d_name);
            add_bundle(list, n, cap, rel);
            continue;
        }
        if (!strcasecmp(e->d_name, APP_DIR) || !strcasecmp(e->d_name, APP_DIR_OLD))
            continue; // our own bundle
        char sub[768];
        snprintf(sub, sizeof(sub), "%s/%s", dir, e->d_name);
        DIR *s = opendir(sub);
        if (!s)
            continue;
        struct dirent *f;
        while ((f = readdir(s)) != NULL)
            if (is_wuhb(f->d_name)) {
                snprintf(rel, sizeof(rel), "wiiu/apps/%s/%s", e->d_name, f->d_name);
                add_bundle(list, n, cap, rel);
            }
        closedir(s);
    }
    closedir(d);
}

// Full path of directory entry `o` ("/meta"); the root is entry 0.
static void dir_path(const uint8_t *dt, size_t dt_size, uint32_t o, char *out, size_t n)
{
    char parts[8][64];
    int np = 0;
    while (o != 0 && o + 24 <= dt_size && np < 8) {
        uint32_t len = be32(dt + o + 20);
        if (len > 63 || o + 24 + len > dt_size)
            break;
        memcpy(parts[np], dt + o + 24, len);
        parts[np][len] = 0;
        np++;
        o = be32(dt + o);
    }
    size_t w = 0;
    out[0] = 0;
    for (int i = np - 1; i >= 0 && w + 1 < n; i--)
        w += (size_t)snprintf(out + w, n - w, "/%s", parts[i]);
}

bool wuhb_read(const char *bundle, const char *inner, uint8_t **out, size_t *size)
{
    *out = NULL;
    *size = 0;
    FILE *f = fopen(bundle, "rb");
    if (!f)
        return false;
    uint8_t h[0x50];
    uint8_t *dt = NULL, *ft = NULL;
    bool ok = false;
    if (fread(h, 1, sizeof(h), f) != sizeof(h) || memcmp(h, "WUHB", 4) != 0)
        goto done;
    uint64_t dt_off = be64(h + 0x18), dt_size = be64(h + 0x20);
    uint64_t ft_off = be64(h + 0x38), ft_size = be64(h + 0x40), data_off = be64(h + 0x48);
    if (dt_size > (1u << 20) || ft_size > (1u << 20) || !dt_size || !ft_size)
        goto done;
    dt = malloc((size_t)dt_size);
    ft = malloc((size_t)ft_size);
    if (!dt || !ft || fseek(f, (long)dt_off, SEEK_SET) || fread(dt, 1, (size_t)dt_size, f) != dt_size ||
        fseek(f, (long)ft_off, SEEK_SET) || fread(ft, 1, (size_t)ft_size, f) != ft_size)
        goto done;
    for (size_t o = 0; o + 32 <= ft_size;) {
        uint32_t parent = be32(ft + o), len = be32(ft + o + 28);
        uint64_t doff = be64(ft + o + 8), dsize = be64(ft + o + 16);
        if (o + 32 + len > ft_size)
            break;
        char full[256], name[128];
        dir_path(dt, (size_t)dt_size, parent, full, sizeof(full));
        size_t nl = len < sizeof(name) - 1 ? len : sizeof(name) - 1;
        memcpy(name, ft + o + 32, nl);
        name[nl] = 0;
        size_t fl = strlen(full);
        snprintf(full + fl, sizeof(full) - fl, "/%s", name);
        if (!strcmp(full, inner)) {
            if (dsize > (32u << 20))
                break;
            uint8_t *buf = malloc((size_t)dsize + 1);
            if (buf && !fseek(f, (long)(data_off + doff), SEEK_SET) && fread(buf, 1, (size_t)dsize, f) == dsize) {
                buf[dsize] = 0;
                *out = buf;
                *size = (size_t)dsize;
                ok = true;
            } else {
                free(buf);
            }
            break;
        }
        o += 32 + ((len + 3) & ~3u);
    }
done:
    free(dt);
    free(ft);
    fclose(f);
    return ok;
}

bool wuhb_read_gz(const char *bundle, const char *inner, uint8_t **out, size_t *size)
{
    uint8_t *gz;
    size_t n;
    *out = NULL;
    *size = 0;
    if (!wuhb_read(bundle, inner, &gz, &n))
        return false;
    bool ok = false;
    if (n >= 18 && gz[0] == 0x1F && gz[1] == 0x8B && gz[2] == 8) {
        int flg = gz[3];
        size_t p = 10;
        if ((flg & 4) && p + 2 <= n) // extra field
            p += 2 + (size_t)(gz[p] | gz[p + 1] << 8);
        if (flg & 8) { // file name
            while (p < n && gz[p])
                p++;
            p++;
        }
        if (flg & 16) { // comment
            while (p < n && gz[p])
                p++;
            p++;
        }
        if (flg & 2)
            p += 2;
        uint32_t isize = (uint32_t)gz[n - 4] | (uint32_t)gz[n - 3] << 8 | (uint32_t)gz[n - 2] << 16 |
                         (uint32_t)gz[n - 1] << 24;
        if (p + 8 < n && isize > 0 && isize <= (16u << 20)) {
            char *buf = malloc(isize);
            if (buf && stbi_zlib_decode_noheader_buffer(buf, (int)isize, (const char *)gz + p, (int)(n - 8 - p)) ==
                           (int)isize) {
                *out = (uint8_t *)buf;
                *size = isize;
                ok = true;
            } else {
                free(buf);
            }
        }
    }
    free(gz);
    return ok;
}

static void ini_value(const char *ini, const char *key, char *out, size_t n)
{
    size_t kl = strlen(key);
    for (const char *p = ini; p && *p;) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            p += kl + 1;
            size_t i = 0;
            while (p[i] && p[i] != '\n' && p[i] != '\r' && i + 1 < n) {
                out[i] = p[i];
                i++;
            }
            out[i] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p)
            p++;
    }
}

bool wuhb_meta(const char *bundle, PlatMeta *m)
{
    uint8_t *ini;
    size_t n;
    memset(m, 0, sizeof(*m));
    if (!wuhb_read(bundle, "/meta/meta.ini", &ini, &n))
        return false;
    ini_value((const char *)ini, "longname", m->name, sizeof(m->name));
    ini_value((const char *)ini, "shortname", m->short_name, sizeof(m->short_name));
    ini_value((const char *)ini, "author", m->publisher, sizeof(m->publisher));
    free(ini);
    if (!m->name[0])
        str_copy(m->name, sizeof(m->name), m->short_name);
    return m->name[0] != 0;
}

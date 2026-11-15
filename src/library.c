// Biblioteca de juegos: lista de títulos, caché de metadatos en la SD,
// estadísticas, categorías y dos hilos de fondo (carga de imágenes y
// descarga de carátulas). El hilo principal es dueño de las texturas.
#include "library.h"
#include "gfx.h"
#include "i18n.h"
#include "icons.h"
#include "image.h"
#include "net.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Library L;

enum { JOB_META, JOB_ICON, JOB_COVER, JOB_BOOT, JOB_SAVE, JOB_LOGO, JOB_DOWNLOAD, JOB_BOOT_FULL, JOB_FONDO, JOB_TITLE,
       JOB_THUMBS, JOB_THUMB, JOB_WRITE };

typedef struct Job {
    int type, idx;
    uint32_t gen, seq;
    int prio;
    uint64_t tid;
    PlatTitle pt;
    char tdb[8];
    char path[192];
    uint8_t *blob;      // JOB_WRITE: bytes to write to path (freed by the worker)
    size_t blob_size;
    uint32_t thumb_stamp; // JOB_COVER: the game's thumbnail is of a cover file this big (0 none)
} Job;

// one thumbnail of miniaturas.bin
typedef struct ThumbEntry {
    uint64_t tid;
    uint16_t w, h;
    uint32_t stamp;
    uint16_t *px;       // RGB565, host order
} ThumbEntry;

typedef struct Result {
    int type, idx;
    uint32_t gen;
    uint64_t tid;
    bool ok;
    int state;
    PlatMeta meta;
    Image img, img2;
    int64_t time;
    uint8_t tint[3];
    uint16_t *px565;    // JOB_COVER / JOB_THUMB: the thumbnail (img2 has it in RGBA)
    ThumbEntry *thumbs; // JOB_THUMBS: everything read from miniaturas.bin
    int nthumbs;
} Result;

#define THUMB_H 120
static ThumbEntry *s_thumb_queue;   // read from the file, waiting to become textures
static int s_thumb_queue_n, s_thumb_queue_pos;
static bool s_thumbs_loaded;        // miniaturas.bin was read (or does not exist)
static bool s_thumb_pass;           // thumbnails of the remaining games were requested
static int s_thumb_jobs;            // JOB_THUMB in flight
static bool s_thumbs_dirty;         // miniaturas.bin should be written again
static uint32_t s_thumbs_changed;   // SDL ticks of the last change
static SDL_atomic_t s_dl_active;    // covers being downloaded right now

typedef struct JobList {
    Job *v;
    int n, cap;
} JobList;

static PlatThread *s_loader, *s_downloader;
static uint32_t s_start_ticks;
static int s_icon_ok, s_icon_fail;

#define DL_EXISTS 100 // internal: the cover file is already on the SD card
static SDL_mutex *s_lock;
static SDL_cond *s_cond, *s_dl_cond;
static JobList s_jobs, s_dls;
static Result *s_res;
static int s_nres, s_capres;
static volatile bool s_quit;
static uint32_t s_gen = 1, s_seq;
static uint32_t s_frame;
static bool s_cache_dirty;

static void mark_shared_ids(void);

// stats lines of games that are not installed right now (kept on save)
static char **s_orphans;
static int s_norphans;

const char *data_path(const char *rel, char *buf, size_t n)
{
    snprintf(buf, n, "%s/%s", plat_data_dir(), rel);
    return buf;
}

void lib_save_soon(void)
{
    L.save_at = SDL_GetTicks() + 1200;
    if (!L.save_at)
        L.save_at = 1;
}

void lib_status(const char *msg, uint32_t ms)
{
    str_copy(L.status, sizeof(L.status), msg);
    L.status_until = SDL_GetTicks() + ms;
}

// ------------------------------------------------------------ job queues

static void jobs_push(JobList *l, const Job *j)
{
    if (l->n == l->cap) {
        int nc = l->cap ? l->cap * 2 : 256;
        Job *nv = realloc(l->v, sizeof(Job) * nc);
        if (!nv)
            return;
        l->v = nv;
        l->cap = nc;
    }
    l->v[l->n] = *j;
    l->v[l->n].seq = s_seq++;
    l->n++;
}

static int jobs_best(const JobList *l)
{
    int best = -1;
    for (int i = 0; i < l->n; i++) {
        const Job *a = &l->v[i];
        if (best < 0 || a->prio < l->v[best].prio ||
            (a->prio == l->v[best].prio && a->seq < l->v[best].seq))
            best = i;
    }
    return best;
}

static Job *jobs_find(JobList *l, int type, int idx)
{
    for (int i = 0; i < l->n; i++)
        if (l->v[i].type == type && l->v[i].idx == idx && l->v[i].gen == s_gen)
            return &l->v[i];
    return NULL;
}

static void submit(int type, int idx, int prio)
{
    Job j;
    memset(&j, 0, sizeof(j));
    j.type = type;
    j.idx = idx;
    j.gen = s_gen;
    j.prio = prio;
    if (idx >= 0 && idx < L.count && type != JOB_LOGO) {
        Game *g = &L.games[idx];
        j.tid = g->tid;
        j.pt = g->pt;
        str_copy(j.tdb, sizeof(j.tdb), g->tdb_shared ? "" : g->tdb_id);
        j.thumb_stamp = g->thumb ? g->thumb_stamp : 0;
    }
    SDL_LockMutex(s_lock);
    if (type == JOB_LOGO) {
        Category *c = cat_by_id(idx);
        if (c) {
            char rel[96];
            snprintf(rel, sizeof(rel), "logos/%s", c->logo);
            data_path(rel, j.path, sizeof(j.path));
        }
    }
    if (type == JOB_DOWNLOAD) {
        jobs_push(&s_dls, &j);
        SDL_CondSignal(s_dl_cond);
    } else {
        jobs_push(&s_jobs, &j);
        SDL_CondSignal(s_cond);
    }
    SDL_UnlockMutex(s_lock);
}

static void push_result(Result *r)
{
    SDL_LockMutex(s_lock);
    if (s_nres == s_capres) {
        int nc = s_capres ? s_capres * 2 : 64;
        Result *nr = realloc(s_res, sizeof(Result) * nc);
        if (!nr) {
            SDL_UnlockMutex(s_lock);
            img_free(&r->img);
            img_free(&r->img2);
            return;
        }
        s_res = nr;
        s_capres = nc;
    }
    s_res[s_nres++] = *r;
    SDL_UnlockMutex(s_lock);
}

// ------------------------------------------------------------ worker jobs

static void force_opaque(Image *im)
{
    size_t n = (size_t)im->w * im->h;
    for (size_t i = 0; i < n; i++)
        im->px[i * 4 + 3] = 255;
}

static bool find_cover_file(const Job *j, char *out, size_t n)
{
    char names[4][48];
    int nn = 0;
    if (j->tdb[0]) {
        snprintf(names[nn++], 48, "covers/%s.png", j->tdb);
        snprintf(names[nn++], 48, "covers/%s.jpg", j->tdb);
    }
    snprintf(names[nn++], 48, "covers/%016llX.png", (unsigned long long)j->tid);
    snprintf(names[nn++], 48, "covers/%016llX.jpg", (unsigned long long)j->tid);
    for (int i = 0; i < nn; i++) {
        data_path(names[i], out, n);
        if (file_exists(out))
            return true;
    }
    return false;
}

// ------------------------------------------------------------ thumbnails
// miniaturas.bin (little-endian): "PXM1", count, 0, then count entries of
// { u64 title id, u16 w, u16 h, u32 stamp, u32 offset } and the RGB565 pixels.

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

// RGBA cover -> THUMB_H tall thumbnail: RGB565 copy (returned) and RGBA (*rgba)
static uint16_t *make_thumb(const Image *src, Image *rgba)
{
    memset(rgba, 0, sizeof(*rgba));
    if (!src->px || src->w <= 0 || src->h <= 0)
        return NULL;
    int h = THUMB_H, w = (int)((float)THUMB_H * src->w / src->h + 0.5f);
    if (w < 40)
        w = 40;
    if (w > 240)
        w = 240;
    *rgba = img_resize(src, w, h);
    if (!rgba->px)
        return NULL;
    uint16_t *px = malloc(sizeof(uint16_t) * (size_t)w * h);
    if (!px)
        return NULL;
    for (int i = 0; i < w * h; i++) {
        const uint8_t *p = rgba->px + (size_t)i * 4;
        px[i] = (uint16_t)((p[0] >> 3) << 11 | (p[1] >> 2) << 5 | p[2] >> 3);
    }
    return px;
}

static void thumb_rgba(const uint16_t *px, int w, int h, Image *out)
{
    if (!img_alloc(out, w, h))
        return;
    for (int i = 0; i < w * h; i++) {
        uint16_t v = px[i];
        uint8_t *p = out->px + (size_t)i * 4;
        int r = v >> 11 & 31, g = v >> 5 & 63, b = v & 31;
        p[0] = (uint8_t)(r << 3 | r >> 2);
        p[1] = (uint8_t)(g << 2 | g >> 4);
        p[2] = (uint8_t)(b << 3 | b >> 2);
        p[3] = 255;
    }
}

static void read_thumb_file(Result *r)
{
    char path[256];
    size_t size = 0;
    uint8_t *d = file_read(data_path("miniaturas.bin", path, sizeof(path)), &size);
    if (!d)
        return;
    uint32_t count = size >= 12 && !memcmp(d, "PXM1", 4) ? le32(d + 4) : 0;
    if (count > 4096 || 12 + (size_t)count * 20 > size)
        count = 0;
    r->thumbs = calloc(count ? count : 1, sizeof(ThumbEntry));
    for (uint32_t i = 0; r->thumbs && i < count; i++) {
        const uint8_t *e = d + 12 + (size_t)i * 20;
        ThumbEntry t;
        t.tid = (uint64_t)le32(e + 4) << 32 | le32(e);
        t.w = le16(e + 8);
        t.h = le16(e + 10);
        t.stamp = le32(e + 12);
        uint32_t off = le32(e + 16);
        size_t bytes = (size_t)t.w * t.h * 2;
        if (!t.w || !t.h || t.w > 512 || t.h > 512 || off + bytes > size)
            continue;
        t.px = malloc(bytes);
        if (!t.px)
            continue;
        for (size_t k = 0; k < (size_t)t.w * t.h; k++)
            t.px[k] = le16(d + off + k * 2);
        r->thumbs[r->nthumbs++] = t;
    }
    free(d);
}

// the whole cache file, built on the main thread (it owns the games)
static uint8_t *thumb_blob(size_t *out_size)
{
    int count = 0;
    size_t bytes = 0;
    for (int i = 0; i < L.count; i++)
        if (L.games[i].thumb_px) {
            count++;
            bytes += (size_t)L.games[i].thumb_w * L.games[i].thumb_h * 2;
        }
    size_t size = 12 + (size_t)count * 20 + bytes;
    uint8_t *b = malloc(size);
    if (!b)
        return NULL;
    memcpy(b, "PXM1", 4);
    uint32_t hdr[2] = { (uint32_t)count, 0 };
    for (int k = 0; k < 8; k++)
        b[4 + k] = (uint8_t)(hdr[k / 4] >> (8 * (k % 4)));
    size_t off = 12 + (size_t)count * 20;
    uint8_t *e = b + 12;
    for (int i = 0; i < L.count; i++) {
        Game *g = &L.games[i];
        if (!g->thumb_px)
            continue;
        uint32_t f[5] = { (uint32_t)g->tid, (uint32_t)(g->tid >> 32), (uint32_t)(g->thumb_w | g->thumb_h << 16),
                          g->thumb_stamp, (uint32_t)off };
        for (int k = 0; k < 20; k++)
            e[k] = (uint8_t)(f[k / 4] >> (8 * (k % 4)));
        e += 20;
        for (size_t p = 0; p < (size_t)g->thumb_w * g->thumb_h; p++) {
            b[off++] = (uint8_t)g->thumb_px[p];
            b[off++] = (uint8_t)(g->thumb_px[p] >> 8);
        }
    }
    *out_size = size;
    return b;
}

// saved thumbnails not turned into textures yet
static void thumb_queue_free(void)
{
    if (!s_thumb_queue)
        return;
    for (int i = s_thumb_queue_pos; i < s_thumb_queue_n; i++)
        free(s_thumb_queue[i].px);
    free(s_thumb_queue);
    s_thumb_queue = NULL;
    s_thumb_queue_n = s_thumb_queue_pos = 0;
}

// a thumbnail made again from a cover that has not changed is not news: the
// file already has it (it is still waiting in the queue)
static bool thumb_in_file(uint64_t tid, uint32_t stamp)
{
    for (int i = s_thumb_queue_pos; s_thumb_queue && i < s_thumb_queue_n; i++)
        if (s_thumb_queue[i].tid == tid)
            return s_thumb_queue[i].stamp == stamp;
    return false;
}

// miniaturas.bin right now, on this thread (closing, rescanning)
static void thumbs_write_now(void)
{
    size_t size = 0;
    uint8_t *blob = thumb_blob(&size);
    char path[256];
    if (blob && file_write_atomic(data_path("miniaturas.bin", path, sizeof(path)), blob, size))
        log_msg("miniaturas guardadas al cerrar (%u KB)", (unsigned)(size / 1024));
    free(blob);
    s_thumbs_dirty = false;
}

static void set_thumb(Game *g, uint16_t *px, int w, int h, uint32_t stamp, Image *rgba)
{
    Image tmp = { 0 };
    if (!rgba || !rgba->px) {
        thumb_rgba(px, w, h, &tmp);
        rgba = &tmp;
    }
    SDL_Texture *t = rgba->px ? img_to_texture(g_ren, rgba) : NULL;
    img_free(&tmp);
    if (!t) {
        free(px);
        return;
    }
    if (g->thumb)
        SDL_DestroyTexture(g->thumb);
    free(g->thumb_px);
    g->thumb = t;
    g->thumb_px = px;
    g->thumb_w = (uint16_t)w;
    g->thumb_h = (uint16_t)h;
    g->thumb_stamp = stamp;
}

static bool load_title_image(const PlatTitle *t, const char *name, Image *out)
{
    size_t size = 0;
    uint8_t *data = plat_read_title_file(t, name, &size);
    memset(out, 0, sizeof(*out));
    if (!data)
        return false;
    bool ok = img_load_mem(data, size, out);
    free(data);
    return ok;
}

// "leyendo.txt" exists while the first reads of console files are in
// progress; if the app freezes there, the next start finds it and turns
// those reads off (Ajustes can turn them back on).
static int s_title_reads;
static bool s_reading_marker;

static void reading_marker(bool on)
{
    char path[256];
    data_path("leyendo.txt", path, sizeof(path));
    if (on) {
        static const char msg[] = "Pixmo Games esta leyendo iconos de la consola\n";
        file_write_atomic(path, msg, sizeof(msg) - 1);
    } else {
        remove(path);
    }
    s_reading_marker = on;
}

static void run_job(const Job *j, Result *r)
{
    bool title_file = j->type == JOB_ICON || j->type == JOB_BOOT || j->type == JOB_BOOT_FULL || j->type == JOB_SAVE;
    if (title_file && !L.set.read_titles)
        return; // r->ok stays false
    if (title_file && s_title_reads < 12 && !s_reading_marker) {
        reading_marker(true);
        log_msg("primeras lecturas de archivos de la consola");
    }
    char path[256];
    switch (j->type) {
    case JOB_META:
        r->ok = plat_read_meta(&j->pt, &r->meta);
        break;
    case JOB_ICON:
        if (load_title_image(&j->pt, "iconTex.tga", &r->img)) {
            if (!img_is_mostly_opaque(&r->img)) {
                // some icons carry an empty alpha channel
                bool any = false;
                for (size_t i = 0; i < (size_t)r->img.w * r->img.h && !any; i++)
                    any = r->img.px[i * 4 + 3] != 0;
                if (!any)
                    force_opaque(&r->img);
            }
            if (r->img.w != 128 || r->img.h != 128) {
                Image s = img_resize(&r->img, 128, 128);
                img_free(&r->img);
                r->img = s;
            }
            img_average(&r->img, r->tint);
            r->ok = r->img.px != NULL;
        }
        break;
    case JOB_COVER:
    case JOB_THUMB: {
        // the cover file; JOB_THUMB only wants its thumbnail
        size_t fsize = 0;
        uint8_t *data = find_cover_file(j, path, sizeof(path)) ? file_read(path, &fsize) : NULL;
        if (data && img_load_mem(data, fsize, &r->img)) {
            // covers of about 440x620 load fast on the console: only shrink really big ones
            if (r->img.w > 640 || r->img.h > 900) {
                float s = fminf(640.0f / r->img.w, 900.0f / r->img.h);
                Image sm = img_resize(&r->img, (int)(r->img.w * s), (int)(r->img.h * s));
                img_free(&r->img);
                r->img = sm;
            }
            r->time = (int64_t)fsize; // stamp: tells when the cover file changed
            // a new thumbnail only when the game has none of this cover yet (on the
            // console every bit of a cover load counts: ~300 ms already)
            if (j->type == JOB_THUMB || (uint32_t)fsize != j->thumb_stamp)
                r->px565 = make_thumb(&r->img, &r->img2);
            if (j->type == JOB_THUMB)
                img_free(&r->img);
            r->ok = r->px565 != NULL || r->img.px != NULL;
        }
        free(data);
        break;
    }
    case JOB_THUMBS:
        read_thumb_file(r);
        r->ok = true;
        break;
    case JOB_WRITE:
        r->ok = j->blob && file_write_atomic(j->path, j->blob, j->blob_size);
        free(j->blob);
        log_msg("miniaturas guardadas (%u KB): %s", (unsigned)(j->blob_size / 1024), r->ok ? "ok" : "error");
        break;
    case JOB_BOOT: {
        Image full;
        if (load_title_image(&j->pt, "bootTvTex.tga", &full)) {
            force_opaque(&full);
            r->img = img_resize(&full, 640, 360);
            r->img2 = img_resize(&full, 128, 72);
            img_blur(&r->img2, 3, 3);
            img_average(&r->img2, r->tint);
            img_free(&full);
            r->ok = r->img.px && r->img2.px;
        }
        break;
    }
    case JOB_BOOT_FULL:
        if (load_title_image(&j->pt, "bootTvTex.tga", &r->img)) {
            force_opaque(&r->img);
            r->ok = true;
        }
        break;
    case JOB_SAVE: {
        int64_t best = plat_save_time(&j->pt);
        // ignore clocks that are obviously wrong
        if (best > time_now() + 86400 || best < 1262304000)
            best = 0;
        r->time = best;
        r->ok = best > 0;
        break;
    }
    case JOB_LOGO:
        if (img_load_file(j->path, &r->img)) {
            if (r->img.w > 160 || r->img.h > 160) {
                float s = fminf(160.0f / r->img.w, 160.0f / r->img.h);
                Image sm = img_resize(&r->img, (int)(r->img.w * s), (int)(r->img.h * s));
                img_free(&r->img);
                r->img = sm;
            }
            r->ok = r->img.px != NULL;
        }
        break;
    case JOB_FONDO: {
        char rel[48];
        snprintf(rel, sizeof(rel), "fondos/%016llX.jpg", (unsigned long long)j->tid);
        data_path(rel, path, sizeof(path));
        if (file_exists(path) && img_load_file(path, &r->img)) {
            if (r->img.w != SCREEN_W || r->img.h != SCREEN_H) {
                Image fit = img_crop_fill(&r->img, SCREEN_W, SCREEN_H);
                img_free(&r->img);
                r->img = fit;
            }
            if (r->img.px)
                force_opaque(&r->img);
            r->ok = r->img.px != NULL;
        }
        break;
    }
    case JOB_TITLE: {
        char rel[48];
        snprintf(rel, sizeof(rel), "titulos/%016llX.png", (unsigned long long)j->tid);
        data_path(rel, path, sizeof(path));
        if (file_exists(path) && img_load_file(path, &r->img)) {
            if (r->img.w > 800 || r->img.h > 320) {
                float s = fminf(800.0f / r->img.w, 320.0f / r->img.h);
                Image sm = img_resize(&r->img, (int)(r->img.w * s), (int)(r->img.h * s));
                img_free(&r->img);
                r->img = sm;
            }
            r->ok = r->img.px != NULL;
            if (r->ok) {
                // Dark logos (most visible pixels dark) would vanish on the dark
                // screen: build a soft white halo from their silhouette, at half size.
                size_t count = (size_t)r->img.w * r->img.h, n = 0, ndark = 0;
                for (size_t i = 0; i < count; i += 3) {
                    const uint8_t *p = r->img.px + i * 4;
                    if (p[3] > 128) {
                        n++;
                        if ((p[0] * 299u + p[1] * 587u + p[2] * 114u) / 1000u < 70)
                            ndark++;
                    }
                }
                const int P = 10;
                int hw = r->img.w / 2, hh = r->img.h / 2;
                if (n > 0 && ndark * 10 >= n * 6 && hw > 0 && hh > 0 &&
                    img_alloc(&r->img2, hw + 2 * P, hh + 2 * P)) {
                    Image *h2 = &r->img2;
                    for (size_t i = 0; i < (size_t)h2->w * h2->h; i++) {
                        h2->px[i * 4] = h2->px[i * 4 + 1] = h2->px[i * 4 + 2] = 255;
                        h2->px[i * 4 + 3] = 0;
                    }
                    for (int y = 0; y < hh; y++)
                        for (int x = 0; x < hw; x++) {
                            // strongest alpha of the 2x2 block
                            const uint8_t *s = r->img.px + ((size_t)(y * 2) * r->img.w + x * 2) * 4;
                            const uint8_t *s2 = s + (size_t)r->img.w * 4;
                            uint8_t a = s[3];
                            if (s[7] > a) a = s[7];
                            if (s2[3] > a) a = s2[3];
                            if (s2[7] > a) a = s2[7];
                            h2->px[((size_t)(y + P) * h2->w + x + P) * 4 + 3] = a;
                        }
                    img_blur(h2, 4, 3);
                }
            }
        }
        break;
    }
    }
}

static int loader_main(void *arg)
{
    (void)arg;
    for (;;) {
        SDL_LockMutex(s_lock);
        while (!s_quit && s_jobs.n == 0)
            SDL_CondWait(s_cond, s_lock);
        if (s_quit) {
            SDL_UnlockMutex(s_lock);
            break;
        }
        int b = jobs_best(&s_jobs);
        Job j = s_jobs.v[b];
        s_jobs.v[b] = s_jobs.v[--s_jobs.n];
        SDL_UnlockMutex(s_lock);

        Result r;
        memset(&r, 0, sizeof(r));
        r.type = j.type;
        r.idx = j.idx;
        r.gen = j.gen;
        r.tid = j.tid;
        uint32_t t0 = SDL_GetTicks();
        run_job(&j, &r);
        uint32_t ms = SDL_GetTicks() - t0;
        static const char *names[] = { "meta", "icono", "caratula", "arranque", "partidas",
                                       "logo", "descarga", "arranque-hd", "fondo", "titulo",
                                       "miniaturas", "miniatura", "guardar" };
        if (ms > 250 && j.type != JOB_THUMB) // thumbnails are background work
            log_msg("trabajo lento: %s juego %d %016llx %u ms", names[j.type], j.idx, (unsigned long long)j.tid, ms);
        if ((j.type == JOB_ICON || j.type == JOB_BOOT) && r.ok && L.set.read_titles && ++s_title_reads == 12 &&
            s_reading_marker) {
            reading_marker(false);
            log_msg("lecturas de la consola funcionando");
        }
        if (j.type == JOB_ICON) {
            if (r.ok)
                s_icon_ok++;
            else
                s_icon_fail++;
            if (s_icon_ok + s_icon_fail <= 3 || (s_icon_ok + s_icon_fail) % 50 == 0)
                log_msg("iconos: %d leidos, %d fallidos (ultimo %016llx %s)", s_icon_ok, s_icon_fail,
                        (unsigned long long)j.tid, r.ok ? "ok" : "fallo");
        }
        if (s_quit) {
            img_free(&r.img);
            img_free(&r.img2);
            break;
        }
        push_result(&r);
        // a pause after every job: never keep a core busy for long (see platform.h)
        plat_sleep_ms(j.type == JOB_SAVE ? 4 : 1);
    }
    log_msg("hilo de carga terminado");
    return 0;
}

static int region_list(const char *tdb, const char **out)
{
    int lang = plat_language();
    static const char *pal_lang[12] = { "EN", "EN", "FR", "DE", "IT", "ES", "EN", "EN", "NL", "PT", "RU", "EN" };
    int n = 0;
    switch (tdb[3]) {
    case 'E': case 'N': out[n++] = "US"; out[n++] = "EN"; break;
    case 'J': out[n++] = "JA"; break;
    case 'K': out[n++] = "KO"; break;
    case 'D': out[n++] = "DE"; break;
    case 'F': out[n++] = "FR"; break;
    case 'S': out[n++] = "ES"; break;
    case 'I': out[n++] = "IT"; break;
    case 'H': out[n++] = "NL"; break;
    case 'U': out[n++] = "AU"; out[n++] = "EN"; break;
    case 'P': case 'X': case 'Y': case 'Z':
        out[n++] = pal_lang[lang >= 0 && lang < 12 ? lang : 1];
        if (strcmp(out[0], "EN")) out[n++] = "EN";
        out[n++] = "FR"; out[n++] = "DE"; out[n++] = "ES"; out[n++] = "IT";
        break;
    default: out[n++] = "US"; out[n++] = "EN"; break;
    }
    return n;
}

static int download_cover_net(const char *tdb);

static int download_cover(const Job *j)
{
    const char *tdb = j->tdb;
    char existing[256];
    if (find_cover_file(j, existing, sizeof(existing)))
        return DL_EXISTS;
    if (!tdb[0])
        return DL_NOT_FOUND;
    static int net = 0; // 0 unknown, 1 up, 2 down
    if (net == 0) {
        uint32_t t0 = SDL_GetTicks();
        net = plat_net_ready(20000, &s_quit) ? 1 : 2;
        log_msg("red: %s (%u ms)", net == 1 ? "conectada" : "sin conexion", SDL_GetTicks() - t0);
    }
    if (net != 1)
        return DL_FAILED;
    // from here on a cover really is coming from the internet: the top bar says so
    (void)SDL_AtomicIncRef(&s_dl_active);
    int r = download_cover_net(tdb);
    (void)SDL_AtomicDecRef(&s_dl_active);
    return r;
}

bool lib_thumbs_ready(void)
{
    // miniaturas.bin read and turned into textures (or missing); never wait forever
    return (s_thumbs_loaded && !s_thumb_queue) || SDL_GetTicks() - s_start_ticks > 6000;
}

int lib_downloads_active(void)
{
    return SDL_AtomicGet(&s_dl_active);
}

static int download_cover_net(const char *tdb)
{
    static int logged;
    const char *regions[8];
    int nr = region_list(tdb, regions);
    static const char *kinds[2] = { "coverM", "cover" };
    bool net_error = false;
    for (int k = 0; k < 2; k++) {
        for (int i = 0; i < nr; i++) {
            char path[96];
            snprintf(path, sizeof(path), "/wiiu/%s/%s/%s.jpg", kinds[k], regions[i], tdb);
            uint8_t *data = NULL;
            size_t len = 0;
            int status = -1;
            uint32_t t0 = SDL_GetTicks();
            bool got = http_get("art.gametdb.com", path, &data, &len, &status);
            if (logged < 6 || (!got && status < 0)) {
                logged++;
                log_msg("GET %s -> %d (%u bytes, %u ms)", path, status, (unsigned)len, SDL_GetTicks() - t0);
            }
            if (got) {
                Image test;
                if (img_load_mem(data, len, &test)) {
                    img_free(&test);
                    char rel[48], out[256];
                    snprintf(rel, sizeof(rel), "covers/%s.jpg", tdb);
                    bool ok = file_write_atomic(data_path(rel, out, sizeof(out)), data, len);
                    free(data);
                    return ok ? DL_OK : DL_FAILED;
                }
                free(data);
            } else if (status < 0) {
                net_error = true;
                if (s_quit)
                    return DL_FAILED;
            }
        }
        if (net_error)
            break;
    }
    return net_error ? DL_FAILED : DL_NOT_FOUND;
}

static int downloader_main(void *arg)
{
    (void)arg;
    for (;;) {
        SDL_LockMutex(s_lock);
        while (!s_quit && s_dls.n == 0)
            SDL_CondWait(s_dl_cond, s_lock);
        if (s_quit) {
            SDL_UnlockMutex(s_lock);
            break;
        }
        int b = jobs_best(&s_dls);
        Job j = s_dls.v[b];
        s_dls.v[b] = s_dls.v[--s_dls.n];
        SDL_UnlockMutex(s_lock);

        Result r;
        memset(&r, 0, sizeof(r));
        r.type = JOB_DOWNLOAD;
        r.idx = j.idx;
        r.gen = j.gen;
        r.tid = j.tid;
        r.state = download_cover(&j);
        if (s_quit)
            break;
        push_result(&r);
        plat_sleep_ms(2);
    }
    log_msg("hilo de descargas terminado");
    return 0;
}

// ------------------------------------------------------------ config files

static Category *cat_add_raw(int id, int icon, uint32_t color, const char *logo, const char *name)
{
    if (L.ncats >= MAX_CATEGORIES || id < 0 || id > 63)
        return NULL;
    Category *c = &L.cats[L.ncats++];
    memset(c, 0, sizeof(*c));
    c->id = id;
    c->icon = icon >= 0 && icon < 64 ? icon : 0;
    c->color = color & 0xFFFFFF;
    str_copy(c->logo, sizeof(c->logo), logo && strcmp(logo, "-") ? logo : "");
    str_copy(c->name, sizeof(c->name), name);
    return c;
}

static void defaults(void)
{
    Settings *s = &L.set;
    memset(s, 0, sizeof(*s));
    s->view = 2;
    s->sort = 0;
    s->theme = 0;
    s->sfx = true;
    s->music = true;
    s->bg_art = true;
    s->download = true;
    s->wii_mode = WII_SHOW_TV;     // Wii games: Wii Remote on the TV
    s->gc_mode = WII_SHOW_GAMEPAD; // GameCube games (Nintendont): the GamePad
    s->show_hidden = false;
    s->show_clock = true;
    s->read_titles = true;
    s->start_tab = 2;
    s->al_encender = true;
    s->volver = true;
}

typedef struct StatLine {
    uint64_t tid;
    int64_t last, first;
    uint32_t count;
    int fav, hidden;
    uint64_t mask;
} StatLine;

static StatLine *s_stats;
static int s_nstats;

static void load_config(void)
{
    char path[256];
    char *txt = (char *)file_read(data_path("config.txt", path, sizeof(path)), NULL);
    if (!txt) {
        L.first_run = true;
        return;
    }
    char *save = NULL;
    for (char *line = strtok_r(txt, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *l = str_trim(line);
        if (!strncmp(l, "set ", 4)) {
            char key[32];
            long long v = 0;
            if (sscanf(l + 4, "%31s %lli", key, &v) != 2)
                continue;
            Settings *s = &L.set;
            if (!strcmp(key, "view")) s->view = v >= 0 && v <= 2 ? (int)v : 2;
            else if (!strcmp(key, "sort")) s->sort = (int)v;
            else if (!strcmp(key, "theme")) s->theme = (int)v;
            else if (!strcmp(key, "sfx")) s->sfx = v != 0;
            else if (!strcmp(key, "music")) s->music = v != 0;
            else if (!strcmp(key, "bg_art")) s->bg_art = v != 0;
            else if (!strcmp(key, "download")) s->download = v != 0;
            else if (!strcmp(key, "show_hidden")) s->show_hidden = v != 0;
            else if (!strcmp(key, "show_clock")) s->show_clock = v != 0;
            else if (!strcmp(key, "read_titles")) s->read_titles = v != 0;
            else if (!strcmp(key, "start_tab")) s->start_tab = (int)v;
            else if (!strcmp(key, "last_tab")) s->last_tab = (int)v;
            else if (!strcmp(key, "last_tid")) s->last_tid = (uint64_t)v;
            else if (!strcmp(key, "wii_mode")) s->wii_mode = v >= 0 && v <= 2 ? (int)v : WII_SHOW_TV;
            else if (!strcmp(key, "gc_mode")) s->gc_mode = v >= 0 && v <= 2 ? (int)v : WII_SHOW_GAMEPAD;
            else if (!strcmp(key, "al_encender")) s->al_encender = v != 0;
            else if (!strcmp(key, "volver")) s->volver = v != 0;
            else if (!strcmp(key, "lang")) s->lang = v >= 0 && v <= 2 ? (int)v : 0;
        } else if (!strncmp(l, "cat ", 4)) {
            int id, icon, off = 0;
            unsigned color;
            char logo[64];
            if (sscanf(l + 4, "%d %d %x %63s %n", &id, &icon, &color, logo, &off) >= 4 && off > 0)
                cat_add_raw(id, icon, color, logo, l + 4 + off);
        } else if (!strncmp(l, "game ", 5)) {
            StatLine st;
            unsigned long long tid, mask;
            long long last, first;
            unsigned count;
            int fav, hidden;
            if (sscanf(l + 5, "%llx %lld %u %d %d %llx %lld", &tid, &last, &count, &fav, &hidden, &mask,
                       &first) == 7) {
                st.tid = tid;
                st.last = last;
                st.count = count;
                st.fav = fav;
                st.hidden = hidden;
                st.mask = mask;
                st.first = first;
                s_stats = realloc(s_stats, sizeof(StatLine) * (s_nstats + 1));
                s_stats[s_nstats++] = st;
            }
        }
    }
    free(txt);
}

void lib_save(void)
{
    size_t cap = 4096 + (size_t)(L.count + s_norphans) * 96 + L.ncats * 160;
    char *buf = malloc(cap);
    if (!buf)
        return;
    size_t o = 0;
    Settings *s = &L.set;
#define OUT(...) o += (size_t)snprintf(buf + o, cap - o, __VA_ARGS__)
    OUT("# Pixmo Games: configuración (se puede borrar para volver a los valores iniciales)\n");
    OUT("set view %d\nset sort %d\nset theme %d\nset sfx %d\nset music %d\nset bg_art %d\n", s->view, s->sort,
        s->theme, s->sfx, s->music, s->bg_art);
    OUT("set download %d\nset show_hidden %d\nset show_clock %d\nset start_tab %d\nset last_tab %d\n",
        s->download, s->show_hidden, s->show_clock, s->start_tab, s->last_tab);
    OUT("set last_tid %lld\n", (long long)s->last_tid);
    OUT("set read_titles %d\n", s->read_titles);
    OUT("set wii_mode %d\n", s->wii_mode);
    OUT("set gc_mode %d\n", s->gc_mode);
    OUT("set al_encender %d\nset volver %d\n", s->al_encender, s->volver);
    OUT("set lang %d\n", s->lang);
    for (int i = 0; i < L.ncats; i++) {
        Category *c = &L.cats[i];
        OUT("cat %d %d %06x %s %s\n", c->id, c->icon, (unsigned)c->color, c->logo[0] ? c->logo : "-", c->name);
    }
    for (int i = 0; i < L.count; i++) {
        Game *g = &L.games[i];
        OUT("game %016llx %lld %u %d %d %llx %lld\n", (unsigned long long)g->tid, (long long)g->last_played,
            g->play_count, g->favorite, g->hidden, (unsigned long long)g->catmask, (long long)g->first_seen);
    }
    for (int i = 0; i < s_norphans; i++)
        OUT("%s\n", s_orphans[i]);
#undef OUT
    char path[256];
    if (!file_write_atomic(data_path("config.txt", path, sizeof(path)), buf, o))
        log_msg("could not write %s", path);
    free(buf);
    L.save_at = 0;
}

static void escape_into(const char *src, char *dst, size_t n)
{
    size_t o = 0;
    for (; *src && o + 3 < n; src++) {
        if (*src == '\n') { dst[o++] = '\\'; dst[o++] = 'n'; }
        else if (*src == '\t') dst[o++] = ' ';
        else if (*src == '\\') { dst[o++] = '\\'; dst[o++] = '\\'; }
        else if (*src != '\r') dst[o++] = *src;
    }
    dst[o] = 0;
}

static void unescape(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '\\' && s[1] == 'n') { *o++ = '\n'; s++; }
        else if (*s == '\\' && s[1] == '\\') { *o++ = '\\'; s++; }
        else *o++ = *s;
    }
    *o = 0;
}

static void derive_names(Game *g)
{
    str_copy(g->name1, sizeof(g->name1), g->name);
    for (char *p = g->name1; *p; p++)
        if (*p == '\n')
            *p = ' ';
    // collapse double spaces left by line breaks
    char *r = g->name1, *w = g->name1;
    for (; *r; r++)
        if (!(r[0] == ' ' && r[1] == ' '))
            *w++ = *r;
    *w = 0;
    const char *k = g->name1;
    // ignore leading articles/symbols for sorting
    str_fold(k, g->sortkey, sizeof(g->sortkey));
    g->tdb_id[0] = 0;
    size_t pl = strlen(g->product);
    if (pl >= 4) {
        const char *code = g->product + pl - 4;
        bool valid = true;
        for (int i = 0; i < 4; i++)
            if (!((code[i] >= 'A' && code[i] <= 'Z') || (code[i] >= '0' && code[i] <= '9')))
                valid = false;
        if (valid) {
            size_t cl = strlen(g->company);
            const char *cc = cl >= 2 ? g->company + cl - 2 : "01";
            snprintf(g->tdb_id, sizeof(g->tdb_id), "%.4s%.2s", code, cc);
        }
    }
}

static void load_cache(void)
{
    char path[256];
    char *txt = (char *)file_read(data_path("cache.txt", path, sizeof(path)), NULL);
    if (!txt)
        return;
    char *save = NULL;
    int lang = -1;
    for (char *line = strtok_r(txt, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        if (!strncmp(line, "ludocache ", 10)) {
            sscanf(line + 10, "%*d %d", &lang);
            if (lang != plat_language())
                break; // the console language changed: names must be read again
            continue;
        }
        if (line[0] != 'm' || line[1] != ' ')
            continue;
        unsigned long long tid;
        unsigned region, ext, drc;
        char product[32], company[8];
        long long dlchk, savet;
        int off = 0;
        if (sscanf(line + 2, "%llx %x %x %x %31s %7s %lld %lld %n", &tid, &region, &ext, &drc, product, company,
                   &dlchk, &savet, &off) < 8 || off <= 0)
            continue;
        Game *g = lib_find(tid);
        if (!g)
            continue;
        char *rest = line + 2 + off;
        char *tab = strchr(rest, '\t');
        if (!tab)
            continue;
        *tab = 0;
        str_copy(g->publisher, sizeof(g->publisher), rest);
        str_copy(g->name, sizeof(g->name), tab + 1);
        unescape(g->name);
        unescape(g->publisher);
        str_copy(g->product, sizeof(g->product), strcmp(product, "-") ? product : "");
        str_copy(g->company, sizeof(g->company), strcmp(company, "-") ? company : "");
        g->region = region;
        g->ext_flags = ext;
        g->drc_use = drc;
        g->dl_checked = dlchk;
        g->save_time = savet;
        derive_names(g);
        g->meta_ok = g->name[0] != 0;
    }
    free(txt);
}

// Game details from fichas.txt (optional, format in docs/DESARROLLO.md), one game per line:
// TID \t year \t players \t rating x10 \t genres \t publisher \t developer \t synopsis
static void load_fichas(void)
{
    char path[256];
    char *txt = (char *)file_read(data_path("fichas.txt", path, sizeof(path)), NULL);
    if (!txt)
        return;
    int n = 0;
    char *save = NULL;
    for (char *line = strtok_r(txt, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *f[9];
        int nf = 0;
        char *p = line;
        f[nf++] = p;
        while (nf < 9 && (p = strchr(p, '\t')) != NULL) {
            *p++ = 0;
            f[nf++] = p;
        }
        if (nf < 8)
            continue; // header or damaged line
        char *cr = strchr(f[nf - 1], '\r');
        if (cr)
            *cr = 0;
        Game *g = lib_find(strtoull(f[0], NULL, 16));
        if (!g)
            continue;
        str_copy(g->year, sizeof(g->year), f[1]);
        int pl = atoi(f[2]), rt = atoi(f[3]);
        g->players = (uint8_t)(pl > 0 && pl < 100 ? pl : 0);
        g->rating10 = (uint8_t)(rt > 0 && rt <= 50 ? rt : 0);
        str_copy(g->genres, sizeof(g->genres), f[4]);
        str_copy(g->lb_pub, sizeof(g->lb_pub), f[5]);
        str_copy(g->lb_dev, sizeof(g->lb_dev), f[6]);
        str_copy(g->synopsis, sizeof(g->synopsis), f[7]);
        unescape(g->synopsis);
        str_copy(g->lb_platform, sizeof(g->lb_platform), nf > 8 ? f[8] : "");
        g->ficha = true;
        n++;
    }
    free(txt);
    log_msg("fichas: %d juegos", n);
}

void lib_save_cache(void)
{
    size_t cap = 256 + (size_t)L.count * 900;
    char *buf = malloc(cap);
    if (!buf)
        return;
    size_t o = (size_t)snprintf(buf, cap, "ludocache 1 %d\n", plat_language());
    for (int i = 0; i < L.count; i++) {
        Game *g = &L.games[i];
        if (!g->meta_ok)
            continue;
        char name[600], pub[300];
        escape_into(g->name, name, sizeof(name));
        escape_into(g->publisher, pub, sizeof(pub));
        o += (size_t)snprintf(buf + o, cap - o, "m %016llx %x %x %x %s %s %lld %lld %s\t%s\n",
                              (unsigned long long)g->tid, (unsigned)g->region, (unsigned)g->ext_flags,
                              (unsigned)g->drc_use, g->product[0] ? g->product : "-",
                              g->company[0] ? g->company : "-", (long long)g->dl_checked,
                              (long long)g->save_time, pub, name);
        // for PC tools that read cache.txt: these are Wii or GameCube games
        if (g->pt.wii && o < cap)
            o += (size_t)snprintf(buf + o, cap - o, "p %016llx wii\n", (unsigned long long)g->tid);
        else if (g->pt.homebrew && o < cap)
            o += (size_t)snprintf(buf + o, cap - o, "p %016llx hb %s\n", (unsigned long long)g->tid, g->pt.path);
        if (o >= cap)
            break;
    }
    char path[256];
    file_write_atomic(data_path("cache.txt", path, sizeof(path)), buf, o < cap ? o : cap - 1);
    free(buf);
    s_cache_dirty = false;
}

// ------------------------------------------------------------ title list

static int cmp_tid(const void *a, const void *b)
{
    const Game *x = a, *y = b;
    return x->tid < y->tid ? -1 : x->tid > y->tid;
}

Game *lib_find(uint64_t tid)
{
    int lo = 0, hi = L.count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (L.games[mid].tid == tid)
            return &L.games[mid];
        if (L.games[mid].tid < tid)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return NULL;
}

static void build_list(void)
{
    PlatTitle *titles = NULL;
    int n = plat_list_titles(&titles);
    int games = 0;
    for (int i = 0; i < n; i++)
        if (titles[i].kind == TITLE_GAME)
            games++;
    L.games = calloc(games ? games : 1, sizeof(Game));
    L.count = 0;
    for (int i = 0; i < n; i++) {
        if (titles[i].kind != TITLE_GAME)
            continue;
        uint32_t hi = (uint32_t)(titles[i].tid >> 32);
        // only applications and demos (00050000 / 00050002), and homebrew bundles from the SD
        if (hi != 0x00050000 && hi != 0x00050002 && !titles[i].homebrew)
            continue;
        Game *g = &L.games[L.count++];
        g->pt = titles[i];
        g->tid = titles[i].tid;
        g->tint = rgb(0x7FA7C8);
    }
    qsort(L.games, L.count, sizeof(Game), cmp_tid);
    // the same title can be listed twice (e.g. disc + installed copy)
    int w = 0;
    for (int i = 0; i < L.count; i++)
        if (w == 0 || L.games[w - 1].tid != L.games[i].tid)
            L.games[w++] = L.games[i];
    L.count = w;
    for (int i = 0; i < n; i++) {
        uint32_t hi = (uint32_t)(titles[i].tid >> 32);
        uint64_t base = (titles[i].tid & 0xFFFFFFFFull) | (0x00050000ull << 32);
        Game *g = lib_find(base);
        if (!g)
            continue;
        if (titles[i].kind == TITLE_UPDATE || hi == 0x0005000E) {
            g->has_update = true;
            g->update_version = titles[i].version;
        } else if (titles[i].kind == TITLE_DLC || hi == 0x0005000C) {
            g->has_dlc = true;
        }
    }
    free(titles);
    log_msg("%d titles, %d games", n, L.count);
}

static void apply_stats(void)
{
    int64_t now = time_now();
    free(s_orphans);
    s_orphans = NULL;
    s_norphans = 0;
    for (int i = 0; i < s_nstats; i++) {
        StatLine *st = &s_stats[i];
        Game *g = lib_find(st->tid);
        if (!g) {
            char line[160];
            snprintf(line, sizeof(line), "game %016llx %lld %u %d %d %llx %lld", (unsigned long long)st->tid,
                     (long long)st->last, st->count, st->fav, st->hidden, (unsigned long long)st->mask,
                     (long long)st->first);
            s_orphans = realloc(s_orphans, sizeof(char *) * (s_norphans + 1));
            s_orphans[s_norphans++] = strdup(line);
            continue;
        }
        g->last_played = st->last;
        g->play_count = st->count;
        g->favorite = st->fav != 0;
        g->hidden = st->hidden != 0;
        g->catmask = st->mask;
        g->first_seen = st->first ? st->first : 1;
    }
    // titles never seen before are "new" (except on the very first run)
    for (int i = 0; i < L.count; i++) {
        Game *g = &L.games[i];
        if (!g->first_seen) {
            g->first_seen = L.first_run ? 1 : now;
            g->fresh = true;
        }
    }
}

// Homebrew tools (updaters, installers, stores...) are not games: when one shows up
// for the first time it starts hidden (Ajustes > Juegos ocultos brings it back).
static bool homebrew_tool(const char *name)
{
    static const char *const words[] = { "updater", "uninstaller", "installer", "app store", "appstore",
                                         "launcher", "ftp", "dumpling", "savemii", "ident", "nand",
                                         "mocha", "tiramisu", "aroma", "pixmo games" };
    char low[256];
    str_fold(name, low, sizeof(low));
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++)
        if (strstr(low, words[i]))
            return true;
    return false;
}

static void queue_initial_jobs(void)
{
    // the saved thumbnails come first: the cover rail is complete from the start
    s_thumbs_loaded = s_thumb_pass = false;
    submit(JOB_THUMBS, -1, -5000);
    // names first (alphabetical order is unknown yet: use title order)
    for (int i = 0; i < L.count; i++) {
        Game *g = &L.games[i];
        if (!g->meta_ok) {
            submit(JOB_META, i, -1000 + i);
            L.meta_pending++;
        }
        g->icon_state = ASSET_LOADING;
        submit(JOB_ICON, i, 1000 + i);
        if (!L.safe_mode) {
            g->save_state = ASSET_LOADING;
            submit(JOB_SAVE, i, 5000 + i);
        }
    }
    for (int i = 0; i < L.ncats; i++)
        if (L.cats[i].logo[0])
            cat_load_logo(&L.cats[i]);
}

void lib_init(void)
{
    memset(&L, 0, sizeof(L));
    defaults();
    char path[256];
    dir_make(plat_data_dir());
    dir_make(data_path("covers", path, sizeof(path)));
    dir_make(data_path("logos", path, sizeof(path)));

    s_lock = SDL_CreateMutex();
    s_cond = SDL_CreateCond();
    s_dl_cond = SDL_CreateCond();

    load_config();
    i18n_set(L.set.lang);
    if (L.first_run) {
        // starting categories, named in the interface language
        cat_add_raw(0, IC_CROSSHAIR, 0xE5533D, "-", tr("Disparos"));
        cat_add_raw(1, IC_SWORD, 0x43A047, "-", tr("Aventura"));
        cat_add_raw(2, IC_CAR, 0xFB8C00, "-", tr("Carreras"));
        cat_add_raw(3, IC_FLAME, 0x8E24AA, "-", tr("Peleas"));
        cat_add_raw(4, IC_GHOST, 0x00ACC1, "-", "Arcade");
    }
    L.loading_list = true;
    build_list();
    load_cache();
    load_fichas();
    mark_shared_ids();
    apply_stats();
    L.loading_list = false;
    L.list_dirty = true;

    // the previous session froze while reading files of the console
    char reading[256];
    data_path("leyendo.txt", reading, sizeof(reading));
    if (file_exists(reading)) {
        L.set.read_titles = false;
        remove(reading);
        log_msg("la sesion anterior se colgo leyendo archivos de la consola: lectura desactivada");
        lib_status("Iconos de la consola desactivados: Pixmo Games se colgó leyéndolos (ver Ajustes)", 9000);
        lib_save_soon();
    }
    // a marker file tells whether the previous session ended normally
    char marker[256];
    data_path("en_uso.txt", marker, sizeof(marker));
    if (file_exists(marker)) {
        L.safe_mode = true;
        log_msg("la sesion anterior no se cerro bien: modo seguro (sin descargas ni lectura de partidas)");
        lib_status("Modo seguro: Pixmo Games no se cerró bien la última vez (sin descargas)", 7000);
    }
    // back in Pixmo Games: an old "come back here" request for the plugin is done
    char volver[256];
    remove(data_path("volver.txt", volver, sizeof(volver)));
    static const char mark[] = "Pixmo Games esta abierta (si queda este archivo, no se cerro bien)\n";
    file_write_atomic(marker, mark, sizeof(mark) - 1);
    s_start_ticks = SDL_GetTicks();

    // generous stacks: image decoders keep sizable tables on the stack
    s_loader = plat_thread_start(loader_main, NULL, "carga", 256 * 1024, PLAT_CORE_WORK);
    s_downloader = plat_thread_start(downloader_main, NULL, "descarga", 128 * 1024, PLAT_CORE_WORK);
    log_msg("hilos: carga=%d descarga=%d", s_loader != NULL, s_downloader != NULL);
    queue_initial_jobs();
    if (L.first_run)
        lib_save();
}

static void free_game_assets(Game *g)
{
    SDL_Texture **t[8] = { &g->icon, &g->cover, &g->boot,  &g->boot_blur,
                           &g->boot_full, &g->fondo, &g->title_logo, &g->title_glow };
    for (int i = 0; i < 8; i++) {
        if (*t[i])
            SDL_DestroyTexture(*t[i]);
        *t[i] = NULL;
    }
    if (g->thumb)
        SDL_DestroyTexture(g->thumb);
    g->thumb = NULL;
    free(g->thumb_px);
    g->thumb_px = NULL;
}

static void stop_threads(void)
{
    SDL_LockMutex(s_lock);
    s_quit = true;
    SDL_CondBroadcast(s_cond);
    SDL_CondBroadcast(s_dl_cond);
    SDL_UnlockMutex(s_lock);
    net_abort();
    plat_thread_join(s_loader);
    plat_thread_join(s_downloader);
    s_loader = s_downloader = NULL;
}

// Before a game starts nothing may be reading the console's storage: the system
// prepares the game while the app is still alive, and reading that same title's
// files on the USB drive at that moment left the console on the white Wii U
// loading screen (Deus Ex, Dual Core, Darksiders II; Nov 2026). Waits for the job
// in progress (at most a cover or a boot image, < 1 s).
void lib_stop_for_launch(void)
{
    if (!s_loader && !s_downloader)
        return;
    uint32_t t0 = SDL_GetTicks();
    stop_threads();
    log_msg("trabajos detenidos para iniciar el juego (%u ms)", SDL_GetTicks() - t0);
}

// The launch did not happen: the app goes on, so the work threads come back.
void lib_resume_after_launch(void)
{
    if (s_loader || s_downloader)
        return;
    SDL_LockMutex(s_lock);
    s_quit = false;
    SDL_UnlockMutex(s_lock);
    net_resume();
    s_loader = plat_thread_start(loader_main, NULL, "carga", 256 * 1024, PLAT_CORE_WORK);
    s_downloader = plat_thread_start(downloader_main, NULL, "descarga", 128 * 1024, PLAT_CORE_WORK);
    log_msg("trabajos reanudados");
}

static void drop_results(void)
{
    SDL_LockMutex(s_lock);
    for (int i = 0; i < s_nres; i++) {
        img_free(&s_res[i].img);
        img_free(&s_res[i].img2);
        free(s_res[i].px565);
        for (int k = 0; k < s_res[i].nthumbs; k++)
            free(s_res[i].thumbs[k].px);
        free(s_res[i].thumbs);
    }
    for (int i = 0; i < s_jobs.n; i++)
        free(s_jobs.v[i].blob);
    s_nres = 0;
    s_jobs.n = 0;
    s_dls.n = 0;
    SDL_UnlockMutex(s_lock);
}

void lib_shutdown(void)
{
    log_msg("cerrando biblioteca");
    stop_threads();
    drop_results();
    if (L.save_at)
        lib_save();
    if (s_cache_dirty)
        lib_save_cache();
    // thumbnails made this session that were not written yet; a thumbnail file
    // still being turned into textures is complete on the card already
    if (s_thumbs_dirty && !s_thumb_queue)
        thumbs_write_now();
    thumb_queue_free();
    for (int i = 0; i < L.count; i++)
        free_game_assets(&L.games[i]);
    for (int i = 0; i < L.ncats; i++)
        if (L.cats[i].logo_tex)
            SDL_DestroyTexture(L.cats[i].logo_tex);
    free(L.games);
    L.games = NULL;
    L.count = 0;
    char marker[256];
    remove(data_path("en_uso.txt", marker, sizeof(marker)));
    if (s_reading_marker)
        reading_marker(false);
    log_msg("biblioteca cerrada");
}

void lib_rescan(void)
{
    // keep stats: write them, then rebuild everything
    lib_save();
    if (s_thumbs_dirty && !s_thumb_queue)
        thumbs_write_now(); // read back below, for the new list
    thumb_queue_free();
    SDL_LockMutex(s_lock);
    s_gen++;
    int kept = 0;
    for (int i = 0; i < s_jobs.n; i++) {
        if (s_jobs.v[i].type == JOB_WRITE)
            s_jobs.v[kept++] = s_jobs.v[i]; // a file being saved still is
        else if (s_jobs.v[i].type == JOB_THUMB)
            s_thumb_jobs--;
    }
    s_jobs.n = kept;
    s_dls.n = 0;
    SDL_UnlockMutex(s_lock);
    for (int i = 0; i < L.count; i++)
        free_game_assets(&L.games[i]);
    free(L.games);
    L.games = NULL;
    L.count = 0;
    free(s_stats);
    s_stats = NULL;
    s_nstats = 0;
    Settings keep = L.set;
    int ncats = L.ncats;
    Category cats[MAX_CATEGORIES];
    memcpy(cats, L.cats, sizeof(cats));
    load_config();
    L.set = keep;
    L.ncats = ncats;
    memcpy(L.cats, cats, sizeof(cats));
    L.meta_pending = 0;
    build_list();
    load_fichas();
    // names are read again from the console (cache ignored on purpose)
    apply_stats();
    L.list_dirty = true;
    for (int i = 0; i < L.count; i++) {
        L.games[i].dl_state = DL_NONE;
    }
    s_thumbs_loaded = s_thumb_pass = false;
    submit(JOB_THUMBS, -1, -5000);
    for (int i = 0; i < L.count; i++) {
        submit(JOB_META, i, -1000 + i);
        L.meta_pending++;
        L.games[i].icon_state = ASSET_LOADING;
        submit(JOB_ICON, i, 1000 + i);
        L.games[i].save_state = ASSET_LOADING;
        submit(JOB_SAVE, i, 5000 + i);
    }
}

// ------------------------------------------------------------ per frame

void lib_request_icons(const int *idx, int n)
{
    SDL_LockMutex(s_lock);
    for (int i = 0; i < n; i++) {
        if (idx[i] < 0 || idx[i] >= L.count)
            continue;
        Game *g = &L.games[idx[i]];
        if (g->icon_state == ASSET_NONE) {
            SDL_UnlockMutex(s_lock);
            g->icon_state = ASSET_LOADING;
            submit(JOB_ICON, idx[i], -1500 + i);
            SDL_LockMutex(s_lock);
        } else if (g->icon_state == ASSET_LOADING) {
            Job *j = jobs_find(&s_jobs, JOB_ICON, idx[i]);
            if (j && j->prio > -1500 + i)
                j->prio = -1500 + i;
            Job *m = jobs_find(&s_jobs, JOB_META, idx[i]);
            if (m && m->prio > -1600 + i)
                m->prio = -1600 + i;
        }
    }
    SDL_UnlockMutex(s_lock);
}

void lib_request_boot_full(int idx)
{
    if (idx < 0 || idx >= L.count)
        return;
    Game *g = &L.games[idx];
    if (g->boot_full_state != ASSET_NONE)
        return;
    g->boot_full_state = ASSET_LOADING;
    submit(JOB_BOOT_FULL, idx, -3000);
}

static bool in_list(const int *v, int n, int x)
{
    for (int i = 0; i < n; i++)
        if (v[i] == x)
            return true;
    return false;
}

// Drops the least recently wanted texture of one kind until at most `keep`
// remain. `slot` returns the texture pointer of a game, `used` its LRU stamp;
// `extra` (optional) is a companion texture freed together with it.
static void evict_kind(int keep, SDL_Texture **(*slot)(Game *), SDL_Texture **(*extra)(Game *),
                       uint32_t (*used)(const Game *), size_t state_off, const int *want, int nwant)
{
    for (;;) {
        int n = 0, oldest = -1;
        for (int i = 0; i < L.count; i++) {
            Game *g = &L.games[i];
            if (*slot(g)) {
                n++;
                if (!in_list(want, nwant, i) && (oldest < 0 || used(g) < used(&L.games[oldest])))
                    oldest = i;
            }
        }
        if (n <= keep || oldest < 0)
            break;
        Game *g = &L.games[oldest];
        SDL_DestroyTexture(*slot(g));
        *slot(g) = NULL;
        if (extra && *extra(g)) {
            SDL_DestroyTexture(*extra(g));
            *extra(g) = NULL;
        }
        *(int *)((char *)g + state_off) = ASSET_NONE;
    }
}

static SDL_Texture **slot_fondo(Game *g) { return &g->fondo; }
static SDL_Texture **slot_title(Game *g) { return &g->title_logo; }
static SDL_Texture **slot_glow(Game *g) { return &g->title_glow; }
static uint32_t used_fondo(const Game *g) { return g->fondo_used; }
static uint32_t used_title(const Game *g) { return g->title_used; }

static void evict(int keep_covers, int keep_boots, const int *want, int nwant, int nbig)
{
    // 1280x720 backgrounds are 3.6 MB each: only the focused game and its neighbours
    evict_kind(4, slot_fondo, NULL, used_fondo, offsetof(Game, fondo_state), want, nbig);
    evict_kind(8, slot_title, slot_glow, used_title, offsetof(Game, title_state), want, nbig);
    for (;;) {
        int ncov = 0, oldest = -1;
        for (int i = 0; i < L.count; i++) {
            Game *g = &L.games[i];
            if (g->cover) {
                ncov++;
                if (!in_list(want, nwant, i) && (oldest < 0 || g->cover_used < L.games[oldest].cover_used))
                    oldest = i;
            }
        }
        if (ncov <= keep_covers || oldest < 0)
            break;
        SDL_DestroyTexture(L.games[oldest].cover);
        L.games[oldest].cover = NULL;
        L.games[oldest].cover_state = ASSET_NONE;
    }
    for (;;) {
        int nb = 0, oldest = -1;
        for (int i = 0; i < L.count; i++) {
            Game *g = &L.games[i];
            if (g->boot) {
                nb++;
                if (!in_list(want, nwant, i) && (oldest < 0 || g->boot_used < L.games[oldest].boot_used))
                    oldest = i;
            }
        }
        if (nb <= keep_boots || oldest < 0)
            break;
        Game *g = &L.games[oldest];
        SDL_DestroyTexture(g->boot);
        if (g->boot_blur)
            SDL_DestroyTexture(g->boot_blur);
        g->boot = g->boot_blur = NULL;
        g->boot_state = ASSET_NONE;
    }
}

// Injected titles (e.g. N64 Virtual Console injects) reuse the product code of
// their base game, so an id used by several installed titles names none of them.
static void mark_shared_ids(void)
{
    for (int i = 0; i < L.count; i++)
        L.games[i].tdb_shared = false;
    for (int i = 0; i < L.count; i++) {
        Game *a = &L.games[i];
        if (!a->tdb_id[0])
            continue;
        for (int k = i + 1; k < L.count; k++)
            if (!strcmp(a->tdb_id, L.games[k].tdb_id))
                a->tdb_shared = L.games[k].tdb_shared = true;
    }
}

static bool queue_download(Game *g, int idx, int prio)
{
    // only once every name is known (shared ids must be detected first)
    if (!L.set.download || L.net_failed || L.safe_mode || L.meta_pending > 0 || !g->tdb_id[0] ||
        g->tdb_shared || g->dl_state != DL_NONE)
        return false;
    // the downloader looks in GameTDB's Wii U covers: Wii/GameCube games use
    // a cover from the SD card (covers/) or a generated one
    if (g->pt.wii)
        return false;
    // GameTDB said "no cover" recently: try again after two weeks
    if (g->dl_checked && time_now() - g->dl_checked < 14 * 86400)
        return false;
    g->dl_state = DL_QUEUED;
    L.downloads_total++;
    submit(JOB_DOWNLOAD, idx, prio);
    return true;
}

static void apply_result(Result *r)
{
    if (r->type == JOB_THUMBS) {
        // the saved thumbnails: they become textures a few per frame (lib_update)
        thumb_queue_free();
        s_thumb_queue = r->thumbs;
        s_thumb_queue_n = r->nthumbs;
        s_thumb_queue_pos = 0;
        s_thumbs_loaded = true;
        log_msg("miniaturas: %d leidas de miniaturas.bin", r->nthumbs);
        return;
    }
    if (r->type == JOB_WRITE)
        return;
    if (r->type == JOB_THUMB)
        s_thumb_jobs--;
    if (r->gen != s_gen || r->idx < 0 ||
        (r->type != JOB_LOGO && (r->idx >= L.count || L.games[r->idx].tid != r->tid))) {
        img_free(&r->img);
        img_free(&r->img2);
        free(r->px565);
        return;
    }
    if ((r->type == JOB_COVER || r->type == JOB_THUMB) && r->px565) {
        // a fresh thumbnail from the cover: keep it if it is new or the cover changed
        Game *tg = &L.games[r->idx];
        if (!tg->thumb || tg->thumb_stamp != (uint32_t)r->time) {
            bool known = !tg->thumb && thumb_in_file(tg->tid, (uint32_t)r->time);
            set_thumb(tg, r->px565, r->img2.w, r->img2.h, (uint32_t)r->time, &r->img2);
            r->px565 = NULL;
            if (!known) {
                s_thumbs_dirty = true;
                s_thumbs_changed = SDL_GetTicks();
            }
        }
    }
    if (r->type == JOB_LOGO) {
        Category *c = cat_by_id(r->idx);
        if (c) {
            if (c->logo_tex)
                SDL_DestroyTexture(c->logo_tex);
            c->logo_tex = r->ok ? img_to_texture(g_ren, &r->img) : NULL;
            c->logo_state = c->logo_tex ? ASSET_READY : ASSET_FAILED;
        }
        img_free(&r->img);
        return;
    }
    Game *g = &L.games[r->idx];
    switch (r->type) {
    case JOB_META:
        L.meta_pending--;
        if (r->ok) {
            PlatMeta *m = &r->meta;
            str_copy(g->name, sizeof(g->name), m->name[0] ? m->name : m->short_name);
            str_copy(g->publisher, sizeof(g->publisher), m->publisher);
            str_copy(g->product, sizeof(g->product), m->product_code);
            str_copy(g->company, sizeof(g->company), m->company_code);
            g->region = m->region;
            g->ext_flags = m->ext_flags;
            g->drc_use = m->drc_use;
            derive_names(g);
            g->meta_ok = true;
            if (g->fresh && g->pt.homebrew && homebrew_tool(g->name1)) {
                g->hidden = true;
                lib_save_soon();
                log_msg("homebrew: \"%s\" parece una herramienta: queda oculto", g->name1);
            }
        } else if (!g->name[0]) {
            snprintf(g->name, sizeof(g->name), tr("Título %08X"), (unsigned)(g->tid & 0xFFFFFFFF));
            derive_names(g);
            g->meta_ok = true;
        }
        s_cache_dirty = true;
        L.list_dirty = true;
        if (L.meta_pending <= 0) {
            mark_shared_ids();
            lib_save_cache();
        }
        break;
    case JOB_ICON:
        if (g->icon)
            SDL_DestroyTexture(g->icon);
        g->icon = r->ok ? img_to_texture(g_ren, &r->img) : NULL;
        g->icon_state = g->icon ? ASSET_READY : ASSET_FAILED;
        if (r->ok && !g->tint_ok) {
            g->tint = (Color){ r->tint[0], r->tint[1], r->tint[2], 255 };
            g->tint_ok = true;
        }
        break;
    case JOB_COVER:
        if (r->ok) {
            if (g->cover)
                SDL_DestroyTexture(g->cover);
            g->cover = img_to_texture(g_ren, &r->img);
            g->cover_state = g->cover ? ASSET_READY : ASSET_FAILED;
            g->cover_src = COVER_SRC_FILE;
        } else {
            g->cover_state = ASSET_FAILED;
            g->cover_src = COVER_SRC_NONE;
            queue_download(g, r->idx, 0);
        }
        break;
    case JOB_BOOT:
        if (r->ok) {
            if (g->boot)
                SDL_DestroyTexture(g->boot);
            if (g->boot_blur)
                SDL_DestroyTexture(g->boot_blur);
            g->boot = img_to_texture(g_ren, &r->img);
            g->boot_blur = img_to_texture(g_ren, &r->img2);
            g->boot_state = g->boot ? ASSET_READY : ASSET_FAILED;
            g->tint = (Color){ r->tint[0], r->tint[1], r->tint[2], 255 };
            g->tint_ok = true;
        } else {
            g->boot_state = ASSET_FAILED;
        }
        break;
    case JOB_BOOT_FULL:
        if (g->boot_full)
            SDL_DestroyTexture(g->boot_full);
        g->boot_full = r->ok ? img_to_texture(g_ren, &r->img) : NULL;
        g->boot_full_state = g->boot_full ? ASSET_READY : ASSET_FAILED;
        break;
    case JOB_FONDO:
        if (g->fondo)
            SDL_DestroyTexture(g->fondo);
        g->fondo = r->ok ? img_to_texture(g_ren, &r->img) : NULL;
        g->fondo_state = g->fondo ? ASSET_READY : ASSET_FAILED;
        break;
    case JOB_TITLE:
        if (g->title_logo)
            SDL_DestroyTexture(g->title_logo);
        if (g->title_glow)
            SDL_DestroyTexture(g->title_glow);
        g->title_logo = r->ok ? img_to_texture(g_ren, &r->img) : NULL;
        g->title_glow = r->ok && r->img2.px ? img_to_texture(g_ren, &r->img2) : NULL;
        g->title_state = g->title_logo ? ASSET_READY : ASSET_FAILED;
        break;
    case JOB_SAVE:
        g->save_state = ASSET_READY;
        if (r->ok && r->time != g->save_time) {
            g->save_time = r->time;
            s_cache_dirty = true;
            L.list_dirty = true;
        }
        break;
    case JOB_DOWNLOAD:
        L.downloads_done++;
        if (r->state == DL_EXISTS) {
            g->dl_state = DL_OK;
            if (g->cover_state == ASSET_FAILED)
                g->cover_state = ASSET_NONE;
        } else {
            g->dl_state = r->state;
        }
        if (r->state == DL_OK) {
            g->cover_state = ASSET_NONE; // reload from the new file
            g->dl_checked = 0;
        } else if (r->state == DL_NOT_FOUND) {
            g->dl_checked = time_now();
            s_cache_dirty = true;
        } else if (r->state == DL_FAILED && !L.net_failed) {
            L.net_failed = true;
            lib_status("Sin conexión a internet: se usan carátulas generadas", 4000);
        }
        if (L.downloads_done >= L.downloads_total) {
            L.downloads_done = L.downloads_total = 0;
            if (s_cache_dirty)
                lib_save_cache();
        }
        break;
    case JOB_THUMB:
        break; // handled above
    }
    img_free(&r->img);
    img_free(&r->img2);
    free(r->px565);
}

static void submit_write(const char *rel, uint8_t *blob, size_t size)
{
    Job j;
    memset(&j, 0, sizeof(j));
    j.type = JOB_WRITE;
    j.idx = -1;
    j.gen = s_gen;
    j.prio = 50000;
    data_path(rel, j.path, sizeof(j.path));
    j.blob = blob;
    j.blob_size = size;
    SDL_LockMutex(s_lock);
    jobs_push(&s_jobs, &j);
    SDL_CondSignal(s_cond);
    SDL_UnlockMutex(s_lock);
}

// thumbnails: saved ones become textures, missing ones are made, the file is rewritten
static void thumbs_update(void)
{
    uint32_t t0 = SDL_GetTicks();
    // at start the rail waits hidden for these (lib_thumbs_ready): a bigger share of
    // the frame makes it appear sooner; afterwards only a little per frame
    uint32_t budget = s_thumb_queue ? 12 : 4;
    for (int k = 0; k < 64 && s_thumb_queue && s_thumb_queue_pos < s_thumb_queue_n && SDL_GetTicks() - t0 < budget;
         k++) {
        ThumbEntry *t = &s_thumb_queue[s_thumb_queue_pos++];
        Game *g = lib_find(t->tid);
        if (g && !g->thumb) {
            set_thumb(g, t->px, t->w, t->h, t->stamp, NULL);
            t->px = NULL;
        }
        free(t->px);
        t->px = NULL;
    }
    if (s_thumb_queue && s_thumb_queue_pos >= s_thumb_queue_n) {
        free(s_thumb_queue);
        s_thumb_queue = NULL;
    }
    if (s_thumbs_loaded && !s_thumb_queue && !s_thumb_pass && L.meta_pending <= 0 &&
        SDL_GetTicks() - s_start_ticks > 3000) {
        s_thumb_pass = true;
        int q = 0;
        for (int i = 0; i < L.count; i++) {
            Game *g = &L.games[i];
            if (g->meta_ok && !g->thumb && !g->thumb_tried) {
                g->thumb_tried = true;
                s_thumb_jobs++;
                submit(JOB_THUMB, i, 40000 + i);
                q++;
            }
        }
        log_msg("miniaturas: %d por hacer", q);
    }
    if (s_thumbs_dirty && s_thumb_jobs <= 0 && !s_thumb_queue && SDL_GetTicks() - s_thumbs_changed > 4000) {
        size_t size = 0;
        uint8_t *blob = thumb_blob(&size);
        if (blob)
            submit_write("miniaturas.bin", blob, size);
        s_thumbs_dirty = false;
    }
}

void lib_update(const int *want, int nwant, int nbig, int focus_game)
{
    if (nbig > nwant)
        nbig = nwant;
    bool cine = L.set.view == 2;
    s_frame++;
    // apply a bounded number of results per frame to keep animation smooth
    uint32_t start = SDL_GetTicks();
    for (int applied = 0; applied < 24; applied++) {
        Result r;
        SDL_LockMutex(s_lock);
        if (s_nres == 0) {
            SDL_UnlockMutex(s_lock);
            break;
        }
        r = s_res[0];
        memmove(s_res, s_res + 1, sizeof(Result) * (--s_nres));
        SDL_UnlockMutex(s_lock);
        apply_result(&r);
        if (SDL_GetTicks() - start > 6)
            break;
    }

    thumbs_update();

    // large art for the focused game and its neighbours
    for (int k = 0; k < nwant; k++) {
        int i = want[k];
        if (i < 0 || i >= L.count)
            continue;
        Game *g = &L.games[i];
        bool big = k < nbig;
        g->cover_used = s_frame;
        if (big)
            g->boot_used = g->fondo_used = g->title_used = s_frame;
        if (!g->meta_ok)
            continue;
        if (g->cover_state == ASSET_NONE) {
            g->cover_state = ASSET_LOADING;
            submit(JOB_COVER, i, -1800 + k);
        }
        if (!big)
            continue;
        if (cine && g->fondo_state == ASSET_NONE) {
            g->fondo_state = ASSET_LOADING;
            submit(JOB_FONDO, i, -1790 + k);
        }
        if (cine && g->title_state == ASSET_NONE) {
            g->title_state = ASSET_LOADING;
            submit(JOB_TITLE, i, -1780 + k);
        }
        if (g->boot_state == ASSET_NONE) {
            g->boot_state = ASSET_LOADING;
            submit(JOB_BOOT, i, -1700 + k);
        }
    }
    // drop queued large-art jobs nobody wants anymore
    SDL_LockMutex(s_lock);
    for (int i = 0; i < s_jobs.n;) {
        Job *j = &s_jobs.v[i];
        bool stale = false;
        if (j->gen == s_gen && j->idx >= 0 && j->idx < L.count) {
            if (j->type == JOB_COVER)
                stale = !in_list(want, nwant, j->idx);
            else if (j->type == JOB_BOOT || j->type == JOB_FONDO || j->type == JOB_TITLE)
                stale = !in_list(want, nbig, j->idx);
        }
        if (stale) {
            Game *g = &L.games[j->idx];
            if (j->type == JOB_COVER)
                g->cover_state = ASSET_NONE;
            else if (j->type == JOB_BOOT)
                g->boot_state = ASSET_NONE;
            else if (j->type == JOB_FONDO)
                g->fondo_state = ASSET_NONE;
            else
                g->title_state = ASSET_NONE;
            s_jobs.v[i] = s_jobs.v[--s_jobs.n];
            continue;
        }
        i++;
    }
    // the focused game's cover download goes first
    if (focus_game >= 0) {
        Job *d = jobs_find(&s_dls, JOB_DOWNLOAD, focus_game);
        if (d)
            d->prio = -100;
    }
    SDL_UnlockMutex(s_lock);

    // background cover downloads for the whole library, once names are known
    // (the download thread checks which covers are already on the SD card)
    static bool bulk_done;
    if (!bulk_done && L.meta_pending <= 0 && L.set.download && !L.net_failed && !L.safe_mode &&
        SDL_GetTicks() - s_start_ticks > 6000) {
        bulk_done = true;
        int queued = 0;
        for (int i = 0; i < L.count; i++) {
            Game *g = &L.games[i];
            if (g->dl_state != DL_NONE || g->cover_state == ASSET_READY)
                continue;
            if (queue_download(g, i, 100 + i))
                queued++;
        }
        log_msg("descargas en cola: %d", queued);
    }

    evict(18, 8, want, nwant, nbig);

    if (L.save_at && SDL_GetTicks() >= L.save_at)
        lib_save();
}

// ------------------------------------------------------------ game helpers

int64_t game_last_time(const Game *g)
{
    return g->last_played > g->save_time ? g->last_played : g->save_time;
}

static const struct {
    const char *lb, *name, *shrt;
} k_systems[] = {
    [SYS_WIIU] = { "Nintendo Wii U", "", "Wii U" },
    [SYS_WII] = { "Nintendo Wii", "Wii", "Wii" },
    [SYS_GC] = { "Nintendo GameCube", "GameCube", "GC" },
    [SYS_N64] = { "Nintendo 64", "Nintendo 64", "N64" },
    [SYS_DS] = { "Nintendo DS", "Nintendo DS", "DS" },
    [SYS_NES] = { "Nintendo Entertainment System", "NES", "NES" },
    [SYS_SNES] = { "Super Nintendo Entertainment System", "Super Nintendo", "SNES" },
    [SYS_GBA] = { "Nintendo Game Boy Advance", "Game Boy Advance", "GBA" },
    [SYS_HOMEBREW] = { "", "Homebrew de Aroma", "HB" },
};

int game_system(const Game *g)
{
    if (g->pt.homebrew)
        return SYS_HOMEBREW;
    bool vc = !strncmp(g->product, "WUP-N-", 6);
    if (!g->pt.wii && !vc)
        return SYS_WIIU; // its fichas.txt platform may say otherwise: still a Wii U game
    if (g->pt.wii) {
        // the fichas.txt platform tells the system of an injected game best
        for (int s = SYS_WII; s <= SYS_GBA; s++)
            if (g->lb_platform[0] && !strcmp(g->lb_platform, k_systems[s].lb))
                return s;
        if (g->lb_platform[0] && strcmp(g->lb_platform, "Nintendo Wii U") != 0)
            return SYS_OTHER; // e.g. an emulator of another console injected as a Wii title
        return vc && g->product[6] == 'G' ? SYS_GC : SYS_WII; // GameCube discs have G... ids
    }
    switch (g->product[6]) {
    case 'N': return SYS_N64;
    case 'D': return SYS_DS;
    case 'F': return SYS_NES;
    case 'J': return SYS_SNES;
    case 'P': return SYS_GBA;
    default: break;
    }
    for (int s = SYS_WII; s <= SYS_GBA; s++)
        if (g->lb_platform[0] && !strcmp(g->lb_platform, k_systems[s].lb))
            return s;
    return SYS_OTHER;
}

const char *game_system_name(const Game *g)
{
    int s = game_system(g);
    if (s == SYS_OTHER)
        return g->lb_platform[0] ? g->lb_platform : tr("Consola Virtual");
    return tr(k_systems[s].name);
}

const char *game_system_short(const Game *g)
{
    int s = game_system(g);
    return s == SYS_OTHER ? "VC" : k_systems[s].shrt;
}

const char *game_device_name(const Game *g)
{
    if (!strcmp(g->pt.device, "usb"))
        return tr("Disco USB");
    if (!strcmp(g->pt.device, "odd"))
        return tr("Disco óptico");
    if (!strcmp(g->pt.device, "mlc"))
        return tr("Memoria del sistema");
    return g->pt.device;
}

void game_launched(Game *g)
{
    g->last_played = time_now();
    g->play_count++;
    g->first_seen = g->first_seen ? g->first_seen : 1;
    L.set.last_tid = g->tid;
    L.list_dirty = true;
    lib_save();
}

void game_retry_cover(Game *g)
{
    if (g->cover)
        SDL_DestroyTexture(g->cover);
    g->cover = NULL;
    g->cover_state = ASSET_NONE;
    g->dl_state = DL_NONE;
    g->dl_checked = 0;
    L.net_failed = false;
    if (g->cover_src == COVER_SRC_NONE) {
        int idx = (int)(g - L.games);
        g->dl_state = DL_NONE;
        queue_download(g, idx, -200);
    }
}

void lib_reload_title_art(void)
{
    for (int i = 0; i < L.count; i++) {
        Game *g = &L.games[i];
        if (g->icon_state == ASSET_FAILED || g->icon_state == ASSET_LOADING) {
            g->icon_state = ASSET_LOADING;
            submit(JOB_ICON, i, 1000 + i);
        }
        if (g->boot_state == ASSET_FAILED)
            g->boot_state = ASSET_NONE;
    }
}

void game_reset_stats(Game *g)
{
    g->last_played = 0;
    g->play_count = 0;
    L.list_dirty = true;
    lib_save_soon();
}

// ------------------------------------------------------------ categories

Category *cat_by_id(int id)
{
    for (int i = 0; i < L.ncats; i++)
        if (L.cats[i].id == id)
            return &L.cats[i];
    return NULL;
}

Category *cat_create(const char *name, int icon, uint32_t color)
{
    for (int id = 0; id < 64; id++) {
        if (!cat_by_id(id)) {
            // make sure no stale game still points to this id
            for (int i = 0; i < L.count; i++)
                L.games[i].catmask &= ~(1ull << id);
            Category *c = cat_add_raw(id, icon, color, "-", name);
            lib_save_soon();
            return c;
        }
    }
    return NULL;
}

void cat_delete(int id)
{
    for (int i = 0; i < L.ncats; i++) {
        if (L.cats[i].id != id)
            continue;
        if (L.cats[i].logo_tex)
            SDL_DestroyTexture(L.cats[i].logo_tex);
        memmove(&L.cats[i], &L.cats[i + 1], sizeof(Category) * (L.ncats - i - 1));
        L.ncats--;
        break;
    }
    for (int i = 0; i < L.count; i++)
        L.games[i].catmask &= ~(1ull << id);
    L.list_dirty = true;
    lib_save_soon();
}

void cat_move(int id, int delta)
{
    for (int i = 0; i < L.ncats; i++) {
        if (L.cats[i].id != id)
            continue;
        int j = i + delta;
        if (j < 0 || j >= L.ncats)
            return;
        Category t = L.cats[i];
        L.cats[i] = L.cats[j];
        L.cats[j] = t;
        lib_save_soon();
        return;
    }
}

int cat_count_games(int id)
{
    int n = 0;
    for (int i = 0; i < L.count; i++)
        if ((L.games[i].catmask >> id) & 1 && (!L.games[i].hidden || L.set.show_hidden))
            n++;
    return n;
}

void cat_load_logo(Category *c)
{
    if (!c->logo[0]) {
        if (c->logo_tex)
            SDL_DestroyTexture(c->logo_tex);
        c->logo_tex = NULL;
        c->logo_state = ASSET_NONE;
        return;
    }
    c->logo_state = ASSET_LOADING;
    submit(JOB_LOGO, c->id, -1900);
}

static int cmp_names(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

int logos_list(char names[][64], int max)
{
    char path[256];
    DIR *d = opendir(data_path("logos", path, sizeof(path)));
    if (!d)
        return 0;
    int n = 0;
    struct dirent *e;
    while (n < max && (e = readdir(d)) != NULL) {
        const char *dot = strrchr(e->d_name, '.');
        if (!dot || e->d_name[0] == '.' || strlen(e->d_name) >= 64)
            continue;
        char ext[8];
        str_fold(dot, ext, sizeof(ext));
        if (strcmp(ext, ".png") && strcmp(ext, ".jpg") && strcmp(ext, ".jpeg"))
            continue;
        str_copy(names[n++], 64, e->d_name);
    }
    closedir(d);
    qsort(names, n, 64, cmp_names);
    return n;
}

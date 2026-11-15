// Atlas de glifos: se rasterizan bajo demanda (stb_truetype) en un buffer
// RGBA blanco con alfa y solo la región modificada se sube a la GPU.
#include "font.h"
#include "gfx.h"
#include "i18n.h"

#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION

#include "stb_truetype.h"

#define ATLAS_W 1024
#define ATLAS_H 1024
#define GCAP 8192
#define BASELINE 0.90f   // baseline position inside the line box, in em
#define LINE_H 1.28f     // line box height, in em

typedef struct Glyph {
    int cp;
    short size;
    short x, y, w, h;
    short xoff, yoff;
    float adv;
    int gidx;
} Glyph;

typedef struct Shelf {
    int y, h, x;
} Shelf;

static stbtt_fontinfo s_font;
static bool s_ok;
static uint8_t *s_px;
static SDL_Texture *s_tex;
static int s_dx0 = ATLAS_W, s_dy0 = ATLAS_H, s_dx1, s_dy1;
static Glyph s_glyphs[GCAP];
static int s_nglyphs;
static Shelf s_shelves[256];
static int s_nshelves, s_next_y;
static unsigned s_gen;
static float s_scale[256];

static float scale_for(int size)
{
    if (size < 1) size = 1;
    if (size > 255) size = 255;
    if (s_scale[size] == 0)
        s_scale[size] = stbtt_ScaleForMappingEmToPixels(&s_font, (float)size);
    return s_scale[size];
}

static void atlas_reset(void)
{
    // white everywhere, alpha 0 (white edges avoid dark fringes when filtering)
    uint32_t *p32 = (uint32_t *)s_px;
    const uint8_t white0[4] = { 255, 255, 255, 0 };
    uint32_t v;
    memcpy(&v, white0, 4);
    for (size_t i = 0; i < (size_t)ATLAS_W * ATLAS_H; i++)
        p32[i] = v;
    memset(s_glyphs, 0, sizeof(s_glyphs));
    s_nglyphs = 0;
    s_nshelves = 0;
    s_next_y = 0;
    s_dx0 = 0;
    s_dy0 = 0;
    s_dx1 = ATLAS_W;
    s_dy1 = ATLAS_H;
    s_gen++;
}

bool font_init(const void *ttf, size_t size)
{
    (void)size;
    if (!stbtt_InitFont(&s_font, ttf, stbtt_GetFontOffsetForIndex(ttf, 0)))
        return false;
    s_px = malloc((size_t)ATLAS_W * ATLAS_H * 4);
    if (!s_px)
        return false;
    s_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, ATLAS_W, ATLAS_H);
    if (!s_tex)
        return false;
    SDL_SetTextureBlendMode(s_tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(s_tex, SDL_ScaleModeLinear);
    atlas_reset();
    s_ok = true;
    return true;
}

void font_shutdown(void)
{
    if (s_tex)
        SDL_DestroyTexture(s_tex);
    free(s_px);
    s_tex = NULL;
    s_px = NULL;
    s_ok = false;
}

static bool atlas_alloc(int w, int h, int *ox, int *oy)
{
    w += 2;
    h += 2;
    for (int i = 0; i < s_nshelves; i++) {
        Shelf *s = &s_shelves[i];
        if (h <= s->h && h * 10 >= s->h * 7 && s->x + w <= ATLAS_W) {
            *ox = s->x + 1;
            *oy = s->y + 1;
            s->x += w;
            return true;
        }
    }
    if (s_next_y + h > ATLAS_H || s_nshelves >= 256 || w > ATLAS_W)
        return false;
    Shelf *s = &s_shelves[s_nshelves++];
    s->y = s_next_y;
    s->h = h;
    s->x = w;
    s_next_y += h;
    *ox = 1;
    *oy = s->y + 1;
    return true;
}

static int quant_size(int size)
{
    // big animated text would otherwise create a new glyph set per pixel size
    return size > 28 ? (size + 2) / 4 * 4 : size;
}

static const Glyph *get_glyph(int size, int cp)
{
    static int depth;
    unsigned hsh = ((unsigned)cp * 2654435761u) ^ ((unsigned)size * 40503u);
    unsigned i = hsh & (GCAP - 1);
    for (;;) {
        Glyph *g = &s_glyphs[i];
        if (g->size == 0)
            break;
        if (g->cp == cp && g->size == size)
            return g;
        i = (i + 1) & (GCAP - 1);
    }
    if (s_nglyphs > GCAP * 3 / 4 && depth == 0) {
        atlas_reset();
        depth++;
        const Glyph *g = get_glyph(size, cp);
        depth--;
        return g;
    }
    int gidx = stbtt_FindGlyphIndex(&s_font, cp);
    if (gidx == 0 && cp > ' ') {
        // missing glyphs fall back to a similar character
        int alt = cp == 0x2026 ? '.' : (cp == 0x2019 || cp == 0x2018) ? '\'' :
                  (cp == 0x201C || cp == 0x201D) ? '"' : (cp == 0x2013 || cp == 0x2014) ? '-' : '?';
        gidx = stbtt_FindGlyphIndex(&s_font, alt);
    }
    float sc = scale_for(size);
    int adv, lsb, x0, y0, x1, y1;
    stbtt_GetGlyphHMetrics(&s_font, gidx, &adv, &lsb);
    stbtt_GetGlyphBitmapBox(&s_font, gidx, sc, sc, &x0, &y0, &x1, &y1);
    int w = x1 - x0, h = y1 - y0;
    int ax = 0, ay = 0;
    if (w > 256 || h > 256)
        w = h = 0; // absurd bounding box (broken glyph)
    if (w > 0 && h > 0) {
        if (!atlas_alloc(w, h, &ax, &ay)) {
            if (depth > 0) {
                w = h = 0; // does not fit even in an empty atlas: draw nothing
            } else {
                atlas_reset();
                depth++;
                const Glyph *g = get_glyph(size, cp);
                depth--;
                return g;
            }
        }
        uint8_t *tmp = malloc((size_t)w * h);
        if (tmp) {
            stbtt_MakeGlyphBitmap(&s_font, tmp, w, h, w, sc, sc, gidx);
            for (int yy = 0; yy < h; yy++) {
                uint8_t *row = s_px + ((size_t)(ay + yy) * ATLAS_W + ax) * 4;
                for (int xx = 0; xx < w; xx++)
                    row[xx * 4 + 3] = tmp[yy * w + xx];
            }
            free(tmp);
        }
        if (ax - 1 < s_dx0) s_dx0 = ax - 1;
        if (ay - 1 < s_dy0) s_dy0 = ay - 1;
        if (ax + w + 1 > s_dx1) s_dx1 = ax + w + 1;
        if (ay + h + 1 > s_dy1) s_dy1 = ay + h + 1;
    }
    Glyph *g = &s_glyphs[i];
    g->cp = cp;
    g->size = (short)size;
    g->x = (short)ax;
    g->y = (short)ay;
    g->w = (short)(w > 0 ? w : 0);
    g->h = (short)(h > 0 ? h : 0);
    g->xoff = (short)x0;
    g->yoff = (short)y0;
    g->adv = adv * sc;
    g->gidx = gidx;
    s_nglyphs++;
    return g;
}

static void flush_atlas(void)
{
    if (s_dx1 <= s_dx0 || s_dy1 <= s_dy0)
        return;
    if (s_dx0 < 0) s_dx0 = 0;
    if (s_dy0 < 0) s_dy0 = 0;
    if (s_dx1 > ATLAS_W) s_dx1 = ATLAS_W;
    if (s_dy1 > ATLAS_H) s_dy1 = ATLAS_H;
    // upload whole rows: a full-width rect keeps the copy contiguous
    SDL_Rect r = { 0, s_dy0, ATLAS_W, s_dy1 - s_dy0 };
    SDL_UpdateTexture(s_tex, &r, s_px + (size_t)s_dy0 * ATLAS_W * 4, ATLAS_W * 4);
    s_dx0 = ATLAS_W;
    s_dy0 = ATLAS_H;
    s_dx1 = s_dy1 = 0;
}

void font_prewarm(const int *sizes, int count)
{
    if (!s_ok)
        return;
    static const char *chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
                               " .,:;!?¡¿-+()/'\"&·...áéíóúñÁÉÍÓÚÑü%#";
    for (int i = 0; i < count; i++) {
        const char *p = chars;
        int cp;
        while ((cp = utf8_decode(&p)) != 0)
            get_glyph(sizes[i], cp);
    }
    flush_atlas();
}

float text_line_height(int size)
{
    size = quant_size(size);
    return size * LINE_H;
}

float text_width(int size, const char *s)
{
    s = tr(s); // UI text is written in Spanish: shown in the interface language
    size = quant_size(size);
    if (!s_ok || !s)
        return 0;
    float w = 0, best = 0;
    int cp, prev = 0;
    const Glyph *pg = NULL;
    float sc = scale_for(size);
    while ((cp = utf8_decode(&s)) != 0) {
        if (cp == '\n') {
            if (w > best) best = w;
            w = 0;
            pg = NULL;
            continue;
        }
        const Glyph *g = get_glyph(size, cp);
        if (pg)
            w += stbtt_GetGlyphKernAdvance(&s_font, pg->gidx, g->gidx) * sc;
        w += g->adv;
        pg = g;
        prev = cp;
    }
    (void)prev;
    return w > best ? w : best;
}

#define MAXQ 512
static SDL_Vertex s_v[MAXQ * 4];
static int s_i[MAXQ * 6];

static void (*s_xform)(float *x, float *y, void *ud);
static void *s_xform_ud;

void text_set_transform(void (*fn)(float *x, float *y, void *ud), void *ud)
{
    s_xform = fn;
    s_xform_ud = ud;
}

float text_draw(int size, const char *s, float x, float y, Color c, int align)
{
    s = tr(s);
    size = quant_size(size);
    if (!s_ok || !s || !s[0] || gfx_alpha() <= 0.002f)
        return 0;
    float w = text_width(size, s);
    if (align == ALIGN_CENTER)
        x -= w * 0.5f;
    else if (align == ALIGN_RIGHT)
        x -= w;
    x = floorf(x + 0.5f);
    float base = floorf(y + size * BASELINE + 0.5f);
    float sc = scale_for(size);
    SDL_Color vc = { c.r, c.g, c.b, (uint8_t)clampf(c.a * gfx_alpha() + 0.5f, 0, 255) };
    const float iw = 1.0f / ATLAS_W, ih = 1.0f / ATLAS_H;

    for (int attempt = 0; attempt < 2; attempt++) {
        unsigned gen = s_gen;
        const char *p = s;
        float pen = x, line_y = base;
        int nq = 0, cp;
        const Glyph *pg = NULL;
        while ((cp = utf8_decode(&p)) != 0 && nq < MAXQ) {
            if (cp == '\n') {
                pen = x;
                line_y += floorf(size * LINE_H + 0.5f);
                pg = NULL;
                continue;
            }
            const Glyph *g = get_glyph(size, cp);
            if (pg)
                pen += stbtt_GetGlyphKernAdvance(&s_font, pg->gidx, g->gidx) * sc;
            if (g->w > 0) {
                float gx = floorf(pen + 0.5f) + g->xoff, gy = line_y + g->yoff;
                float u0 = g->x * iw, v0 = g->y * ih, u1 = (g->x + g->w) * iw, v1 = (g->y + g->h) * ih;
                SDL_Vertex *v = &s_v[nq * 4];
                v[0].position.x = gx;          v[0].position.y = gy;
                v[1].position.x = gx + g->w;   v[1].position.y = gy;
                v[2].position.x = gx + g->w;   v[2].position.y = gy + g->h;
                v[3].position.x = gx;          v[3].position.y = gy + g->h;
                v[0].tex_coord.x = u0; v[0].tex_coord.y = v0;
                v[1].tex_coord.x = u1; v[1].tex_coord.y = v0;
                v[2].tex_coord.x = u1; v[2].tex_coord.y = v1;
                v[3].tex_coord.x = u0; v[3].tex_coord.y = v1;
                v[0].color = v[1].color = v[2].color = v[3].color = vc;
                int *ix = &s_i[nq * 6], b = nq * 4;
                ix[0] = b; ix[1] = b + 1; ix[2] = b + 2;
                ix[3] = b; ix[4] = b + 2; ix[5] = b + 3;
                nq++;
            }
            pen += g->adv;
            pg = g;
        }
        if (gen != s_gen)
            continue; // the atlas was rebuilt while laying out: redo
        if (s_xform)
            for (int k = 0; k < nq * 4; k++)
                s_xform(&s_v[k].position.x, &s_v[k].position.y, s_xform_ud);
        flush_atlas();
        gfx_geometry(s_tex, s_v, nq * 4, s_i, nq * 6);
        break;
    }
    return w;
}

float text_draw_shadow(int size, const char *s, float x, float y, Color c, Color shadow, int align)
{
    text_draw(size, s, x, y + 2, shadow, align);
    return text_draw(size, s, x, y, c, align);
}

void text_fit(int size, const char *s, float maxw, char *out, size_t n)
{
    str_copy(out, n, tr(s));
    if (text_width(size, out) <= maxw)
        return;
    size_t len = strlen(out);
    while (len > 0) {
        len--;
        while (len > 0 && ((unsigned char)out[len] & 0xC0) == 0x80)
            len--;
        char tmp[512];
        size_t keep = len < sizeof(tmp) - 4 ? len : sizeof(tmp) - 4;
        memcpy(tmp, out, keep);
        while (keep > 0 && tmp[keep - 1] == ' ')
            keep--;
        memcpy(tmp + keep, "...", 4);
        if (text_width(size, tmp) <= maxw || len == 0) {
            str_copy(out, n, tmp);
            return;
        }
    }
}

int text_wrap(int size, const char *s, float maxw, int maxlines, char lines[][256])
{
    s = tr(s);
    int nl = 0;
    char cur[256] = "";
    const char *p = s;
    while (*p && nl < maxlines) {
        // next word (up to a space or newline)
        const char *we = p;
        while (*we && *we != ' ' && *we != '\n')
            we++;
        char word[256];
        size_t wl = (size_t)(we - p) < 255 ? (size_t)(we - p) : 255;
        memcpy(word, p, wl);
        word[wl] = 0;
        char trial[512];
        snprintf(trial, sizeof(trial), "%s%s%s", cur, cur[0] ? " " : "", word);
        if (!cur[0] || text_width(size, trial) <= maxw) {
            str_copy(cur, sizeof(cur), trial);
        } else {
            str_copy(lines[nl++], 256, cur);
            str_copy(cur, sizeof(cur), word);
            if (nl == maxlines)
                break;
        }
        p = we;
        if (*p == '\n') {
            if (nl < maxlines && cur[0]) {
                str_copy(lines[nl++], 256, cur);
                cur[0] = 0;
            }
            p++;
        } else if (*p == ' ') {
            p++;
        }
    }
    if (nl < maxlines && cur[0])
        str_copy(lines[nl++], 256, cur);
    else if (nl == maxlines && (cur[0] || *p)) {
        // text left over: ellipsize the last line
        char tmp[512];
        snprintf(tmp, sizeof(tmp), "%s %s", lines[nl - 1], cur[0] ? cur : p);
        text_fit(size, tmp, maxw, lines[nl - 1], 256);
    }
    for (int i = 0; i < nl; i++)
        if (text_width(size, lines[i]) > maxw)
            text_fit(size, lines[i], maxw, lines[i], 256);
    return nl;
}

// Vista "Cine" (al estilo de Aurora en Xbox 360): el fondo del juego llena la
// pantalla, la caja se ve en 3D con su lomo, la ficha del juego va a la
// derecha y abajo hay un carrusel de carátulas. Se elige en Ajustes > Vista.
//
// Arte que usa (en la SD, junto a config.txt): covers/<TID>.jpg, fondos/<TID>.jpg,
// titulos/<TID>.png y fichas.txt (docs/DESARROLLO.md). Sin fondo usa la
// imagen de arranque del juego; sin logo, el nombre.
#include "ui_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CASE_CX  300.0f
#define CASE_CY  376.0f
#define CASE_H   560.0f   // height of a Wii U case
#define CASE_YAW 22.0f    // degrees: the spine faces the viewer a little
#define PERSP    1600.0f

#define INFO_X 600.0f
#define INFO_W 620.0f

#define RAIL_X0     664.0f
#define RAIL_X1     1150.0f
#define RAIL_BOTTOM 684.0f
#define RAIL_H      78.0f
#define RAIL_HSEL   98.0f
#define RAIL_GAP    10.0f

static const Color k_bg = { 5, 7, 12, 255 };

// ------------------------------------------------------------ small helpers

static Color bgc(float a)
{
    return col_alpha(k_bg, a);
}

static Color white(float a)
{
    return rgba(0xFFFFFF, a);
}

// uppercase for the kicker line, Spanish letters included (á -> Á, ñ -> Ñ)
static void upper_es(const char *in, char *out, size_t n)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 2 < n; p++) {
        unsigned char c = *p;
        if (c >= 'a' && c <= 'z') {
            out[o++] = (char)(c - 32);
        } else if (c == 0xC3 && p[1] >= 0xA0 && p[1] <= 0xBE && p[1] != 0xB7) {
            out[o++] = (char)c;
            out[o++] = (char)(p[1] - 0x20);
            p++;
        } else {
            out[o++] = (char)c;
        }
    }
    out[o] = 0;
}

// The thumbnail is always there (miniaturas.bin) and has the cover's shape:
// the rail and the case keep their size when the big cover arrives or leaves.
static float cover_aspect(Game *g)
{
    if (g && g->thumb && g->thumb_w && g->thumb_h)
        return (float)g->thumb_w / (float)g->thumb_h;
    if (g && g->cover) {
        int w = 0, h = 0;
        SDL_QueryTexture(g->cover, NULL, NULL, &w, &h);
        if (w > 0 && h > 0)
            return (float)w / (float)h;
    }
    return 0.711f;
}

typedef struct CaseDim {
    float w, h, d;
    int sys; // SYS_*: look of the spine
} CaseDim;

static CaseDim case_dims(Game *g)
{
    float a = cover_aspect(g);
    CaseDim c;
    if (a < 0.8f) {          // Wii U case
        c.h = CASE_H;
        c.w = c.h * a;
        c.d = c.h * 0.054f;
    } else if (a <= 1.05f) { // DS-like, almost square
        c.h = CASE_H * 0.8f;
        c.w = c.h * a;
        c.d = c.h * 0.07f;
    } else {                 // wide boxes (Nintendo 64, Super Nintendo)
        c.w = CASE_H * 0.84f;
        c.h = c.w / a;
        c.d = c.h * 0.12f;
    }
    if (c.w > 480) {
        float k = 480 / c.w;
        c.w *= k;
        c.h *= k;
        c.d *= k;
    }
    c.sys = game_system(g);
    return c;
}

void cine_case_rect(Game *g, float *x, float *y, float *w, float *h)
{
    CaseDim c = case_dims(g);
    *w = c.w;
    *h = c.h;
    *x = CASE_CX - c.w * 0.5f;
    *y = CASE_CY - c.h * 0.5f;
}

// ------------------------------------------------------------ 3D case

typedef struct Box3 {
    float w, h, d;
    float ca, sa;   // yaw
    float dx;       // slide (transitions)
    float alpha;
} Box3;

static void project(const Box3 *b, float x, float y, float z, float *sx, float *sy)
{
    float xr = x * b->ca + z * b->sa;
    float zr = -x * b->sa + z * b->ca;
    float k = PERSP / (PERSP - zr);
    *sx = CASE_CX + b->dx + xr * k;
    *sy = CASE_CY + y * k;
}

static SDL_Color vcolor(Color c, float a)
{
    SDL_Color o = { c.r, c.g, c.b, (uint8_t)clampf(c.a * a * gfx_alpha() + 0.5f, 0, 255) };
    return o;
}

// one flat quad on the box, corners in box coordinates, one colour each
static void quad3(const Box3 *b, const float p[4][3], const Color c[4])
{
    SDL_Vertex v[4];
    for (int i = 0; i < 4; i++) {
        project(b, p[i][0], p[i][1], p[i][2], &v[i].position.x, &v[i].position.y);
        v[i].color = vcolor(c[i], b->alpha);
        v[i].tex_coord.x = v[i].tex_coord.y = 0;
    }
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    gfx_geometry(NULL, v, 4, idx, 6);
}

// The cover on the front face. A grid instead of two triangles keeps the
// texture from bending along the diagonal (SDL maps textures affinely).
static void draw_front(const Box3 *b, SDL_Texture *tex)
{
    enum { NX = 6, NY = 8 };
    SDL_Vertex v[(NX + 1) * (NY + 1)];
    int idx[NX * NY * 6];
    SDL_Color col = vcolor(rgb(0xFFFFFF), b->alpha);
    for (int j = 0; j <= NY; j++)
        for (int i = 0; i <= NX; i++) {
            float u = (float)i / NX, t = (float)j / NY;
            SDL_Vertex *p = &v[j * (NX + 1) + i];
            project(b, -b->w * 0.5f + u * b->w, -b->h * 0.5f + t * b->h, b->d * 0.5f, &p->position.x,
                    &p->position.y);
            p->color = col;
            p->tex_coord.x = u;
            p->tex_coord.y = t;
        }
    int n = 0;
    for (int j = 0; j < NY; j++)
        for (int i = 0; i < NX; i++) {
            int a = j * (NX + 1) + i, c = a + NX + 1;
            idx[n++] = a;
            idx[n++] = a + 1;
            idx[n++] = c + 1;
            idx[n++] = a;
            idx[n++] = c + 1;
            idx[n++] = c;
        }
    gfx_geometry(tex, v, (NX + 1) * (NY + 1), idx, n);

    // plastic gloss and the shade of the hinge
    float x0 = -b->w * 0.5f, x1 = b->w * 0.5f, y0 = -b->h * 0.5f, y1 = b->h * 0.5f, z = b->d * 0.5f;
    float gp[4][3] = { { x0, y0, z }, { x1, y0, z }, { x1, y1, z }, { x0, y1, z } };
    Color gc[4] = { white(0.22f), white(0.05f), white(0.0f), white(0.03f) };
    quad3(b, gp, gc);
    float hw = b->w * 0.045f;
    float hp[4][3] = { { x0, y0, z }, { x0 + hw, y0, z }, { x0 + hw, y1, z }, { x0, y1, z } };
    Color hc[4] = { rgba(0, 0.2f), rgba(0, 0), rgba(0, 0), rgba(0, 0.2f) };
    quad3(b, hp, hc);
}

typedef struct SpineText {
    const Box3 *b;
    float y0, z0;
    bool across; // label across the spine (band) instead of along it
} SpineText;

static void spine_xform(float *x, float *y, void *ud)
{
    const SpineText *s = ud;
    float px = -s->b->w * 0.5f, py, pz;
    if (s->across) {
        py = s->y0 + *y;
        pz = s->z0 + *x;
    } else {
        // turned 90° clockwise: reads from the top down, letters face the front
        py = s->y0 + *x;
        pz = s->z0 - *y;
    }
    project(s->b, px, py, pz, x, y);
}

static void draw_spine(const Box3 *b, Game *g, int sys)
{
    float x = -b->w * 0.5f, y0 = -b->h * 0.5f, y1 = b->h * 0.5f;
    float zb = -b->d * 0.5f, zf = b->d * 0.5f, zm = 0;
    float band = b->h * 0.095f;
    // white plastic cases (Wii U, Wii) and black ones (GameCube, retro boxes)
    Color back = rgb(0xC9D0D8), mid = rgb(0xFFFFFF), front = rgb(0xDDE2E8);
    Color b1 = rgb(0x25B0E8), b2 = rgb(0x0A7CC0), name = rgb(0x2A2F35), label_c = rgb(0xFFFFFF);
    if (sys == SYS_WII) {
        b1 = rgb(0xDCE0E5);
        b2 = rgb(0xAEB4BB);
        label_c = rgb(0x4A5058);
    } else if (sys != SYS_WIIU) {
        back = rgb(0x121418); mid = rgb(0x2B2E35); front = rgb(0x16181C);
        name = rgb(0xF1F2F4);
        if (sys == SYS_GC) {
            b1 = rgb(0x5546B0);
            b2 = rgb(0x2E2766);
            band = b->h * 0.11f;
        } else {
            b1 = rgb(0x3B3E46);
            b2 = rgb(0x24262B);
            band = b->h * 0.16f;
        }
    }
    float p1[4][3] = { { x, y0 + band, zb }, { x, y0 + band, zm }, { x, y1, zm }, { x, y1, zb } };
    Color c1[4] = { back, mid, mid, back };
    quad3(b, p1, c1);
    float p2[4][3] = { { x, y0 + band, zm }, { x, y0 + band, zf }, { x, y1, zf }, { x, y1, zm } };
    Color c2[4] = { mid, front, front, mid };
    quad3(b, p2, c2);
    float p3[4][3] = { { x, y0, zb }, { x, y0, zf }, { x, y0 + band, zf }, { x, y0 + band, zb } };
    Color c3[4] = { b1, b1, b2, b2 };
    quad3(b, p3, c3);
    if (b->d < 14)
        return;

    gfx_push_alpha(b->alpha);
    const char *label = game_system_short(g);
    int ls = (int)clampf(b->d * 0.3f, 8, 12);
    float lw = text_width(ls, label);
    SpineText st = { b, y0 + band * 0.5f - ls * 0.62f, -lw * 0.5f, true };
    text_set_transform(spine_xform, &st);
    text_draw(ls, label, 0, 0, label_c, ALIGN_LEFT);

    int ns = (int)clampf(b->d * 0.42f, 9, 14);
    char fit[256];
    float pad = 14;
    text_fit(ns, g->name1, b->h - band - pad * 2, fit, sizeof(fit));
    SpineText sn2 = { b, y0 + band + pad, ns * 0.62f, false };
    text_set_transform(spine_xform, &sn2);
    text_draw(ns, fit, 0, 0, name, ALIGN_LEFT);
    text_set_transform(NULL, NULL);
    gfx_pop_alpha();
}

static void draw_case(Game *g, float alpha, float dx, float yaw_deg, bool hits)
{
    if (!g || alpha <= 0.01f)
        return;
    // the big cover, or its thumbnail while that one loads (same shape: no jump)
    SDL_Texture *front = g->cover ? g->cover : g->thumb;
    if (!front) {
        // no cover at all: the generated Wii U box, flat
        float w = CASE_H * 0.711f, h = CASE_H;
        float x = CASE_CX - w * 0.5f + dx, y = CASE_CY - h * 0.5f;
        draw_cover(g, x, y, w, h, alpha);
        if (hits)
            hit_add(x, y, w, h, HIT_PLAY, 0);
        return;
    }
    CaseDim cd = case_dims(g);
    float yaw = yaw_deg * (float)M_PI / 180.0f;
    Box3 b = { cd.w, cd.h, cd.d, cosf(yaw), sinf(yaw), dx, alpha };

    // corners of the front, for the floor shadow and the touch area
    float fx[4], fy[4];
    project(&b, -cd.w * 0.5f, -cd.h * 0.5f, cd.d * 0.5f, &fx[0], &fy[0]);
    project(&b, cd.w * 0.5f, -cd.h * 0.5f, cd.d * 0.5f, &fx[1], &fy[1]);
    project(&b, cd.w * 0.5f, cd.h * 0.5f, cd.d * 0.5f, &fx[2], &fy[2]);
    project(&b, -cd.w * 0.5f, cd.h * 0.5f, cd.d * 0.5f, &fx[3], &fy[3]);
    float bx, by;
    project(&b, -cd.w * 0.5f, cd.h * 0.5f, -cd.d * 0.5f, &bx, &by);
    float sx0 = fminf(bx, fx[3]) - 22, sx1 = fx[2] + 22, sy = fmaxf(fy[2], fy[3]);
    gfx_push_alpha(alpha);
    gfx_shadow(sx0, sy - 16, sx1 - sx0, 32, 16, 30, rgba(0x000000, 0.7f));
    gfx_pop_alpha();

    draw_spine(&b, g, cd.sys);
    draw_front(&b, front);

    if (g->favorite) {
        float px, py;
        project(&b, cd.w * 0.5f - 34, cd.h * 0.5f - 34, cd.d * 0.5f, &px, &py);
        gfx_push_alpha(alpha);
        gfx_shadow(px - 20, py - 18, 40, 40, 20, 10, rgba(0x000000, 0.45f));
        gfx_circle(px, py, 20, rgb(0xF5B400));
        icon_draw(IC_STAR, px - 12, py - 12, 24, rgb(0x2A1D00));
        gfx_pop_alpha();
    }
    if (hits) {
        float x0 = fminf(fx[0], fx[3]), x1 = fmaxf(fx[1], fx[2]);
        float y0 = fminf(fy[0], fy[1]), y1 = fmaxf(fy[2], fy[3]);
        hit_add(x0, y0, x1 - x0, y1 - y0, HIT_PLAY, 0);
    }
}

// ------------------------------------------------------------ background

static int s_kb_game = -2;
static float s_kb_start, s_zoom_cur = 1.02f, s_zoom_prev = 1.02f;

// Full-screen image, zoomed by `zoom` towards (fx, fy) in 0..1. Drawn as one
// screen-sized quad with cropped texture coordinates: no geometry off screen.
static void screen_image(SDL_Texture *t, float zoom, float fx, float fy, float alpha)
{
    int tw = 1, th = 1;
    SDL_QueryTexture(t, NULL, NULL, &tw, &th);
    float ta = (float)tw / th, sa = (float)SCREEN_W / SCREEN_H;
    float vw = ta > sa ? sa / ta : 1, vh = ta > sa ? 1 : ta / sa; // "cover" fit
    vw /= zoom;
    vh /= zoom;
    float u0 = (1 - vw) * fx, v0 = (1 - vh) * fy;
    SDL_Color c = vcolor(rgb(0xFFFFFF), alpha);
    SDL_Vertex v[4] = {
        { { 0, 0 }, c, { u0, v0 } },
        { { SCREEN_W, 0 }, c, { u0 + vw, v0 } },
        { { SCREEN_W, SCREEN_H }, c, { u0 + vw, v0 + vh } },
        { { 0, SCREEN_H }, c, { u0, v0 + vh } },
    };
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    gfx_geometry(t, v, 4, idx, 6);
}

static void art_layer(Game *g, float alpha, float zoom)
{
    if (!g || alpha <= 0.003f)
        return;
    SDL_Texture *t = g->fondo ? g->fondo : g->boot ? g->boot : g->boot_blur;
    if (!t || !L.set.bg_art) {
        Color c = g->tint;
        gfx_rect_vgrad(0, 0, SCREEN_W, SCREEN_H, col_alpha(darken(c, 0.45f), alpha),
                       col_alpha(darken(c, 0.88f), alpha));
        return;
    }
    screen_image(t, zoom, 0.62f, 0.4f, alpha);
}

void cine_background(void)
{
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, k_bg);
    Game *cur = ui.bg_game >= 0 && ui.bg_game < L.count ? &L.games[ui.bg_game] : NULL;
    Game *prev = ui.bg_prev >= 0 && ui.bg_prev < L.count ? &L.games[ui.bg_prev] : NULL;
    if (ui.bg_game != s_kb_game) {
        s_kb_game = ui.bg_game;
        s_kb_start = ui.time;
        s_zoom_prev = s_zoom_cur;
    }
    // slow "Ken Burns" zoom-out while the game stays selected
    float kb = ease_out_cubic(clampf((ui.time - s_kb_start) / 10.0f, 0, 1));
    s_zoom_cur = 1.10f - 0.08f * kb;
    float t = ease_in_out(ui.bg_t);
    if (prev && t < 1)
        art_layer(prev, 1, s_zoom_prev);
    if (cur)
        art_layer(cur, t, s_zoom_cur);

    // darken where the text goes, keep the art bright behind the case
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, bgc(0.18f));
    gfx_rect_hgrad(0, 0, 460, SCREEN_H, bgc(0.0f), bgc(0.1f));
    gfx_rect_hgrad(460, 0, 180, SCREEN_H, bgc(0.1f), bgc(0.66f));
    gfx_rect_hgrad(640, 0, SCREEN_W - 640, SCREEN_H, bgc(0.66f), bgc(0.84f));
    gfx_rect_vgrad(0, 0, SCREEN_W, 130, bgc(0.8f), bgc(0.0f));
    gfx_rect_vgrad(0, SCREEN_H - 240, SCREEN_W, 240, bgc(0.0f), bgc(0.84f));
}

// ------------------------------------------------------------ fact sheet

static void kicker_for(Game *g, char *out, size_t n, Color *col)
{
    Category *c = NULL;
    const Tab *t = &ui.tabs[ui.tab];
    if (t->kind == TAB_CAT && ((g->catmask >> t->cat) & 1))
        c = cat_by_id(t->cat);
    for (int i = 0; !c && i < L.ncats; i++)
        if ((g->catmask >> L.cats[i].id) & 1)
            c = &L.cats[i];
    char label[96];
    if (c) {
        str_copy(label, sizeof(label), c->name);
    } else if (g->genres[0]) {
        str_copy(label, sizeof(label), g->genres);
        char *comma = strchr(label, ',');
        if (comma)
            *comma = 0;
    } else if (game_system(g) != SYS_WIIU) {
        str_copy(label, sizeof(label), game_system_name(g));
    } else {
        str_copy(label, sizeof(label), tr("Juego"));
    }
    // a category or genre name may be one of the Spanish names (default categories, genres)
    if (g->year[0])
        snprintf(out, n, "%s  ·  %s", tr(label), g->year);
    else
        str_copy(out, n, tr(label));
    *col = c ? lighten(rgb(c->color), 0.22f) : rgb(0x7FD3F7);
}

static void stat_cell(float *x, float y, const char *caption, const char *value)
{
    text_draw(14, caption, *x, y, white(0.6f), ALIGN_LEFT);
    text_draw(22, value, *x, y + 20, rgb(0xFFFFFF), ALIGN_LEFT);
    *x += fmaxf(text_width(14, caption), text_width(22, value)) + 36;
}

static void rating_cell(float *x, float y, int rating10)
{
    text_draw(14, "VALORACIÓN", *x, y, white(0.6f), ALIGN_LEFT);
    const float S = 19, STEP = 21;
    float sy = y + 24;
    for (int i = 0; i < 5; i++)
        icon_draw(IC_STAR, *x + i * STEP, sy, S, white(0.22f));
    float r = rating10 / 10.0f;
    int full = (int)r;
    float filled = full * STEP + (r - full) * S;
    gfx_clip((int)*x, (int)sy - 2, (int)(filled + 0.5f), (int)S + 4);
    for (int i = 0; i < 5; i++)
        icon_draw(IC_STAR, *x + i * STEP, sy, S, rgb(0xF5B400));
    gfx_unclip();
    char v[16];
    snprintf(v, sizeof(v), i18n_english() ? "%d.%d" : "%d,%d", rating10 / 10, rating10 % 10);
    float vx = *x + 5 * STEP + 8;
    float w = text_draw(22, v, vx, y + 20, rgb(0xFFFFFF), ALIGN_LEFT);
    *x = vx + w + 36;
}

static void draw_title(Game *g, float x, float y, float bw, float bh)
{
    if (g->title_logo) {
        int tw = 1, th = 1;
        SDL_QueryTexture(g->title_logo, NULL, NULL, &tw, &th);
        float s = fminf(fminf(bw / tw, bh / th), 1.25f);
        float w = tw * s, h = th * s, ly = y + bh - h;
        if (g->title_glow) {
            // dark logo on the dark screen: soft light halo (its silhouette, blurred,
            // made at half size with a 10 px margin by the loader)
            int gw = 1, gh = 1;
            SDL_QueryTexture(g->title_glow, NULL, NULL, &gw, &gh);
            float gs = s * 2;
            gfx_image(g->title_glow, NULL, x - 10 * gs, ly - 10 * gs, gw * gs, gh * gs, 0, rgba(0xFFFFFF, 0.75f));
        } else {
            // soft shadow: the logo's own silhouette, offset
            gfx_image(g->title_logo, NULL, x, ly + 6, w, h, 0, rgba(0x000000, 0.25f));
            gfx_image(g->title_logo, NULL, x, ly + 3, w, h, 0, rgba(0x000000, 0.35f));
        }
        gfx_image(g->title_logo, NULL, x, ly, w, h, 0, rgb(0xFFFFFF));
        return;
    }
    char lines[2][256];
    int ts = 44;
    int n = text_wrap(ts, g->name1[0] ? g->name1 : "...", bw + 120, 2, lines);
    if (n > 1) {
        ts = 36;
        n = text_wrap(ts, g->name1, bw + 120, 2, lines);
    }
    float lh = ts * 1.16f, ty = y + bh - n * lh;
    for (int i = 0; i < n; i++)
        text_draw_shadow(ts, lines[i], x, ty + i * lh, rgb(0xFFFFFF), rgba(0x000000, 0.55f), ALIGN_LEFT);
}

static void draw_info(Game *g, float alpha, float dx)
{
    if (!g || alpha <= 0.01f)
        return;
    gfx_push_alpha(alpha);
    float x = INFO_X + dx, y = 94;
    char buf[256], fit[256];

    Color kc;
    kicker_for(g, buf, sizeof(buf), &kc);
    upper_es(buf, fit, sizeof(fit));
    gfx_circle(x + 5, y + 9, 5, kc);
    text_draw(15, fit, x + 18, y, kc, ALIGN_LEFT);
    y += 26;

    draw_title(g, x, y, 470, 116);
    y += 116 + 14;

    const char *pub = g->lb_pub[0] ? g->lb_pub : g->publisher;
    if (g->lb_dev[0] && strcmp(g->lb_dev, pub))
        snprintf(buf, sizeof(buf), "%s  ·  %s", pub, g->lb_dev);
    else
        str_copy(buf, sizeof(buf), pub);
    text_fit(18, buf, INFO_W - 20, fit, sizeof(fit));
    text_draw(18, fit, x, y, white(0.8f), ALIGN_LEFT);
    y += 32;

    // genres and system (a hidden game, listed because "show hidden games" is on, says so first)
    float cx = x, cw, cmax = x + INFO_W - 20;
    if (g->hidden) {
        draw_chip(cx, y, "Oculto", IC_EYE_OFF, rgba(0x6C747D, 0.9f), rgb(0xFFFFFF), &cw);
        cx += cw + 8;
    }
    char gen[96];
    str_copy(gen, sizeof(gen), g->genres);
    char *save = NULL;
    for (char *tk = strtok_r(gen, ",", &save); tk; tk = strtok_r(NULL, ",", &save)) {
        tk = str_trim(tk);
        if (!*tk || cx + chip_width(tk, -1) > cmax)
            continue;
        draw_chip(cx, y, tk, -1, white(0.14f), rgb(0xFFFFFF), &cw);
        cx += cw + 8;
    }
    int sys = game_system(g);
    if (sys != SYS_WIIU) {
        // Wii and GameCube games run in Wii mode; the rest are Virtual Console
        if (sys == SYS_WII || sys == SYS_GC)
            snprintf(buf, sizeof(buf), tr("Juego de %s · modo Wii"), game_system_name(g));
        else if (sys == SYS_HOMEBREW)
            str_copy(buf, sizeof(buf), "Homebrew de Aroma (SD)");
        else if (sys == SYS_OTHER && !g->lb_platform[0])
            str_copy(buf, sizeof(buf), "Consola Virtual"); // console unknown: just once
        else
            snprintf(buf, sizeof(buf), tr("Consola Virtual · %s"), game_system_name(g));
        Color cc = sys == SYS_WII ? rgba(0x8A939D, 0.6f) : sys == SYS_GC ? rgba(0x5546B0, 0.7f)
                                                                        : rgba(0x0A7CC0, 0.55f);
        if (cx + chip_width(buf, IC_GAMEPAD) <= cmax) {
            draw_chip(cx, y, buf, IC_GAMEPAD, cc, rgb(0xFFFFFF), &cw);
            cx += cw + 8;
        }
    }
    if (g->has_dlc && cx + chip_width("DLC", IC_PLUS) <= cmax)
        draw_chip(cx, y, "DLC", IC_PLUS, white(0.14f), rgb(0xFFFFFF), &cw);
    y += 32 + 18;

    // year, players, rating, product code
    float sx = x;
    stat_cell(&sx, y, "AÑO", g->year[0] ? g->year : "—");
    if (g->players > 1)
        snprintf(buf, sizeof(buf), "1–%u", (unsigned)g->players);
    else
        str_copy(buf, sizeof(buf), g->players == 1 ? "1" : "—");
    stat_cell(&sx, y, "JUGADORES", buf);
    if (g->rating10)
        rating_cell(&sx, y, g->rating10);
    if (g->product[0])
        stat_cell(&sx, y, "CÓDIGO", g->product);
    y += 52 + 14;

    if (g->synopsis[0]) {
        char lines[3][256];
        int n = text_wrap(17, g->synopsis, 580, 3, lines);
        for (int i = 0; i < n; i++)
            text_draw(17, lines[i], x, y + i * 25, white(0.86f), ALIGN_LEFT);
    }
    y = 476;

    draw_button(x, y, 160, 52, BTN_A, "Jugar", true, 0, BTN_A);
    draw_button(x + 172, y, 176, 52, BTN_X, "Opciones", false, 0, BTN_X);
    draw_button(x + 360, y, 214, 52, BTN_Y, g->favorite ? "Quitar favorito" : "Favorito", false, 0, BTN_Y);
    // hide (or show again) right here; also in Opciones (X)
    draw_round_button(x + 586, y, 52, g->hidden ? IC_EYE : IC_EYE_OFF, HIT_HIDE, (int)(g - L.games));
    y += 52 + 16;

    int64_t t = game_last_time(g);
    if (t > 0) {
        char when[64];
        format_relative_time(t, when, sizeof(when));
        if (g->play_count > 0)
            snprintf(buf, sizeof(buf),
                     tr(g->play_count == 1 ? "Última vez: %s  ·  1 vez desde Pixmo Games"
                                           : "Última vez: %s  ·  %u veces desde Pixmo Games"),
                     when, g->play_count);
        else
            snprintf(buf, sizeof(buf), tr("Última vez: %s"), when);
    } else {
        str_copy(buf, sizeof(buf), "Todavía no lo juegas desde Pixmo Games");
    }
    icon_draw(IC_CLOCK, x, y + 1, 18, white(0.6f));
    text_draw(16, buf, x + 26, y, white(0.68f), ALIGN_LEFT);
    gfx_pop_alpha();
}

// ------------------------------------------------------------ cover rail

static float *s_slot;
static int s_slot_cap;
static float s_rail_off;
static bool s_rail_init;
static float s_rail_fade;  // 0..1: the rail fades in once, with its final layout
static uint64_t s_rail_tid; // game selected and rail target of the last frame (to tell
static float s_rail_target; // a new selection from covers changing shape or order)

static void rail_cover(Game *g, float x, float y, float w, float h, float alpha)
{
    gfx_push_alpha(alpha * (g->hidden ? 0.5f : 1.0f)); // hidden games (shown on request) look faded
    gfx_shadow(x, y + 4, w, h, 5, 14, rgba(0x000000, 0.55f));
    // the thumbnail first: it is close to this size (the big cover would shimmer
    // when shrunk this much) and it does not change when covers load or leave
    SDL_Texture *t = g->thumb ? g->thumb : g->cover;
    if (t) {
        gfx_image(t, NULL, x, y, w, h, 5, rgb(0xFFFFFF));
    } else {
        // small generated box: colour of the game and its icon
        Color tc = g->tint;
        gfx_rrect_vgrad(x, y, w, h, 5, lighten(tc, 0.25f), darken(tc, 0.35f));
        float band = h * 0.095f;
        int sys = game_system(g);
        uint32_t c1 = sys == SYS_WIIU ? 0x1AA6E0 : sys == SYS_WII ? 0xE6E9ED : sys == SYS_GC ? 0x5546B0 : 0x3B3E46;
        uint32_t c2 = sys == SYS_WIIU ? 0x0077B8 : sys == SYS_WII ? 0xB9BFC6 : sys == SYS_GC ? 0x2E2766 : 0x24262B;
        gfx_clip((int)x, (int)y, (int)w + 1, (int)(band + 0.5f));
        gfx_rrect_vgrad(x, y, w, h, 5, rgb(c1), rgb(c2));
        gfx_unclip();
        if (g->icon) {
            float is = w * 0.72f;
            gfx_image(g->icon, NULL, x + (w - is) * 0.5f, y + (h - is) * 0.55f, is, is, is * 0.14f, rgb(0xFFFFFF));
        }
    }
    gfx_rrect_vgrad(x, y, w, h * 0.45f, 5, white(0.14f), white(0.0f));
    gfx_rrect_outline(x, y, w, h, 5, 1, rgba(0x000000, 0.25f));
    if (g->favorite) {
        float bs = fmaxf(16, w * 0.3f);
        float bx = x + w - bs * 0.75f, by = y + h - bs * 0.75f;
        gfx_circle(bx + bs * 0.5f, by + bs * 0.5f, bs * 0.5f, rgb(0xF5B400));
        icon_draw(IC_STAR, bx + bs * 0.18f, by + bs * 0.16f, bs * 0.64f, rgb(0x2A1D00));
    }
    if (g->hidden) {
        float bs = fmaxf(16, w * 0.3f);
        float bx = x + w - bs * 0.75f, by = y - bs * 0.25f;
        gfx_circle(bx + bs * 0.5f, by + bs * 0.5f, bs * 0.5f, rgb(0x6C747D));
        icon_draw(IC_EYE_OFF, bx + bs * 0.18f, by + bs * 0.18f, bs * 0.64f, rgb(0xFFFFFF));
    }
    gfx_pop_alpha();
}

static void round_arrow(float cx, float cy, int dir, bool enabled)
{
    const float r = 22;
    gfx_circle(cx, cy, r, white(enabled ? 0.12f : 0.05f));
    gfx_ring(cx, cy, r, 1.5f, white(enabled ? 0.35f : 0.15f));
    Color c = white(enabled ? 1.0f : 0.35f);
    float s = (float)dir;
    gfx_line(cx - 3 * s, cy - 7, cx + 4 * s, cy, 2.6f, c);
    gfx_line(cx + 4 * s, cy, cx - 3 * s, cy + 7, 2.6f, c);
    hit_add(cx - r, cy - r, r * 2, r * 2, HIT_BUTTON, dir < 0 ? BTN_LEFT : BTN_RIGHT);
}

static void draw_rail(void)
{
    if (ui.nview <= 0)
        return;
    // The rail waits (hidden) until every saved thumbnail is a texture: before that,
    // covers have a default width and the rail would reshape and slide while they
    // arrive. Then it appears once, already in place, and fades in.
    // The first time it also waits for the game list to settle (names are sorted
    // while the library loads), at most 3.5 s after start.
    static bool shown;
    bool settled = SDL_GetTicks() - g_view_built_at > 300 || SDL_GetTicks() > 3500;
    if (!lib_thumbs_ready() || (!shown && !settled)) {
        s_rail_init = false;
        s_rail_fade = 0;
        return;
    }
    shown = true;
    s_rail_fade = fminf(1.0f, s_rail_fade + 1.0f / 12.0f);
    if (s_slot_cap < ui.nview) {
        int cap = ui.nview + 32;
        float *ns = realloc(s_slot, sizeof(float) * cap * 2);
        if (!ns)
            return;
        s_slot = ns;
        s_slot_cap = cap;
    }
    float *sx = s_slot, *sw = s_slot + s_slot_cap;
    float acc = 0;
    for (int i = 0; i < ui.nview; i++) {
        // whole pixels: a cover that loads with a slightly different shape does
        // not push every cover after it by a fraction of a pixel
        sw[i] = roundf(RAIL_H * cover_aspect(&L.games[ui.view[i]]));
        sx[i] = acc;
        acc += sw[i] + RAIL_GAP;
    }
    float total = acc - RAIL_GAP, railw = RAIL_X1 - RAIL_X0;
    float target = sx[ui.sel] + sw[ui.sel] * 0.5f - railw * 0.5f;
    if (target > total - railw)
        target = total - railw;
    if (target < 0)
        target = 0;
    uint64_t sel_tid = L.games[ui.view[ui.sel]].tid;
    if (!s_rail_init || !ui.user_moved) {
        // until the user moves, the rail sits on the game: the list can still be
        // sorted or reshaped while the library loads, and that must not slide it
        s_rail_off = target;
        s_rail_init = true;
    } else if (sel_tid == s_rail_tid && target != s_rail_target) {
        // same game selected, but covers before it changed shape or order (a cover
        // arrived, names were sorted): keep it still instead of sliding there
        s_rail_off += target - s_rail_target;
    }
    s_rail_tid = sel_tid;
    s_rail_target = target;
    s_rail_off = approach(s_rail_off, target, 14.0f, 1.0f / 60);
    if (fabsf(s_rail_off - target) < 0.5f)
        s_rail_off = target; // settled: no endless sub-pixel creep
    float off = s_rail_off;
    if (ui.dragging && ui.ty0 > 560 && ui.tx0 > INFO_X && npanels == 0)
        off -= ui.tx - ui.tx0; // follow the finger
    off = roundf(off); // covers on whole pixels: no shimmer while the rail moves or rests

    gfx_push_alpha(s_rail_fade);
    const float cy = RAIL_BOTTOM - RAIL_H * 0.5f;
    round_arrow(INFO_X + 22, cy, -1, ui.sel > 0);
    round_arrow(RAIL_X1 + 32, cy, 1, ui.sel < ui.nview - 1);
    char pos[32];
    snprintf(pos, sizeof(pos), "%d / %d", ui.sel + 1, ui.nview);
    text_draw(16, pos, SCREEN_W - 12, cy - 16 * 0.62f, white(0.75f), ALIGN_RIGHT);

    gfx_clip((int)RAIL_X0 - 18, 556, (int)railw + 28, 150); // room for the selection glow
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < ui.nview; i++) {
            float x = RAIL_X0 + sx[i] - off;
            if (x + sw[i] * 1.3f < RAIL_X0 - 10 || x - sw[i] * 0.3f > RAIL_X1 + 10)
                continue;
            Game *gi = &L.games[ui.view[i]];
            float k = gi->anim_sel;
            if ((pass == 0) == (k > 0.5f))
                continue;
            float h = roundf(RAIL_H + (RAIL_HSEL - RAIL_H) * k), w = roundf(h * (sw[i] / RAIL_H));
            float mid = x + sw[i] * 0.5f, bx = roundf(mid - w * 0.5f), by = RAIL_BOTTOM - h;
            float fade = clampf((mid - RAIL_X0 + 12) / 50.0f, 0, 1) * clampf((RAIL_X1 + 12 - mid) / 50.0f, 0, 1);
            if (ui.launching && i == ui.sel)
                fade *= clampf(1 - ui.launch_t * 3, 0, 1);
            rail_cover(gi, bx, by, w, h, fade);
            if (k > 0.01f)
                draw_sel_frame(bx, by, w, h, 5, k * fade);
            hit_add(bx, by, w, h, HIT_GAME, i);
        }
    }
    gfx_unclip();
    gfx_pop_alpha();
}

// ------------------------------------------------------------ top bar & hints

static void split_now(struct tm *tm)
{
    time_t t = (time_t)time_now();
#ifdef __WIIU__
    gmtime_r(&t, tm);
#else
    localtime_r(&t, tm);
#endif
}

static Color tab_color(const Tab *t)
{
    return lighten(rgb(t->color), 0.2f);
}

void cine_top_bar(void)
{
    // Pixmo Games: the mascot and its name in pixel letters (PIXMO over GAMES)
    brand_draw_icon(26, 13, 46, 1);
    brand_draw_name(82, 12, 3, true, rgb(0xFFFFFF));

    const float tx0 = 200, tx1 = L.set.show_clock ? 1110 : 1250, ty = 12, th = 46;
    float tab_x[64], tab_w[64], x = 0;
    for (int i = 0; i < ui.ntabs; i++) {
        tab_w[i] = 12 + 22 + 8 + text_width(19, ui.tabs[i].name) + 12;
        tab_x[i] = x;
        x += tab_w[i] + 2;
    }
    float total = x - 2;
    if (ui.ntabs > 0) {
        float target = ui.tabs_scroll, sx = tab_x[ui.tab], sw = tab_w[ui.tab];
        if (sx - target < 0)
            target = sx - 40;
        if (sx + sw - target > tx1 - tx0)
            target = sx + sw - (tx1 - tx0) + 40;
        if (target > total - (tx1 - tx0))
            target = total - (tx1 - tx0);
        if (target < 0)
            target = 0;
        ui.tabs_scroll = approach(ui.tabs_scroll, target, 14, 1.0f / 60);
        if (!ui.tab_ind_init) {
            ui.tab_ind_x = sx;
            ui.tab_ind_w = sw;
            ui.tab_ind_init = true;
        }
        ui.tab_ind_x = approach(ui.tab_ind_x, sx, 18, 1.0f / 60);
        ui.tab_ind_w = approach(ui.tab_ind_w, sw, 18, 1.0f / 60);
    }
    gfx_clip((int)tx0 - 8, 0, (int)(tx1 - tx0) + 16, 72);
    float off = tx0 - ui.tabs_scroll;
    if (ui.ntabs > 0) {
        float ix = off + ui.tab_ind_x;
        gfx_rrect(ix, ty, ui.tab_ind_w, th, 12, white(0.1f));
        gfx_rrect(ix + 12, ty + th - 4, ui.tab_ind_w - 24, 4, 2, tab_color(&ui.tabs[ui.tab]));
    }
    for (int i = 0; i < ui.ntabs; i++) {
        float bx = off + tab_x[i];
        if (bx + tab_w[i] < tx0 - 8 || bx > tx1 + 8)
            continue;
        const Tab *t = &ui.tabs[i];
        bool cur = i == ui.tab;
        float ix = bx + 12, iy = ty + (th - 22) * 0.5f - 1;
        Category *c = t->kind == TAB_CAT ? cat_by_id(t->cat) : NULL;
        if (c && c->logo_tex)
            gfx_image_contain(c->logo_tex, ix, iy, 22, 22, 4, rgb(0xFFFFFF));
        else
            icon_draw(t->icon, ix, iy, 22, tab_color(t));
        text_draw(19, t->name, ix + 30, ty + th * 0.5f - 19 * 0.62f - 1, cur ? rgb(0xFFFFFF) : white(0.7f),
                  ALIGN_LEFT);
        hit_add(bx, ty, tab_w[i], th, HIT_TAB, i);
    }
    if (ui.focus == FOCUS_TABS && ui.ntabs > 0)
        draw_sel_frame(off + ui.tab_ind_x, ty, ui.tab_ind_w, th, 12, 1);
    gfx_unclip();

    if (L.set.show_clock) {
        struct tm tm;
        split_now(&tm);
        char hm[16], date[32];
        snprintf(hm, sizeof(hm), "%d:%02d", tm.tm_hour, tm.tm_min);
        format_clock_date(&tm, date, sizeof(date));
        text_draw(28, hm, SCREEN_W - 28, 10, rgb(0xFFFFFF), ALIGN_RIGHT);
        text_draw(15, date, SCREEN_W - 28, 46, white(0.7f), ALIGN_RIGHT);
    }
    // only real work is announced: a cover coming from the internet, or new games
    if (lib_downloads_active() > 0 || L.meta_pending > 0) {
        char st[48];
        if (lib_downloads_active() > 0)
            str_copy(st, sizeof(st), "Descargando carátulas...");
        else
            str_copy(st, sizeof(st), "Leyendo juegos...");
        float tw = text_width(15, st) + 46, px = SCREEN_W - 20 - tw, py = 78;
        gfx_rrect(px, py, tw, 28, 14, rgba(0x14181D, 0.85f));
        draw_spinner(px + 16, py + 14, 6, rgb(0x33C3F0));
        text_draw(15, st, px + 30, py + 14 - 15 * 0.62f, white(0.8f), ALIGN_LEFT);
    }
}

static void hint(float *x, float cy, Button a, Button b, const char *label)
{
    float x0 = *x;
    *x += draw_glyph(a, *x, cy, 24) + 4;
    hit_add(x0 - 4, cy - 20, *x - x0 + 4, 40, HIT_BUTTON, a);
    Button tap = a;
    if (b != a) {
        float x1 = *x;
        *x += draw_glyph(b, *x, cy, 24) + 4;
        hit_add(x1, cy - 20, *x - x1, 40, HIT_BUTTON, b);
        tap = b;
    }
    float lx = *x + 3;
    *x = lx + text_draw(17, label, lx, cy - 17 * 0.62f, white(0.8f), ALIGN_LEFT);
    hit_add(lx, cy - 20, *x - lx + 4, 40, HIT_BUTTON, tap);
    *x += 24;
}

static void cine_hints(void)
{
    float x = 30, cy = 700;
    hint(&x, cy, BTN_L, BTN_R, "Categoría");
    hint(&x, cy, BTN_MINUS, BTN_MINUS, "Buscar");
    hint(&x, cy, BTN_PLUS, BTN_PLUS, "Ajustes");
}

static void cine_empty(void)
{
    const char *title, *sub;
    int icon;
    home_empty_texts(&title, &sub, &icon);
    const Tab *t = &ui.tabs[ui.tab];
    float cy = 330;
    if (icon == t->icon)
        draw_tab_badge(t, SCREEN_W * 0.5f - 48, cy - 130, 96);
    else
        draw_badge(icon, 0x8C96A0, NULL, SCREEN_W * 0.5f - 48, cy - 130, 96);
    text_draw(28, title, SCREEN_W * 0.5f, cy, rgb(0xFFFFFF), ALIGN_CENTER);
    text_draw(19, sub, SCREEN_W * 0.5f, cy + 44, white(0.72f), ALIGN_CENTER);
}

void cine_render(void)
{
    if (ui.search[0]) {
        char q[96], fit[96];
        snprintf(q, sizeof(q), tr("«%s»"), ui.search);
        text_fit(17, q, 260, fit, sizeof(fit));
        float w = chip_width(fit, IC_SEARCH), cw;
        float cx = SCREEN_W - 40 - w;
        draw_chip(cx, 84, fit, IC_SEARCH, T.accent, T.accent_text, &cw);
        hit_add(cx, 84, cw, 32, HIT_SEARCH_CLEAR, 0);
    }
    if (ui.nview == 0) {
        cine_empty();
        cine_hints();
        return;
    }
    Game *g = ui.hero_game >= 0 ? &L.games[ui.hero_game] : NULL;
    Game *pg = ui.hero_prev >= 0 ? &L.games[ui.hero_prev] : NULL;
    float t = ease_out_cubic(ui.hero_t);
    float dir = (float)(ui.hero_dir ? ui.hero_dir : 1);
    float shown = ui.launching ? 0 : 1;
    if (pg && t < 1)
        draw_case(pg, (1 - t) * shown, -dir * 70 * t, CASE_YAW - dir * 6 * t, false);
    if (g) {
        draw_case(g, t * shown, dir * 70 * (1 - t), CASE_YAW + dir * 6 * (1 - t), true);
        draw_info(g, t, dir * 24 * (1 - t));
    }
    draw_rail();
    cine_hints();
}

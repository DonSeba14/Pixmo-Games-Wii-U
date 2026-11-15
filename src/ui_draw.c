// Elementos visuales de la interfaz con la estética del menú de Wii U:
// fondo claro con rayas finas, barras grises, azul Wii U y botones redondos.
#include "ui_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

Theme T;

static Theme s_light, s_dark;
static SDL_Texture *s_stripes;

static void themes_init(void)
{
    Theme *l = &s_light;
    l->bg_top = rgb(0xF8F9FB);
    l->bg_bottom = rgb(0xDCE1E7);
    l->stripe = rgba(0x8A96A3, 0.07f);
    l->bar_top = rgb(0xFFFFFF);
    l->bar_bottom = rgb(0xEDF0F3);
    l->bar_line = rgb(0xCBD2D9);
    l->panel = rgb(0xFFFFFF);
    l->panel_head = rgb(0xF1F3F6);
    l->row_line = rgb(0xE4E8EC);
    l->text = rgb(0x323539);
    l->text_dim = rgb(0x6C747D);
    l->text_faint = rgb(0xA2A9B1);
    l->accent = rgb(0x009AC7);
    l->accent_dark = rgb(0x007DAF);
    l->accent_light = rgb(0x33C3F0);
    l->accent_text = rgb(0xFFFFFF);
    l->sel_fill = rgb(0xE2F4FB);
    l->sel_line = rgb(0x1CB5EA);
    l->shadow = rgba(0x1B2E44, 0.30f);
    l->dim = rgba(0x101820, 0.38f);
    l->glyph_bg = rgb(0x5B636C);
    l->glyph_fg = rgb(0xFFFFFF);
    l->tile_bg = rgb(0xFFFFFF);
    l->chip_bg = rgb(0xECEFF3);
    l->art_alpha = 0.62f;
    l->veil = 0.70f;

    Theme *d = &s_dark;
    d->bg_top = rgb(0x262C33);
    d->bg_bottom = rgb(0x14171B);
    d->stripe = rgba(0xFFFFFF, 0.025f);
    d->bar_top = rgb(0x323941);
    d->bar_bottom = rgb(0x2A3037);
    d->bar_line = rgb(0x434C56);
    d->panel = rgb(0x2D333A);
    d->panel_head = rgb(0x262B31);
    d->row_line = rgb(0x3B434C);
    d->text = rgb(0xEEF1F4);
    d->text_dim = rgb(0xA8B0B9);
    d->text_faint = rgb(0x6F7882);
    d->accent = rgb(0x0FA7DD);
    d->accent_dark = rgb(0x0886B5);
    d->accent_light = rgb(0x4CCAF4);
    d->accent_text = rgb(0xFFFFFF);
    d->sel_fill = rgb(0x1C3A4B);
    d->sel_line = rgb(0x31C1F1);
    d->shadow = rgba(0x000000, 0.55f);
    d->dim = rgba(0x000000, 0.5f);
    d->glyph_bg = rgb(0xDADFE4);
    d->glyph_fg = rgb(0x23282E);
    d->tile_bg = rgb(0x3B424A);
    d->chip_bg = rgb(0x39414A);
    d->art_alpha = 0.55f;
    d->veil = 0.72f;
}

static void make_stripes(void)
{
    const int N = 256;
    uint8_t *px = calloc((size_t)N * N, 4);
    if (!px)
        return;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            // 45° lines every 8 px, 1.2 px wide, antialiased
            float d = fmodf((float)(x + y) + 0.5f, 8.0f);
            float dist = fminf(d, 8.0f - d) / 1.41421f;
            float a = clampf(1.0f - (dist - 0.4f) / 0.8f, 0, 1);
            uint8_t *p = px + ((size_t)y * N + x) * 4;
            p[0] = p[1] = p[2] = 255;
            p[3] = (uint8_t)(a * 255);
        }
    s_stripes = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, N, N);
    if (s_stripes) {
        SDL_UpdateTexture(s_stripes, NULL, px, N * 4);
        SDL_SetTextureBlendMode(s_stripes, SDL_BLENDMODE_BLEND);
    }
    free(px);
}

static Color mixc(Color a, Color b, float t)
{
    return col_mix(a, b, t);
}

void theme_update(float dt)
{
    static bool init;
    if (!init) {
        themes_init();
        make_stripes();
        init = true;
        ui.theme_t = (float)L.set.theme;
    }
    ui.theme_t = approach(ui.theme_t, (float)L.set.theme, 8.0f, dt);
    if (fabsf(ui.theme_t - L.set.theme) < 0.002f)
        ui.theme_t = (float)L.set.theme;
    float t = ui.theme_t;
    const Theme *a = &s_light, *b = &s_dark;
#define M(f) T.f = mixc(a->f, b->f, t)
    M(bg_top); M(bg_bottom); M(stripe); M(bar_top); M(bar_bottom); M(bar_line);
    M(panel); M(panel_head); M(row_line); M(text); M(text_dim); M(text_faint);
    M(accent); M(accent_dark); M(accent_light); M(accent_text); M(sel_fill); M(sel_line);
    M(shadow); M(dim); M(glyph_bg); M(glyph_fg); M(tile_bg); M(chip_bg);
#undef M
    T.art_alpha = lerpf(a->art_alpha, b->art_alpha, t);
    T.veil = lerpf(a->veil, b->veil, t);
}

Color lighten(Color c, float t)
{
    return col_mix(c, (Color){ 255, 255, 255, c.a }, t);
}

Color darken(Color c, float t)
{
    return col_mix(c, (Color){ 0, 0, 0, c.a }, t);
}

Color theme_color_of(uint32_t hex)
{
    Color c = rgb(hex);
    return ui.theme_t > 0.5f ? lighten(c, 0.08f) : c;
}

// ------------------------------------------------------------ glyphs

float glyph_width(Button b, float size)
{
    switch (b) {
    case BTN_L: case BTN_R: return size * 1.3f;
    case BTN_ZL: case BTN_ZR: return size * 1.6f;
    default: return size;
    }
}

float draw_glyph(Button b, float x, float cy, float size)
{
    float w = glyph_width(b, size);
    Color bg = T.glyph_bg, fg = T.glyph_fg;
    const char *txt = NULL;
    switch (b) {
    case BTN_A: txt = "A"; break;
    case BTN_B: txt = "B"; break;
    case BTN_X: txt = "X"; break;
    case BTN_Y: txt = "Y"; break;
    case BTN_L: txt = "L"; break;
    case BTN_R: txt = "R"; break;
    case BTN_ZL: txt = "ZL"; break;
    case BTN_ZR: txt = "ZR"; break;
    default: break;
    }
    if (b == BTN_L || b == BTN_R || b == BTN_ZL || b == BTN_ZR) {
        float h = size * 0.78f;
        gfx_rrect(x, cy - h * 0.5f, w, h, h * 0.42f, bg);
        int ts = (int)(size * 0.56f);
        text_draw(ts, txt, x + w * 0.5f, cy - ts * 0.6f, fg, ALIGN_CENTER);
    } else if (b == BTN_PLUS || b == BTN_MINUS) {
        gfx_circle(x + size * 0.5f, cy, size * 0.5f, bg);
        float l = size * 0.5f, t = size * 0.12f;
        gfx_rrect(x + (size - l) * 0.5f, cy - t * 0.5f, l, t, t * 0.4f, fg);
        if (b == BTN_PLUS)
            gfx_rrect(x + (size - t) * 0.5f, cy - l * 0.5f, t, l, t * 0.4f, fg);
    } else if (b >= BTN_UP && b <= BTN_RIGHT) {
        float t = size * 0.34f;
        gfx_rrect(x, cy - t * 0.5f, size, t, t * 0.25f, bg);
        gfx_rrect(x + (size - t) * 0.5f, cy - size * 0.5f, t, size, t * 0.25f, bg);
        // highlight the arm that matters
        float hx = x + (size - t) * 0.5f, hy = cy - t * 0.5f;
        if (b == BTN_UP) hy = cy - size * 0.5f;
        if (b == BTN_DOWN) hy = cy + size * 0.5f - t;
        if (b == BTN_LEFT) hx = x;
        if (b == BTN_RIGHT) hx = x + size - t;
        gfx_rrect(hx + t * 0.25f, hy + t * 0.25f, t * 0.5f, t * 0.5f, t * 0.2f, col_alpha(fg, 0.9f));
    } else {
        gfx_circle(x + size * 0.5f, cy, size * 0.5f, bg);
        int ts = (int)(size * 0.62f);
        text_draw(ts, txt, x + size * 0.5f, cy - ts * 0.6f, fg, ALIGN_CENTER);
    }
    return w;
}

float draw_hints(const Button *btns, const char *const *labels, int n, float x, float cy, int align)
{
    const float gs = 26, ts = 19, gap = 26;
    float total = 0;
    for (int i = 0; i < n; i++) {
        bool pair = !labels[i][0];
        total += glyph_width(btns[i], gs) + (pair ? 4 : 7 + text_width(ts, labels[i]) + (i + 1 < n ? gap : 0));
    }
    if (align == ALIGN_RIGHT)
        x -= total;
    else if (align == ALIGN_CENTER)
        x -= total * 0.5f;
    for (int i = 0; i < n; i++) {
        float x0 = x;
        if (!labels[i][0]) {
            x += draw_glyph(btns[i], x, cy, gs);
            hit_add(x0 - 4, cy - 22, x - x0 + 6, 44, HIT_BUTTON, btns[i]);
            x += 4;
            continue;
        }
        x += draw_glyph(btns[i], x, cy, gs) + 7;
        x += text_draw(ts, labels[i], x, cy - ts * 0.62f, T.text, ALIGN_LEFT);
        hit_add(x0 - 6, cy - 22, x - x0 + 12, 44, HIT_BUTTON, btns[i]);
        x += gap;
    }
    return total;
}

// ------------------------------------------------------------ badges, chips

void draw_badge(int icon, uint32_t color, SDL_Texture *logo, float x, float y, float size)
{
    float r = size * 0.26f;
    if (size >= 40)
        gfx_shadow(x, y + size * 0.06f, size, size, r, size * 0.18f, col_alpha(T.shadow, 0.6f));
    if (logo) {
        gfx_rrect(x, y, size, size, r, rgb(0xFFFFFF));
        float pad = size * 0.08f;
        gfx_image_contain(logo, x + pad, y + pad, size - pad * 2, size - pad * 2, r * 0.6f, rgb(0xFFFFFF));
        gfx_rrect_outline(x, y, size, size, r, 1, rgba(0x000000, 0.12f));
        return;
    }
    Color c = rgb(color);
    gfx_rrect_vgrad(x, y, size, size, r, lighten(c, 0.22f), darken(c, 0.06f));
    gfx_rrect_vgrad(x + 1, y + 1, size - 2, size * 0.5f, r, rgba(0xFFFFFF, 0.22f), rgba(0xFFFFFF, 0.02f));
    float is = size * 0.64f;
    icon_draw(icon, x + (size - is) * 0.5f, y + (size - is) * 0.5f, is, rgb(0xFFFFFF));
}

void draw_tab_badge(const Tab *t, float x, float y, float size)
{
    SDL_Texture *logo = NULL;
    if (t->kind == TAB_CAT) {
        Category *c = cat_by_id(t->cat);
        if (c && c->logo_tex)
            logo = c->logo_tex;
    }
    draw_badge(t->icon, t->color, logo, x, y, size);
}

float chip_width(const char *text, int icon)
{
    return text_width(17, text) + 24 + (icon >= 0 ? 22 : 0);
}

void draw_chip(float x, float y, const char *text, int icon, Color bg, Color fg, float *out_w)
{
    float w = chip_width(text, icon), h = 32;
    gfx_rrect(x, y, w, h, h * 0.5f, bg);
    float tx = x + 12;
    if (icon >= 0) {
        icon_draw(icon, tx - 1, y + 7, 18, fg);
        tx += 22;
    }
    text_draw(17, text, tx, y + h * 0.5f - 17 * 0.62f, fg, ALIGN_LEFT);
    if (out_w)
        *out_w = w;
}

void draw_toggle(float x, float y, bool on, float alpha)
{
    gfx_push_alpha(alpha);
    float w = 60, h = 32;
    Color bg = on ? T.accent : col_mix(T.text_faint, T.panel, 0.3f);
    gfx_rrect(x, y, w, h, h * 0.5f, bg);
    float kx = on ? x + w - h + 3 : x + 3;
    gfx_shadow(kx, y + 4, h - 6, h - 6, (h - 6) * 0.5f, 5, col_alpha(T.shadow, 0.6f));
    gfx_circle(kx + (h - 6) * 0.5f, y + h * 0.5f, (h - 6) * 0.5f, rgb(0xFFFFFF));
    gfx_pop_alpha();
}

void draw_check(float x, float y, float size, bool on)
{
    float r = size * 0.24f;
    if (on) {
        gfx_rrect_vgrad(x, y, size, size, r, T.accent_light, T.accent);
        icon_draw(IC_CHECK, x + size * 0.14f, y + size * 0.14f, size * 0.72f, rgb(0xFFFFFF));
    } else {
        gfx_rrect(x, y, size, size, r, T.panel);
        gfx_rrect_outline(x, y, size, size, r, 2, T.text_faint);
    }
}

void draw_spinner(float cx, float cy, float r, Color c)
{
    for (int i = 0; i < 8; i++) {
        float a = (float)(i * M_PI / 4) + ui.time * 6.0f;
        float k = (float)i / 8.0f;
        gfx_circle(cx + cosf(a) * r, cy + sinf(a) * r, r * 0.26f, col_alpha(c, 0.15f + 0.85f * k));
    }
}

void draw_sel_frame(float x, float y, float w, float h, float r, float amount)
{
    if (amount <= 0.01f)
        return;
    float pulse = 0.72f + 0.28f * sinf(ui.time * 4.6f);
    gfx_push_alpha(amount);
    gfx_glow(x - 4, y - 4, w + 8, h + 8, r + 4, 14, col_alpha(T.sel_line, 0.42f * pulse));
    gfx_rrect_outline(x - 5, y - 5, w + 10, h + 10, r + 5, 5, col_mix(T.sel_line, rgb(0xFFFFFF), 0.25f * (1 - pulse)));
    gfx_rrect_outline(x - 1, y - 1, w + 2, h + 2, r + 1, 1.5f, rgba(0xFFFFFF, 0.9f));
    gfx_pop_alpha();
}

void draw_button(float x, float y, float w, float h, Button glyph, const char *label, bool primary, float focus,
                 int hit_param)
{
    float r = h * 0.5f;
    gfx_shadow(x, y + 3, w, h, r, 12, col_alpha(T.shadow, primary ? 0.7f : 0.45f));
    if (primary) {
        gfx_rrect_vgrad(x, y, w, h, r, T.accent_light, T.accent_dark);
        gfx_rrect_vgrad(x + 2, y + 2, w - 4, h * 0.48f, r - 2, rgba(0xFFFFFF, 0.32f), rgba(0xFFFFFF, 0.04f));
    } else {
        gfx_rrect_vgrad(x, y, w, h, r, T.panel, col_mix(T.panel, T.bar_bottom, 0.8f));
        gfx_rrect_outline(x, y, w, h, r, 1.5f, col_alpha(T.bar_line, 0.9f));
    }
    Color fg = primary ? T.accent_text : T.text;
    int ts = h >= 56 ? 24 : 20;
    float gs = h * 0.5f;
    float tw = text_width(ts, label) + gs + 10;
    float cx = x + (w - tw) * 0.5f;
    if (primary) {
        // glyph in white on the blue button
        gfx_circle(cx + gs * 0.5f, y + h * 0.5f, gs * 0.5f, rgba(0xFFFFFF, 0.95f));
        const char *l = glyph == BTN_A ? "A" : glyph == BTN_X ? "X" : glyph == BTN_Y ? "Y" : "B";
        text_draw((int)(gs * 0.62f), l, cx + gs * 0.5f, y + h * 0.5f - gs * 0.62f * 0.6f, T.accent_dark,
                  ALIGN_CENTER);
    } else {
        draw_glyph(glyph, cx, y + h * 0.5f, gs);
    }
    text_draw(ts, label, cx + gs + 10, y + h * 0.5f - ts * 0.62f, fg, ALIGN_LEFT);
    if (focus > 0)
        draw_sel_frame(x, y, w, h, r, focus);
    hit_add(x, y, w, h, hit_param == BTN_A ? HIT_PLAY : HIT_BUTTON, hit_param);
}

void draw_round_button(float x, float y, float size, int icon, int hit_kind, int hit_param)
{
    float r = size * 0.5f;
    gfx_shadow(x, y + 3, size, size, r, 12, col_alpha(T.shadow, 0.45f));
    gfx_rrect_vgrad(x, y, size, size, r, T.panel, col_mix(T.panel, T.bar_bottom, 0.8f));
    gfx_rrect_outline(x, y, size, size, r, 1.5f, col_alpha(T.bar_line, 0.9f));
    float is = size * 0.5f;
    icon_draw(icon, x + (size - is) * 0.5f, y + (size - is) * 0.5f, is, T.text);
    hit_add(x, y, size, size, hit_kind, hit_param);
}

// ------------------------------------------------------------ covers & tiles

static void synth_cover(Game *g, float x, float y, float w, float h, float r)
{
    float band = h * 0.085f;
    // art area (clipped below the band)
    gfx_clip((int)x, (int)(y + band), (int)w + 1, (int)(h - band) + 1);
    Color t = g->tint;
    gfx_rrect_vgrad(x, y, w, h, r, lighten(t, 0.35f), darken(t, 0.25f));
    if (g->boot_blur)
        gfx_image_cover(g->boot_blur, x, y + band, w, h - band, r, rgb(0xFFFFFF));
    gfx_rect_vgrad(x, y + band, w, h - band, rgba(0x000000, 0.0f), rgba(0x000000, 0.28f));
    gfx_unclip();
    float art_y = y + band;
    if (g->boot) {
        float bw = w - 18, bh = bw * 9.0f / 16.0f;
        float by = art_y + (h - band) * 0.16f;
        gfx_shadow(x + 9, by + 2, bw, bh, 6, 12, rgba(0x000000, 0.35f));
        gfx_image(g->boot, NULL, x + 9, by, bw, bh, 6, rgb(0xFFFFFF));
        gfx_rrect_outline(x + 9, by, bw, bh, 6, 1.5f, rgba(0xFFFFFF, 0.5f));
    } else if (g->name1[0]) {
        char lines[3][256];
        int n = text_wrap(20, g->name1, w - 30, 3, lines);
        float ty = art_y + (h - band) * 0.22f;
        for (int i = 0; i < n; i++)
            text_draw_shadow(20, lines[i], x + w * 0.5f, ty + i * 26, rgb(0xFFFFFF), rgba(0x000000, 0.4f),
                             ALIGN_CENTER);
    }
    float is = w * 0.38f;
    float ix = x + (w - is) * 0.5f, iy = y + h - is - h * 0.07f;
    if (g->icon) {
        gfx_shadow(ix, iy + 3, is, is, is * 0.16f, 14, rgba(0x000000, 0.45f));
        gfx_rrect(ix - 3, iy - 3, is + 6, is + 6, is * 0.16f + 3, rgb(0xFFFFFF));
        gfx_image(g->icon, NULL, ix, iy, is, is, is * 0.14f, rgb(0xFFFFFF));
    }
    // band with the system name: blue Wii U, grey Wii, indigo GameCube, dark retro
    int sys = game_system(g);
    Color top = rgb(0x1AA6E0), bot = rgb(0x0077B8), fg = rgb(0xFFFFFF);
    const char *label = "Wii U";
    if (sys == SYS_WII) {
        top = rgb(0xE6E9ED);
        bot = rgb(0xB9BFC6);
        fg = rgb(0x4A5058);
        label = "Wii";
    } else if (sys == SYS_GC) {
        top = rgb(0x5546B0);
        bot = rgb(0x2E2766);
        label = "GameCube";
    } else if (sys != SYS_WIIU) {
        top = rgb(0x3B3E46);
        bot = rgb(0x24262B);
        label = game_system_name(g);
    }
    gfx_clip((int)x, (int)y, (int)w + 1, (int)(band + 0.5f));
    gfx_rrect_vgrad(x, y, w, h, r, top, bot);
    gfx_unclip();
    gfx_rect(x, y + band - 1, w, 2, rgba(0xFFFFFF, 0.85f));
    int ts = (int)(band * 0.66f);
    char fit[64];
    text_fit(ts, label, w - 8, fit, sizeof(fit));
    text_draw(ts, fit, x + w * 0.5f, y + band * 0.5f - ts * 0.6f, fg, ALIGN_CENTER);
}

void draw_cover(Game *g, float x, float y, float w, float h, float alpha)
{
    if (!g)
        return;
    gfx_push_alpha(alpha);
    float r = w * 0.035f + 2;
    gfx_shadow(x, y + h * 0.02f, w, h, r, w * 0.09f, T.shadow);
    if (g->cover) {
        int tw = 1, th = 1;
        SDL_QueryTexture(g->cover, NULL, NULL, &tw, &th);
        float aspect = (float)tw / (float)th;
        if (aspect > 0.62f && aspect < 0.8f) {
            gfx_rrect(x, y, w, h, r, rgb(0xFFFFFF));
            gfx_image_cover(g->cover, x, y, w, h, r, rgb(0xFFFFFF));
        } else {
            // boxes of other shapes (N64 boxes are wide, DS ones almost square):
            // the whole cover over a dimmed, enlarged copy of itself
            gfx_rrect(x, y, w, h, r, rgb(0x14181D));
            gfx_image_cover(g->cover, x, y, w, h, r, rgba(0xFFFFFF, 0.35f));
            gfx_rrect_vgrad(x, y, w, h, r, rgba(0x000000, 0.35f), rgba(0x000000, 0.6f));
            float pad = w * 0.06f;
            float s = fminf((w - 2 * pad) / tw, (h - 2 * pad) / th);
            float dw = tw * s, dh = th * s;
            float dx = x + (w - dw) * 0.5f, dy = y + (h - dh) * 0.5f;
            gfx_shadow(dx, dy + 3, dw, dh, 4, 14, rgba(0x000000, 0.55f));
            gfx_image(g->cover, NULL, dx, dy, dw, dh, 4, rgb(0xFFFFFF));
        }
    } else {
        synth_cover(g, x, y, w, h, r);
    }
    // case gloss and spine
    gfx_rrect_vgrad(x, y, w, h * 0.45f, r, rgba(0xFFFFFF, 0.16f), rgba(0xFFFFFF, 0.0f));
    gfx_rect_hgrad(x + 2, y + r, w * 0.045f, h - 2 * r, rgba(0x000000, 0.16f), rgba(0x000000, 0.0f));
    gfx_rrect_outline(x, y, w, h, r, 1, rgba(0x000000, 0.18f));
    if (!g->cover && (g->cover_state == ASSET_LOADING || g->dl_state == DL_QUEUED) && !g->boot)
        draw_spinner(x + w * 0.5f, y + h * 0.45f, w * 0.06f, rgba(0xFFFFFF, 0.9f));
    gfx_pop_alpha();
}

void draw_tile(Game *g, float x, float y, float size, float sel, float alpha)
{
    gfx_push_alpha(alpha);
    float r = size * 0.15f;
    gfx_shadow(x, y + size * 0.04f + sel * 4, size, size, r, size * (0.12f + sel * 0.08f),
               col_alpha(T.shadow, 0.75f + sel * 0.25f));
    if (g->icon) {
        gfx_image(g->icon, NULL, x, y, size, size, r, g->hidden && !sel ? rgba(0xFFFFFF, 0.55f) : rgb(0xFFFFFF));
        gfx_rrect_vgrad(x, y, size, size * 0.5f, r, rgba(0xFFFFFF, 0.14f), rgba(0xFFFFFF, 0.0f));
    } else {
        Color t = g->tint;
        gfx_rrect_vgrad(x, y, size, size, r, lighten(t, 0.45f), lighten(t, 0.1f));
        char letter[8] = "?";
        if (g->name1[0]) {
            const char *p = g->name1;
            int cp = utf8_decode(&p);
            letter[utf8_encode(cp, letter)] = 0;
        }
        int ts = (int)(size * 0.46f);
        text_draw(ts, letter, x + size * 0.5f, y + size * 0.5f - ts * 0.62f, rgba(0xFFFFFF, 0.95f), ALIGN_CENTER);
        if (g->icon_state == ASSET_LOADING)
            draw_spinner(x + size * 0.5f, y + size * 0.82f, size * 0.05f, rgba(0xFFFFFF, 0.8f));
    }
    gfx_rrect_outline(x, y, size, size, r, 1, rgba(0x000000, 0.10f));
    float bs = size * 0.26f;
    if (g->favorite) {
        float bx = x + size - bs * 0.8f, by = y + size - bs * 0.8f;
        gfx_shadow(bx, by + 1, bs, bs, bs * 0.5f, 5, rgba(0x000000, 0.35f));
        gfx_circle(bx + bs * 0.5f, by + bs * 0.5f, bs * 0.5f, rgb(0xFFB300));
        icon_draw(IC_STAR, bx + bs * 0.18f, by + bs * 0.16f, bs * 0.64f, rgb(0xFFFFFF));
    }
    if (g->hidden) {
        float bx = x + size - bs * 0.8f, by = y - bs * 0.2f;
        gfx_circle(bx + bs * 0.5f, by + bs * 0.5f, bs * 0.5f, rgb(0x6C747D));
        icon_draw(IC_EYE_OFF, bx + bs * 0.18f, by + bs * 0.18f, bs * 0.64f, rgb(0xFFFFFF));
    }
    bool is_new = !L.first_run && g->play_count == 0 && g->first_seen > 1 &&
                  time_now() - g->first_seen < 7 * 86400;
    if (is_new) {
        int ts = (int)(size * 0.13f);
        if (ts < 11) ts = 11;
        float tw = text_width(ts, "NUEVO") + ts;
        float bx = x - 4, by = y - 6;
        gfx_rrect(bx, by, tw, ts * 1.6f, ts * 0.8f, rgb(0xFF6D3A));
        text_draw(ts, "NUEVO", bx + tw * 0.5f, by + ts * 0.8f - ts * 0.6f, rgb(0xFFFFFF), ALIGN_CENTER);
    }
    gfx_pop_alpha();
}

// ------------------------------------------------------------ background & top bar

void draw_background(void)
{
    gfx_rect_vgrad(0, 0, SCREEN_W, SCREEN_H, T.bg_top, T.bg_bottom);
    if (s_stripes) {
        SDL_SetTextureColorMod(s_stripes, T.stripe.r, T.stripe.g, T.stripe.b);
        SDL_SetTextureAlphaMod(s_stripes, (uint8_t)(T.stripe.a * gfx_alpha()));
        for (int y = 0; y < SCREEN_H; y += 256)
            for (int x = 0; x < SCREEN_W; x += 256) {
                SDL_Rect d = { x, y, 256, 256 };
                SDL_RenderCopy(g_ren, s_stripes, NULL, &d);
            }
    }
    if (!L.set.bg_art)
        return;
    // blurred boot art of the focused game, cross-faded
    Game *cur = ui.bg_game >= 0 && ui.bg_game < L.count ? &L.games[ui.bg_game] : NULL;
    Game *prev = ui.bg_prev >= 0 && ui.bg_prev < L.count ? &L.games[ui.bg_prev] : NULL;
    float t = ease_in_out(ui.bg_t);
    if (prev && prev->boot_blur && t < 1)
        gfx_image_cover(prev->boot_blur, -20, -20, SCREEN_W + 40, SCREEN_H + 40, 0,
                        rgba(0xFFFFFF, T.art_alpha * (1 - t)));
    if (cur && cur->boot_blur)
        gfx_image_cover(cur->boot_blur, -20, -20, SCREEN_W + 40, SCREEN_H + 40, 0,
                        rgba(0xFFFFFF, T.art_alpha * (prev && prev->boot_blur ? t : t * ui.art_fade)));
    // veil that keeps text readable and the Wii U look (light, airy)
    Color top = col_alpha(T.bg_top, T.veil * 0.82f), bot = col_alpha(T.bg_bottom, fminf(1, T.veil * 1.3f));
    gfx_rect_vgrad(0, 0, SCREEN_W, SCREEN_H * 0.62f, top, col_alpha(T.bg_bottom, T.veil));
    gfx_rect_vgrad(0, SCREEN_H * 0.62f, SCREEN_W, SCREEN_H * 0.38f, col_alpha(T.bg_bottom, T.veil), bot);
}

static void split_now(struct tm *tm)
{
    time_t t = (time_t)time_now();
#ifdef __WIIU__
    gmtime_r(&t, tm);
#else
    localtime_r(&t, tm);
#endif
}

void draw_top_bar(void)
{
    const float H = 72;
    gfx_rect_vgrad(0, 0, SCREEN_W, H, T.bar_top, T.bar_bottom);
    gfx_rect(0, H - 1, SCREEN_W, 1, T.bar_line);
    gfx_rect_vgrad(0, H, SCREEN_W, 10, col_alpha(T.shadow, 0.35f), col_alpha(T.shadow, 0));

    // app mark: Pixmo Games' mascot and its name in pixel letters, in the text colour
    brand_draw_icon(18, 13, 46, 1);
    brand_draw_name(74, 12, 3, true, ui.theme_t > 0.5f ? rgb(0xFFFFFF) : T.text);

    // tabs
    const float tx0 = 208, tx1 = L.set.show_clock ? 1128 : 1250, th = 44, ty = 14;
    float x = 0;
    float tab_x[64], tab_w[64];
    for (int i = 0; i < ui.ntabs; i++) {
        tab_w[i] = 8 + 28 + 8 + text_width(19, ui.tabs[i].name) + 15;
        tab_x[i] = x;
        x += tab_w[i] + 6;
    }
    float total = x - 6;
    // keep the selected tab visible
    float target_scroll = ui.tabs_scroll;
    if (ui.ntabs > 0) {
        float sx = tab_x[ui.tab], sw = tab_w[ui.tab];
        if (sx - target_scroll < 0)
            target_scroll = sx - 40;
        if (sx + sw - target_scroll > tx1 - tx0)
            target_scroll = sx + sw - (tx1 - tx0) + 40;
        if (target_scroll > total - (tx1 - tx0))
            target_scroll = total - (tx1 - tx0);
        if (target_scroll < 0)
            target_scroll = 0;
        ui.tabs_scroll = approach(ui.tabs_scroll, target_scroll, 14, 1.0f / 60);
        float ix = tab_x[ui.tab], iw = tab_w[ui.tab];
        if (!ui.tab_ind_init) {
            ui.tab_ind_x = ix;
            ui.tab_ind_w = iw;
            ui.tab_ind_init = true;
        }
        ui.tab_ind_x = approach(ui.tab_ind_x, ix, 18, 1.0f / 60);
        ui.tab_ind_w = approach(ui.tab_ind_w, iw, 18, 1.0f / 60);
    }
    gfx_clip((int)tx0 - 8, 0, (int)(tx1 - tx0) + 16, (int)H);
    float off = tx0 - ui.tabs_scroll;
    // sliding indicator
    if (ui.ntabs > 0) {
        float ix = off + ui.tab_ind_x;
        gfx_shadow(ix, ty + 2, ui.tab_ind_w, th, th * 0.5f, 10, col_alpha(T.shadow, 0.6f));
        gfx_rrect_vgrad(ix, ty, ui.tab_ind_w, th, th * 0.5f, T.accent_light, T.accent_dark);
        gfx_rrect_vgrad(ix + 2, ty + 2, ui.tab_ind_w - 4, th * 0.45f, th * 0.5f - 2, rgba(0xFFFFFF, 0.3f),
                        rgba(0xFFFFFF, 0.03f));
    }
    for (int i = 0; i < ui.ntabs; i++) {
        float bx = off + tab_x[i];
        if (bx + tab_w[i] < tx0 - 8 || bx > tx1 + 8)
            continue;
        bool cur = i == ui.tab;
        draw_tab_badge(&ui.tabs[i], bx + 8, ty + 8, 28);
        text_draw(19, ui.tabs[i].name, bx + 8 + 28 + 8, ty + th * 0.5f - 19 * 0.62f,
                  cur ? T.accent_text : T.text_dim, ALIGN_LEFT);
        hit_add(bx, ty, tab_w[i], th, HIT_TAB, i);
    }
    if (ui.focus == FOCUS_TABS && ui.ntabs > 0)
        draw_sel_frame(off + ui.tab_ind_x, ty, ui.tab_ind_w, th, th * 0.5f, 1);
    gfx_unclip();
    // fade edges when the tab strip scrolls
    if (ui.tabs_scroll > 2)
        gfx_rect_hgrad(tx0 - 8, 0, 34, H - 1, T.bar_top, col_alpha(T.bar_top, 0));
    if (total - ui.tabs_scroll > tx1 - tx0 + 2)
        gfx_rect_hgrad(tx1 - 26, 0, 34, H - 1, col_alpha(T.bar_top, 0), T.bar_top);

    // clock
    float rx = SCREEN_W - 24;
    if (L.set.show_clock) {
        struct tm tm;
        split_now(&tm);
        char hm[16], date[32];
        snprintf(hm, sizeof(hm), "%d:%02d", tm.tm_hour, tm.tm_min);
        format_clock_date(&tm, date, sizeof(date));
        float w1 = text_width(28, hm);
        text_draw(28, hm, rx, 10, T.text, ALIGN_RIGHT);
        text_draw(15, date, rx, 46, T.text_dim, ALIGN_RIGHT);
        rx -= fmaxf(w1, text_width(15, date)) + 22;
    }
    // activity indicator: small pill under the bar
    (void)rx;
    if (lib_downloads_active() > 0 || L.meta_pending > 0) {
        char st[48];
        if (lib_downloads_active() > 0)
            str_copy(st, sizeof(st), "Descargando carátulas...");
        else
            str_copy(st, sizeof(st), "Leyendo juegos...");
        float tw = text_width(15, st) + 46;
        float px = SCREEN_W - 20 - tw, py = H + 8;
        gfx_rrect(px, py, tw, 28, 14, col_alpha(T.panel, 0.92f));
        gfx_rrect_outline(px, py, tw, 28, 14, 1, col_alpha(T.bar_line, 0.8f));
        draw_spinner(px + 16, py + 14, 6, T.accent);
        text_draw(15, st, px + 30, py + 14 - 15 * 0.62f, T.text_dim, ALIGN_LEFT);
    }
}

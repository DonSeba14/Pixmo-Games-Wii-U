// Marca de Pixmo Games. El icono va dentro del programa (src/pixmo_icono.h,
// generado por tools/make_assets.py); el nombre se arma con las letras 5x7 de
// Pixmo Games de PC (Proyecto/src/Interfaz/Marca.cs) en una textura de 1 px
// por celda que se dibuja ampliada sin suavizar.
#include "brand.h"
#include "gfx.h"
#include "image.h"
#include "pixmo_icono.h"

#include <string.h>

static const struct {
    char ch;
    const char *rows[7];
} k_glyphs[] = {
    { 'P', { "XXXX.", "X...X", "X...X", "XXXX.", "X....", "X....", "X...." } },
    { 'I', { "XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "XXXXX" } },
    { 'X', { "X...X", "X...X", ".X.X.", "..X..", ".X.X.", "X...X", "X...X" } },
    { 'M', { "X...X", "XX.XX", "X.X.X", "X.X.X", "X...X", "X...X", "X...X" } },
    { 'O', { ".XXX.", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX." } },
    { 'G', { ".XXX.", "X...X", "X....", "X.XXX", "X...X", "X...X", ".XXXX" } },
    { 'A', { ".XXX.", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X" } },
    { 'E', { "XXXXX", "X....", "X....", "XXXX.", "X....", "X....", "XXXXX" } },
    { 'S', { ".XXXX", "X....", "X....", ".XXX.", "....X", "....X", "XXXX." } },
};

static SDL_Texture *s_icon, *s_name[2];
static bool s_tried_icon, s_tried_name[2];

static const char *const *glyph(char c)
{
    for (size_t i = 0; i < sizeof(k_glyphs) / sizeof(k_glyphs[0]); i++)
        if (k_glyphs[i].ch == c)
            return k_glyphs[i].rows;
    return NULL;
}

// cells: one line "PIXMO GAMES" = 65 x 7; two lines = 29 x 16 (2 cells apart)
static void name_cells(bool two, int *w, int *h)
{
    *w = two ? 29 : 65;
    *h = two ? 16 : 7;
}

static void put_word(uint8_t *px, int stride, const char *word, int x0, int y0, const uint8_t rgb[3])
{
    for (int i = 0; word[i]; i++) {
        const char *const *g = glyph(word[i]);
        if (!g)
            continue;
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                if (g[r][c] == 'X') {
                    uint8_t *p = px + ((size_t)(y0 + r) * stride + x0 + i * 6 + c) * 4;
                    p[0] = rgb[0];
                    p[1] = rgb[1];
                    p[2] = rgb[2];
                    p[3] = 255;
                }
    }
}

static SDL_Texture *name_texture(bool two)
{
    int t = two ? 1 : 0;
    if (s_tried_name[t])
        return s_name[t];
    s_tried_name[t] = true;
    int w, h;
    name_cells(two, &w, &h);
    Image im;
    if (!img_alloc(&im, w, h))
        return NULL;
    memset(im.px, 0, (size_t)w * h * 4);
    static const uint8_t ice[3] = { 0xD1, 0xF7, 0xFF }, cyan[3] = { 0x05, 0xD9, 0xE8 };
    put_word(im.px, w, "PIXMO", 0, 0, ice);
    if (two)
        put_word(im.px, w, "GAMES", 0, 9, cyan);
    else
        put_word(im.px, w, "GAMES", 36, 0, cyan);
    s_name[t] = img_to_texture(g_ren, &im);
    img_free(&im);
    if (s_name[t])
        SDL_SetTextureScaleMode(s_name[t], SDL_ScaleModeNearest); // crisp pixels at any size
    return s_name[t];
}

void brand_draw_icon(float x, float y, float size, float alpha)
{
    if (!s_tried_icon) {
        s_tried_icon = true;
        Image im;
        if (img_load_mem(k_pixmo_icono_png, sizeof(k_pixmo_icono_png), &im)) {
            s_icon = img_to_texture(g_ren, &im);
            img_free(&im);
        }
    }
    if (s_icon)
        gfx_image(s_icon, NULL, x, y, size, size, 0, rgba(0xFFFFFF, alpha));
}

float brand_name_width(int cell, bool two_lines)
{
    int w, h;
    name_cells(two_lines, &w, &h);
    return (float)(w * cell);
}

float brand_name_height(int cell, bool two_lines)
{
    int w, h;
    name_cells(two_lines, &w, &h);
    return (float)(h * cell);
}

void brand_draw_name(float x, float y, int cell, bool two_lines, Color tint)
{
    SDL_Texture *t = name_texture(two_lines);
    if (!t)
        return;
    int w, h;
    name_cells(two_lines, &w, &h);
    // whole pixels: the letters stay sharp
    gfx_image(t, NULL, floorf(x + 0.5f), floorf(y + 0.5f), (float)(w * cell), (float)(h * cell), 0, tint);
}

// Primitivas suavizadas: los bordes se dibujan con una franja de 1 px cuyo
// alfa cae a 0 (antialias sin multisampling), y las sombras/brillos usan
// franjas anchas con caída gradual. Todo va por SDL_RenderGeometry.
#include "gfx.h"

SDL_Renderer *g_ren;

static float s_alpha = 1.0f;
static float s_alpha_stack[32];
static int s_alpha_sp;

#define MAXV 1024
#define MAXI 4096
static SDL_Vertex V[MAXV];
static int I[MAXI];

void gfx_init(SDL_Renderer *r)
{
    g_ren = r;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
}

void gfx_push_alpha(float a)
{
    if (s_alpha_sp < 32)
        s_alpha_stack[s_alpha_sp++] = s_alpha;
    s_alpha *= clampf(a, 0, 1);
}

void gfx_pop_alpha(void)
{
    if (s_alpha_sp > 0)
        s_alpha = s_alpha_stack[--s_alpha_sp];
}

float gfx_alpha(void)
{
    return s_alpha;
}

void gfx_clip(int x, int y, int w, int h)
{
    SDL_Rect r = { x, y, w, h };
    SDL_RenderSetClipRect(g_ren, &r);
}

void gfx_unclip(void)
{
    SDL_RenderSetClipRect(g_ren, NULL);
}

static inline SDL_Color vcol(Color c, float a)
{
    SDL_Color o = { c.r, c.g, c.b, (uint8_t)clampf(c.a * a * s_alpha + 0.5f, 0, 255) };
    return o;
}

void gfx_geometry(SDL_Texture *t, const SDL_Vertex *v, int nv, const int *idx, int ni)
{
    if (nv > 0 && ni > 0)
        SDL_RenderGeometry(g_ren, t, v, nv, idx, ni);
}

void gfx_rect_vgrad(float x, float y, float w, float h, Color top, Color bottom)
{
    if (w <= 0 || h <= 0)
        return;
    SDL_Vertex v[4] = {
        { { x, y }, vcol(top, 1), { 0, 0 } },
        { { x + w, y }, vcol(top, 1), { 0, 0 } },
        { { x + w, y + h }, vcol(bottom, 1), { 0, 0 } },
        { { x, y + h }, vcol(bottom, 1), { 0, 0 } },
    };
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(g_ren, NULL, v, 4, idx, 6);
}

void gfx_rect_hgrad(float x, float y, float w, float h, Color left, Color right)
{
    if (w <= 0 || h <= 0)
        return;
    SDL_Vertex v[4] = {
        { { x, y }, vcol(left, 1), { 0, 0 } },
        { { x + w, y }, vcol(right, 1), { 0, 0 } },
        { { x + w, y + h }, vcol(right, 1), { 0, 0 } },
        { { x, y + h }, vcol(left, 1), { 0, 0 } },
    };
    static const int idx[6] = { 0, 1, 2, 0, 2, 3 };
    SDL_RenderGeometry(g_ren, NULL, v, 4, idx, 6);
}

void gfx_rect(float x, float y, float w, float h, Color c)
{
    gfx_rect_vgrad(x, y, w, h, c, c);
}

typedef struct Shape {
    float x, y, w, h, r;
    Color top, bottom;
    SDL_Texture *tex;
    float u0, v0, u1, v1;
} Shape;

// Builds concentric rounded-rect loops at the given offsets from the border
// (negative = inside) with per-loop alpha, optionally filling the first loop.
static void rr_build(const Shape *s, const float *offs, const float *alphas, int nloops, bool fill)
{
    float x = s->x, y = s->y, w = s->w, h = s->h;
    if (w <= 0 || h <= 0 || s_alpha <= 0.002f)
        return;
    float r = s->r;
    if (r > w * 0.5f) r = w * 0.5f;
    if (r > h * 0.5f) r = h * 0.5f;
    if (r < 0) r = 0;
    float maxoff = 0;
    for (int l = 0; l < nloops; l++)
        if (offs[l] > maxoff)
            maxoff = offs[l];
    int seg = (int)((r + maxoff) * 0.3f) + 2;
    if (seg > 12) seg = 12;
    int per = seg + 1, npts = per * 4;
    int nv = 0, ni = 0;
    if ((fill ? 1 : 0) + npts * nloops > MAXV)
        return;

    static const float a0s[4] = { (float)M_PI, (float)(1.5 * M_PI), 0.0f, (float)(0.5 * M_PI) };
    float cos_t[4][13], sin_t[4][13];
    for (int c = 0; c < 4; c++)
        for (int i = 0; i <= seg; i++) {
            float a = a0s[c] + (float)(0.5 * M_PI) * i / seg;
            cos_t[c][i] = cosf(a);
            sin_t[c][i] = sinf(a);
        }

    float inv_w = 1.0f / w, inv_h = 1.0f / h;
    int center = -1;
    if (fill) {
        center = nv++;
        float t = 0.5f;
        Color c = col_mix(s->top, s->bottom, t);
        V[center].position.x = x + w * 0.5f;
        V[center].position.y = y + h * 0.5f;
        V[center].color = vcol(c, alphas[0]);
        V[center].tex_coord.x = (s->u0 + s->u1) * 0.5f;
        V[center].tex_coord.y = (s->v0 + s->v1) * 0.5f;
    }
    int base[8];
    for (int l = 0; l < nloops; l++) {
        float o = offs[l];
        float cr = r > -o ? r : -o;  // corner center inset
        float rad = r + o > 0 ? r + o : 0;
        float cx[4] = { x + cr, x + w - cr, x + w - cr, x + cr };
        float cy[4] = { y + cr, y + cr, y + h - cr, y + h - cr };
        base[l] = nv;
        for (int c = 0; c < 4; c++) {
            for (int i = 0; i <= seg; i++) {
                SDL_Vertex *v = &V[nv++];
                float px = cx[c] + cos_t[c][i] * rad;
                float py = cy[c] + sin_t[c][i] * rad;
                v->position.x = px;
                v->position.y = py;
                float ty = clampf((py - y) * inv_h, 0, 1);
                v->color = vcol(col_mix(s->top, s->bottom, ty), alphas[l]);
                float tx = clampf((px - x) * inv_w, 0, 1);
                v->tex_coord.x = s->u0 + (s->u1 - s->u0) * tx;
                v->tex_coord.y = s->v0 + (s->v1 - s->v0) * ty;
            }
        }
    }
    if (fill) {
        for (int k = 0; k < npts; k++) {
            I[ni++] = center;
            I[ni++] = base[0] + k;
            I[ni++] = base[0] + (k + 1) % npts;
        }
    }
    for (int l = 0; l + 1 < nloops; l++) {
        for (int k = 0; k < npts; k++) {
            int k2 = (k + 1) % npts;
            int a = base[l] + k, b = base[l] + k2, c = base[l + 1] + k2, d = base[l + 1] + k;
            I[ni++] = a; I[ni++] = b; I[ni++] = c;
            I[ni++] = a; I[ni++] = c; I[ni++] = d;
        }
    }
    SDL_RenderGeometry(g_ren, s->tex, V, nv, I, ni);
}

void gfx_rrect_vgrad(float x, float y, float w, float h, float r, Color top, Color bottom)
{
    Shape s = { x, y, w, h, r, top, bottom, NULL, 0, 0, 1, 1 };
    static const float offs[2] = { -0.5f, 0.5f }, al[2] = { 1, 0 };
    rr_build(&s, offs, al, 2, true);
}

void gfx_rrect(float x, float y, float w, float h, float r, Color c)
{
    gfx_rrect_vgrad(x, y, w, h, r, c, c);
}

void gfx_rrect_outline(float x, float y, float w, float h, float r, float thick, Color c)
{
    Shape s = { x, y, w, h, r, c, c, NULL, 0, 0, 1, 1 };
    float a = thick < 1 ? thick : 1;
    if (thick < 1) thick = 1;
    float offs[4] = { -thick - 0.5f, -thick + 0.5f, -0.5f, 0.5f };
    float al[4] = { 0, a, a, 0 };
    rr_build(&s, offs, al, 4, false);
}

void gfx_shadow(float x, float y, float w, float h, float r, float blur, Color c)
{
    Shape s = { x, y, w, h, r, c, c, NULL, 0, 0, 1, 1 };
    float offs[4] = { -blur * 0.3f, blur * 0.12f, blur * 0.45f, blur };
    static const float al[4] = { 1.0f, 0.6f, 0.2f, 0.0f };
    rr_build(&s, offs, al, 4, true);
}

void gfx_glow(float x, float y, float w, float h, float r, float size, Color c)
{
    Shape s = { x, y, w, h, r, c, c, NULL, 0, 0, 1, 1 };
    float offs[4] = { -1.0f, 0.0f, size * 0.35f, size };
    static const float al[4] = { 1.0f, 1.0f, 0.4f, 0.0f };
    rr_build(&s, offs, al, 4, false);
}

void gfx_circle(float cx, float cy, float rad, Color c)
{
    gfx_rrect(cx - rad, cy - rad, rad * 2, rad * 2, rad, c);
}

void gfx_ring(float cx, float cy, float rad, float thick, Color c)
{
    gfx_rrect_outline(cx - rad, cy - rad, rad * 2, rad * 2, rad, thick, c);
}

void gfx_line(float x1, float y1, float x2, float y2, float thick, Color c)
{
    float dx = x2 - x1, dy = y2 - y1, len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f)
        return;
    float nx = -dy / len, ny = dx / len;
    float hi = thick * 0.5f - 0.5f, ho = thick * 0.5f + 0.5f;
    if (hi < 0) hi = 0;
    float a = thick < 1 ? thick : 1;
    SDL_Vertex v[8];
    float o[4] = { -ho, -hi, hi, ho };
    float al[4] = { 0, a, a, 0 };
    for (int i = 0; i < 4; i++) {
        v[i].position.x = x1 + nx * o[i];
        v[i].position.y = y1 + ny * o[i];
        v[i + 4].position.x = x2 + nx * o[i];
        v[i + 4].position.y = y2 + ny * o[i];
        v[i].color = v[i + 4].color = vcol(c, al[i]);
        v[i].tex_coord.x = v[i].tex_coord.y = 0;
        v[i + 4].tex_coord = v[i].tex_coord;
    }
    int idx[18], n = 0;
    for (int i = 0; i < 3; i++) {
        idx[n++] = i; idx[n++] = i + 1; idx[n++] = i + 5;
        idx[n++] = i; idx[n++] = i + 5; idx[n++] = i + 4;
    }
    SDL_RenderGeometry(g_ren, NULL, v, 8, idx, n);
}

void gfx_image(SDL_Texture *t, const SDL_Rect *src, float x, float y, float w, float h, float r, Color tint)
{
    if (!t)
        return;
    int tw, th;
    SDL_QueryTexture(t, NULL, NULL, &tw, &th);
    Shape s = { x, y, w, h, r, tint, tint, t, 0, 0, 1, 1 };
    if (src) {
        s.u0 = (float)src->x / tw;
        s.v0 = (float)src->y / th;
        s.u1 = (float)(src->x + src->w) / tw;
        s.v1 = (float)(src->y + src->h) / th;
    }
    static const float offs[2] = { -0.5f, 0.5f }, al[2] = { 1, 0 };
    rr_build(&s, offs, al, 2, true);
}

void gfx_image_cover(SDL_Texture *t, float x, float y, float w, float h, float r, Color tint)
{
    if (!t)
        return;
    int tw, th;
    SDL_QueryTexture(t, NULL, NULL, &tw, &th);
    float ta = w / h, sa = (float)tw / th;
    SDL_Rect src = { 0, 0, tw, th };
    if (sa > ta) {
        src.w = (int)(th * ta);
        src.x = (tw - src.w) / 2;
    } else {
        src.h = (int)(tw / ta);
        src.y = (th - src.h) / 2;
    }
    gfx_image(t, &src, x, y, w, h, r, tint);
}

void gfx_image_contain(SDL_Texture *t, float x, float y, float w, float h, float r, Color tint)
{
    if (!t)
        return;
    int tw, th;
    SDL_QueryTexture(t, NULL, NULL, &tw, &th);
    float s = fminf(w / tw, h / th);
    float dw = tw * s, dh = th * s;
    gfx_image(t, NULL, x + (w - dw) * 0.5f, y + (h - dh) * 0.5f, dw, dh, r, tint);
}

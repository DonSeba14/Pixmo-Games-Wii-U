// Iconos dibujados con campos de distancia con signo (SDF) en un atlas
// blanco con alfa; se tiñen al dibujarlos. Coordenadas: [-1,1], y hacia abajo.
#include "icons.h"
#include "gfx.h"
#include "platform.h"

#include <stdlib.h>
#include <string.h>

#define ICON_PX 128
#define ATLAS_COLS 8

typedef struct { float x, y; } v2;

static SDL_Texture *s_atlas;
static SDL_Renderer *s_ren;
static PlatThread *s_thread;
static SDL_atomic_t s_ready;
static uint8_t *s_px;
static int s_aw, s_ah;

static inline v2 V2(float x, float y) { v2 r = { x, y }; return r; }
static inline v2 sub(v2 a, v2 b) { return V2(a.x - b.x, a.y - b.y); }
static inline float dot(v2 a, v2 b) { return a.x * b.x + a.y * b.y; }
static inline float len(v2 p) { return sqrtf(p.x * p.x + p.y * p.y); }
static inline float U(float a, float b) { return fminf(a, b); }
static inline float S(float a, float b) { return fmaxf(a, -b); }    // a minus b
static inline float X(float a, float b) { return fmaxf(a, b); }     // intersection

static v2 rot(v2 p, float deg)
{
    float a = deg * (float)M_PI / 180.0f, c = cosf(a), s = sinf(a);
    return V2(c * p.x + s * p.y, -s * p.x + c * p.y);
}

static float circle(v2 p, float cx, float cy, float r) { return len(sub(p, V2(cx, cy))) - r; }
static float ring(v2 p, float cx, float cy, float r, float t) { return fabsf(circle(p, cx, cy, r)) - t; }

static float box(v2 p, float cx, float cy, float hx, float hy, float r)
{
    float dx = fabsf(p.x - cx) - hx + r, dy = fabsf(p.y - cy) - hy + r;
    float out = len(V2(fmaxf(dx, 0), fmaxf(dy, 0)));
    return out + fminf(fmaxf(dx, dy), 0) - r;
}

static float seg(v2 p, float ax, float ay, float bx, float by, float th)
{
    v2 pa = V2(p.x - ax, p.y - ay), ba = V2(bx - ax, by - ay);
    float h = clampf(dot(pa, ba) / dot(ba, ba), 0, 1);
    return len(V2(pa.x - ba.x * h, pa.y - ba.y * h)) - th;
}

static float poly(v2 p, const v2 *v, int n)
{
    float d = dot(sub(p, v[0]), sub(p, v[0]));
    float s = 1;
    for (int i = 0, j = n - 1; i < n; j = i, i++) {
        v2 e = sub(v[j], v[i]), w = sub(p, v[i]);
        float h = clampf(dot(w, e) / dot(e, e), 0, 1);
        v2 b = V2(w.x - e.x * h, w.y - e.y * h);
        d = fminf(d, dot(b, b));
        bool c1 = p.y >= v[i].y, c2 = p.y < v[j].y, c3 = e.x * w.y > e.y * w.x;
        if ((c1 && c2 && c3) || (!c1 && !c2 && !c3))
            s = -s;
    }
    return s * sqrtf(d);
}

static float star5(v2 p, float r, float rf)
{
    p.y = -p.y;
    const v2 k1 = { 0.809016994375f, -0.587785252292f };
    const v2 k2 = { -0.809016994375f, -0.587785252292f };
    p.x = fabsf(p.x);
    float d1 = fmaxf(dot(k1, p), 0);
    p = V2(p.x - 2 * d1 * k1.x, p.y - 2 * d1 * k1.y);
    float d2 = fmaxf(dot(k2, p), 0);
    p = V2(p.x - 2 * d2 * k2.x, p.y - 2 * d2 * k2.y);
    p.x = fabsf(p.x);
    p.y -= r;
    v2 ba = V2(rf * -k1.y - 0, rf * k1.x - 1);
    float h = clampf(dot(p, ba) / dot(ba, ba), 0, r);
    float sg = (p.y * ba.x - p.x * ba.y) >= 0 ? 1.0f : -1.0f;
    return len(V2(p.x - ba.x * h, p.y - ba.y * h)) * sg;
}

static float heart(v2 p)
{
    const float s = 1.38f;
    v2 q = V2(fabsf(p.x) / s, (0.78f - p.y) / s);
    float d;
    if (q.y + q.x > 1.0f) {
        d = len(sub(q, V2(0.25f, 0.75f))) - 0.35355339f;
    } else {
        v2 a = sub(q, V2(0, 1));
        float m = 0.5f * fmaxf(q.x + q.y, 0);
        v2 b = sub(q, V2(m, m));
        d = sqrtf(fminf(dot(a, a), dot(b, b))) * (q.x - q.y >= 0 ? 1.0f : -1.0f);
    }
    return d * s;
}

static float ellipse_ring(v2 p, float rx, float ry, float t)
{
    // approximate distance to an ellipse outline (good enough for icons)
    float k = len(V2(p.x / rx, p.y / ry));
    float g = len(V2(p.x / (rx * rx), p.y / (ry * ry)));
    float d = g > 0 ? k * (k - 1) / g : -fminf(rx, ry);
    return fabsf(d) - t;
}

static float icon_sdf(int id, v2 p)
{
    switch (id) {
    case IC_GAMEPAD: {
        float body = U(box(p, 0, 0.05f, 0.88f, 0.42f, 0.36f), U(circle(p, -0.55f, 0.32f, 0.36f), circle(p, 0.55f, 0.32f, 0.36f)));
        float dpad = U(box(p, -0.48f, 0.02f, 0.2f, 0.065f, 0.02f), box(p, -0.48f, 0.02f, 0.065f, 0.2f, 0.02f));
        float btn = U(circle(p, 0.4f, 0.1f, 0.085f), circle(p, 0.6f, -0.08f, 0.085f));
        return S(S(body, dpad), btn);
    }
    case IC_CROSSHAIR: {
        float d = ring(p, 0, 0, 0.56f, 0.085f);
        d = U(d, seg(p, 0, -0.92f, 0, -0.3f, 0.075f));
        d = U(d, seg(p, 0, 0.3f, 0, 0.92f, 0.075f));
        d = U(d, seg(p, -0.92f, 0, -0.3f, 0, 0.075f));
        d = U(d, seg(p, 0.3f, 0, 0.92f, 0, 0.075f));
        return U(d, circle(p, 0, 0, 0.11f));
    }
    case IC_SWORD: {
        const float k = 1.3f;
        v2 q = rot(V2(p.x / k, p.y / k), -45);
        v2 blade[5] = { { -0.13f, 0.28f }, { -0.13f, -0.6f }, { 0, -0.84f }, { 0.13f, -0.6f }, { 0.13f, 0.28f } };
        float d = poly(q, blade, 5);
        d = U(d, box(q, 0, 0.34f, 0.36f, 0.075f, 0.04f));
        d = U(d, box(q, 0, 0.58f, 0.075f, 0.2f, 0.03f));
        d = U(d, circle(q, 0, 0.82f, 0.12f));
        return d * k;
    }
    case IC_CAR: {
        const float k = 1.08f;
        p = V2(p.x / k, p.y / k + 0.04f);
        float body = box(p, 0, 0.12f, 0.88f, 0.22f, 0.14f);
        v2 cab[4] = { { -0.52f, -0.06f }, { 0.42f, -0.06f }, { 0.2f, -0.46f }, { -0.32f, -0.46f } };
        float d = U(body, poly(p, cab, 4) - 0.04f);
        v2 win[4] = { { -0.36f, -0.13f }, { 0.28f, -0.13f }, { 0.13f, -0.36f }, { -0.24f, -0.36f } };
        d = S(d, poly(p, win, 4));
        d = S(d, box(p, -0.04f, -0.24f, 0.035f, 0.14f, 0));
        d = S(d, U(circle(p, -0.48f, 0.38f, 0.27f), circle(p, 0.5f, 0.38f, 0.27f)));
        d = U(d, U(circle(p, -0.48f, 0.38f, 0.19f), circle(p, 0.5f, 0.38f, 0.19f)));
        return d * k;
    }
    case IC_PUZZLE: {
        float d = box(p, -0.08f, 0.1f, 0.58f, 0.58f, 0.1f);
        d = U(d, circle(p, -0.08f, -0.58f, 0.2f));
        d = U(d, circle(p, 0.6f, 0.1f, 0.2f));
        d = S(d, circle(p, -0.08f, 0.78f, 0.19f));
        d = S(d, circle(p, -0.76f, 0.1f, 0.19f));
        return d;
    }
    case IC_STAR:
        return star5(V2(p.x, p.y - 0.06f), 0.9f, 0.48f) - 0.03f;
    case IC_STAR_OUTLINE: {
        float d = star5(V2(p.x, p.y - 0.06f), 0.9f, 0.48f) - 0.03f;
        return fabsf(d + 0.07f) - 0.07f;
    }
    case IC_HEART:
        return heart(p);
    case IC_NOTE: {
        float d = circle(p, -0.4f, 0.55f, 0.22f);
        d = U(d, circle(p, 0.5f, 0.4f, 0.22f));
        d = U(d, seg(p, -0.2f, 0.5f, -0.2f, -0.6f, 0.07f));
        d = U(d, seg(p, 0.7f, 0.35f, 0.7f, -0.78f, 0.07f));
        v2 beam[4] = { { -0.27f, -0.66f }, { 0.77f, -0.88f }, { 0.77f, -0.62f }, { -0.27f, -0.4f } };
        return U(d, poly(p, beam, 4));
    }
    case IC_BALL: {
        float d = ring(p, 0, 0, 0.8f, 0.075f);
        v2 pent[5];
        for (int i = 0; i < 5; i++) {
            float a = (float)(-M_PI / 2 + i * 2 * M_PI / 5);
            pent[i] = V2(cosf(a) * 0.3f, sinf(a) * 0.3f);
            d = U(d, seg(p, pent[i].x, pent[i].y, cosf(a) * 0.8f, sinf(a) * 0.8f, 0.06f));
        }
        return U(d, poly(p, pent, 5));
    }
    case IC_BOLT: {
        v2 b[7] = { { 0.22f, -0.92f }, { -0.52f, 0.12f }, { -0.02f, 0.12f }, { -0.22f, 0.92f },
                    { 0.56f, -0.18f }, { 0.06f, -0.18f }, { 0.36f, -0.92f } };
        return poly(p, b, 7) - 0.02f;
    }
    case IC_GHOST: {
        float d = U(circle(p, 0, -0.18f, 0.62f), box(p, 0, 0.28f, 0.62f, 0.46f, 0));
        d = S(d, circle(p, -0.21f, 0.82f, 0.19f));
        d = S(d, circle(p, 0.21f, 0.82f, 0.19f));
        d = S(d, box(p, 0, 0.9f, 0.7f, 0.08f, 0));
        d = S(d, box(p, -0.22f, -0.2f, 0.09f, 0.14f, 0.09f));
        d = S(d, box(p, 0.22f, -0.2f, 0.09f, 0.14f, 0.09f));
        return d;
    }
    case IC_ROCKET: {
        const float k = 1.22f;
        v2 q = rot(V2(p.x / k, p.y / k), 45);
        float body = X(circle(q, -0.55f, 0.1f, 0.85f), circle(q, 0.55f, 0.1f, 0.85f));
        body = X(body, q.y - 0.5f);
        v2 fl[3] = { { -0.28f, 0.1f }, { -0.58f, 0.62f }, { -0.22f, 0.5f } };
        v2 fr[3] = { { 0.28f, 0.1f }, { 0.58f, 0.62f }, { 0.22f, 0.5f } };
        float d = U(body, U(poly(q, fl, 3), poly(q, fr, 3)));
        d = S(d, circle(q, 0, -0.1f, 0.13f));
        v2 fire[3] = { { -0.14f, 0.6f }, { 0.14f, 0.6f }, { 0, 0.95f } };
        return U(d, poly(q, fire, 3)) * k;
    }
    case IC_TROPHY: {
        float cup = X(box(p, 0, -0.42f, 0.48f, 0.4f, 0.06f), circle(p, 0, -0.78f, 0.98f));
        float handles = X(U(ring(p, -0.5f, -0.45f, 0.22f, 0.065f), ring(p, 0.5f, -0.45f, 0.22f, 0.065f)), 0.4f - fabsf(p.x));
        float d = U(cup, handles);
        d = U(d, box(p, 0, 0.22f, 0.08f, 0.2f, 0));
        d = U(d, box(p, 0, 0.5f, 0.3f, 0.09f, 0.03f));
        d = U(d, box(p, 0, 0.72f, 0.46f, 0.1f, 0.03f));
        return d;
    }
    case IC_CROWN: {
        v2 c[7] = { { -0.78f, 0.42f }, { -0.78f, -0.4f }, { -0.4f, 0.02f }, { 0, -0.6f },
                    { 0.4f, 0.02f }, { 0.78f, -0.4f }, { 0.78f, 0.42f } };
        float d = poly(p, c, 7);
        d = U(d, circle(p, -0.78f, -0.5f, 0.12f));
        d = U(d, circle(p, 0, -0.7f, 0.12f));
        d = U(d, circle(p, 0.78f, -0.5f, 0.12f));
        d = U(d, box(p, 0, 0.62f, 0.78f, 0.1f, 0.03f));
        return d;
    }
    case IC_PEOPLE: {
        float back = U(circle(p, 0.36f, -0.36f, 0.22f), box(p, 0.42f, 0.5f, 0.36f, 0.3f, 0.26f));
        float front = U(circle(p, -0.24f, -0.3f, 0.26f), box(p, -0.24f, 0.55f, 0.46f, 0.33f, 0.3f));
        return U(front, S(back, front - 0.09f));
    }
    case IC_SHIELD: {
        v2 s[5] = { { -0.7f, -0.78f }, { 0.7f, -0.78f }, { 0.7f, 0.02f }, { 0, 0.9f }, { -0.7f, 0.02f } };
        float d = poly(p, s, 5) - 0.04f;
        v2 in[5];
        for (int i = 0; i < 5; i++)
            in[i] = V2(s[i].x * 0.68f, s[i].y * 0.68f + 0.02f);
        return S(d, fabsf(poly(p, in, 5)) - 0.045f);
    }
    case IC_PAW: {
        float d = U(circle(p, 0, 0.36f, 0.4f), box(p, 0, 0.52f, 0.46f, 0.3f, 0.28f));
        d = U(d, circle(p, -0.66f, -0.1f, 0.18f));
        d = U(d, circle(p, -0.26f, -0.5f, 0.2f));
        d = U(d, circle(p, 0.26f, -0.5f, 0.2f));
        d = U(d, circle(p, 0.66f, -0.1f, 0.18f));
        return d;
    }
    case IC_GEM: {
        v2 g[5] = { { -0.84f, -0.28f }, { -0.46f, -0.72f }, { 0.46f, -0.72f }, { 0.84f, -0.28f }, { 0, 0.86f } };
        float d = poly(p, g, 5);
        float lines = U(seg(p, -0.84f, -0.28f, 0.84f, -0.28f, 0.035f), seg(p, -0.24f, -0.28f, 0, 0.86f, 0.035f));
        lines = U(lines, seg(p, 0.24f, -0.28f, 0, 0.86f, 0.035f));
        lines = U(lines, U(seg(p, -0.24f, -0.28f, -0.1f, -0.72f, 0.035f), seg(p, 0.24f, -0.28f, 0.1f, -0.72f, 0.035f)));
        return S(d, lines);
    }
    case IC_FLAME: {
        v2 t[3] = { { -0.55f, 0.2f }, { 0.55f, 0.2f }, { 0.08f, -0.95f } };
        float outer = U(circle(p, 0, 0.32f, 0.56f), poly(p, t, 3));
        v2 t2[3] = { { -0.26f, 0.45f }, { 0.26f, 0.45f }, { 0.02f, -0.15f } };
        float inner = U(circle(p, 0, 0.56f, 0.26f), poly(p, t2, 3));
        return S(outer, inner);
    }
    case IC_PLANET: {
        float planet = circle(p, 0, 0, 0.46f);
        v2 q = rot(p, -22);
        float rg = ellipse_ring(q, 0.92f, 0.3f, 0.055f);
        bool back = q.y < 0;
        if (back)
            rg = S(rg, planet - 0.08f);
        else
            planet = S(planet, rg - 0.07f);
        return U(planet, rg);
    }
    case IC_LEAF: {
        v2 q = rot(p, 45);
        float d = X(circle(q, -0.5f, 0, 0.92f), circle(q, 0.5f, 0, 0.92f));
        d = S(d, seg(q, 0, -0.62f, 0, 0.62f, 0.04f));
        d = U(d, seg(q, 0, 0.55f, 0, 0.95f, 0.06f));
        return d;
    }
    case IC_DICE: {
        float d = box(p, 0, 0, 0.78f, 0.78f, 0.18f);
        d = S(d, circle(p, -0.4f, -0.4f, 0.13f));
        d = S(d, circle(p, 0.4f, -0.4f, 0.13f));
        d = S(d, circle(p, 0, 0, 0.13f));
        d = S(d, circle(p, -0.4f, 0.4f, 0.13f));
        d = S(d, circle(p, 0.4f, 0.4f, 0.13f));
        return d;
    }
    case IC_MUSHROOM: {
        float cap = X(circle(p, 0, 0.1f, 0.86f), p.y - 0.1f);
        cap = U(cap, box(p, 0, 0.06f, 0.86f, 0.06f, 0.05f));
        cap = S(cap, circle(p, -0.42f, -0.28f, 0.17f));
        cap = S(cap, circle(p, 0.32f, -0.46f, 0.15f));
        cap = S(cap, circle(p, 0.55f, -0.05f, 0.11f));
        float stem = box(p, 0, 0.5f, 0.3f, 0.34f, 0.14f);
        return U(cap, S(stem, box(p, 0, 0.1f, 1, 0.13f, 0)));
    }
    case IC_MOUNTAIN: {
        v2 m1[3] = { { -0.95f, 0.72f }, { -0.18f, -0.62f }, { 0.58f, 0.72f } };
        v2 m2[3] = { { 0.0f, 0.72f }, { 0.45f, -0.18f }, { 0.95f, 0.72f } };
        float d = U(poly(p, m1, 3), poly(p, m2, 3)) - 0.03f;
        v2 snow[5] = { { -0.42f, -0.2f }, { -0.18f, -0.62f }, { 0.06f, -0.2f }, { -0.08f, -0.28f }, { -0.26f, -0.18f } };
        return S(d, fabsf(poly(p, snow, 5)) - 0.035f);
    }
    case IC_BOOK: {
        v2 l[4] = { { -0.9f, -0.6f }, { -0.05f, -0.48f }, { -0.05f, 0.72f }, { -0.9f, 0.6f } };
        v2 r[4] = { { 0.05f, -0.48f }, { 0.9f, -0.6f }, { 0.9f, 0.6f }, { 0.05f, 0.72f } };
        float d = U(poly(p, l, 4), poly(p, r, 4)) - 0.03f;
        for (int i = 0; i < 3; i++) {
            float y = -0.28f + i * 0.26f;
            d = S(d, seg(p, -0.72f, y, -0.22f, y + 0.07f, 0.03f));
            d = S(d, seg(p, 0.22f, y + 0.07f, 0.72f, y, 0.03f));
        }
        return d;
    }
    case IC_CLOCK: {
        float d = ring(p, 0, 0, 0.76f, 0.1f);
        d = U(d, seg(p, 0, 0, 0, -0.46f, 0.08f));
        d = U(d, seg(p, 0, 0, 0.34f, 0.2f, 0.08f));
        return d;
    }
    case IC_HISTORY: {
        float d = ring(p, 0, 0, 0.72f, 0.09f);
        d = S(d, box(p, -0.72f, -0.22f, 0.2f, 0.16f, 0));
        v2 ah[3] = { { -0.98f, -0.08f }, { -0.46f, -0.08f }, { -0.72f, 0.26f } };
        d = U(d, poly(p, ah, 3));
        d = U(d, seg(p, 0, 0, 0, -0.38f, 0.075f));
        d = U(d, seg(p, 0, 0, 0.28f, 0.18f, 0.075f));
        return d;
    }
    case IC_ALL: {
        float d = box(p, -0.4f, -0.4f, 0.3f, 0.3f, 0.09f);
        d = U(d, box(p, 0.4f, -0.4f, 0.3f, 0.3f, 0.09f));
        d = U(d, box(p, -0.4f, 0.4f, 0.3f, 0.3f, 0.09f));
        d = U(d, box(p, 0.4f, 0.4f, 0.3f, 0.3f, 0.09f));
        return d;
    }
    case IC_FLAG: {
        float d = seg(p, -0.66f, -0.86f, -0.66f, 0.92f, 0.07f);
        float flag = box(p, 0.08f, -0.36f, 0.68f, 0.46f, 0.03f);
        // checkered pattern: every other cell is a hole (true distance to the cell border)
        float u = (p.x + 0.6f) / 0.227f, v = (p.y + 0.82f) / 0.307f;
        int cx = (int)floorf(u), cy = (int)floorf(v);
        float fx = u - cx, fy = v - cy;
        float edge = fminf(fminf(fx, 1 - fx) * 0.227f, fminf(fy, 1 - fy) * 0.307f);
        float hole = ((cx + cy) & 1) ? -edge : edge;
        return U(d, S(flag, hole));
    }
    case IC_SEARCH:
        return U(ring(p, -0.14f, -0.14f, 0.5f, 0.11f), seg(p, 0.24f, 0.24f, 0.74f, 0.74f, 0.13f));
    case IC_GEAR: {
        float d = circle(p, 0, 0, 0.58f);
        for (int i = 0; i < 8; i++) {
            v2 q = rot(p, i * 45.0f);
            d = U(d, box(q, 0, -0.72f, 0.15f, 0.2f, 0.04f));
        }
        return S(d, circle(p, 0, 0, 0.25f));
    }
    case IC_PLAY: {
        v2 t[3] = { { -0.42f, -0.66f }, { 0.72f, 0 }, { -0.42f, 0.66f } };
        return poly(p, t, 3) - 0.06f;
    }
    case IC_USB: {
        float d = box(p, 0, 0.22f, 0.36f, 0.62f, 0.12f);
        d = U(d, box(p, 0, -0.58f, 0.24f, 0.26f, 0.02f));
        d = S(d, box(p, -0.09f, -0.6f, 0.05f, 0.06f, 0));
        d = S(d, box(p, 0.09f, -0.6f, 0.05f, 0.06f, 0));
        return d;
    }
    case IC_CHIP: {
        float d = box(p, 0, 0, 0.56f, 0.56f, 0.08f);
        d = S(d, fabsf(box(p, 0, 0, 0.32f, 0.32f, 0.05f)) - 0.04f);
        for (int i = -1; i <= 1; i++) {
            float o = i * 0.3f;
            d = U(d, box(p, o, -0.75f, 0.06f, 0.14f, 0.02f));
            d = U(d, box(p, o, 0.75f, 0.06f, 0.14f, 0.02f));
            d = U(d, box(p, -0.75f, o, 0.14f, 0.06f, 0.02f));
            d = U(d, box(p, 0.75f, o, 0.14f, 0.06f, 0.02f));
        }
        return d;
    }
    case IC_DISC: {
        float d = S(circle(p, 0, 0, 0.86f), circle(p, 0, 0, 0.17f));
        return S(d, ring(p, 0, 0, 0.34f, 0.035f));
    }
    case IC_CHECK:
        return U(seg(p, -0.62f, 0.02f, -0.16f, 0.48f, 0.13f), seg(p, -0.16f, 0.48f, 0.66f, -0.46f, 0.13f));
    case IC_PLUS:
        return U(box(p, 0, 0, 0.72f, 0.13f, 0.06f), box(p, 0, 0, 0.13f, 0.72f, 0.06f));
    case IC_TRASH: {
        float d = box(p, 0, -0.58f, 0.72f, 0.075f, 0.04f);
        d = U(d, S(box(p, 0, -0.72f, 0.22f, 0.12f, 0.06f), box(p, 0, -0.66f, 0.12f, 0.08f, 0)));
        v2 can[4] = { { -0.58f, -0.44f }, { 0.58f, -0.44f }, { 0.48f, 0.84f }, { -0.48f, 0.84f } };
        float body = poly(p, can, 4) - 0.03f;
        body = S(body, box(p, -0.22f, 0.2f, 0.05f, 0.42f, 0.05f));
        body = S(body, box(p, 0.22f, 0.2f, 0.05f, 0.42f, 0.05f));
        return U(d, body);
    }
    case IC_PENCIL: {
        v2 q = rot(p, -45);
        float d = box(q, 0, -0.1f, 0.2f, 0.58f, 0.03f);
        v2 tip[3] = { { -0.2f, 0.52f }, { 0.2f, 0.52f }, { 0, 0.9f } };
        d = U(d, poly(q, tip, 3));
        d = S(d, box(q, 0, -0.42f, 0.3f, 0.03f, 0));
        return d;
    }
    case IC_EYE:
    case IC_EYE_OFF: {
        float eye = X(circle(p, 0, 0.62f, 1.0f), circle(p, 0, -0.62f, 1.0f));
        float d = S(eye, circle(p, 0, 0, 0.4f));
        d = U(d, circle(p, 0, 0, 0.24f));
        if (id == IC_EYE_OFF) {
            d = S(d, seg(p, -0.72f, -0.72f, 0.72f, 0.72f, 0.16f));
            d = U(d, seg(p, -0.72f, -0.72f, 0.72f, 0.72f, 0.07f));
        }
        return d;
    }
    case IC_INFO: {
        float d = circle(p, 0, 0, 0.86f);
        d = S(d, circle(p, 0, -0.42f, 0.11f));
        d = S(d, box(p, 0, 0.18f, 0.09f, 0.36f, 0.03f));
        return d;
    }
    case IC_DOWNLOAD: {
        float d = box(p, 0, -0.3f, 0.12f, 0.46f, 0.03f);
        v2 ah[3] = { { -0.46f, 0.0f }, { 0.46f, 0.0f }, { 0, 0.46f } };
        d = U(d, poly(p, ah, 3));
        d = U(d, box(p, 0, 0.78f, 0.78f, 0.08f, 0.04f));
        return d;
    }
    case IC_IMAGE: {
        float frame = fabsf(box(p, 0, 0, 0.84f, 0.66f, 0.1f)) - 0.065f;
        v2 m[3] = { { -0.66f, 0.5f }, { -0.15f, -0.12f }, { 0.3f, 0.5f } };
        v2 m2[3] = { { 0.0f, 0.5f }, { 0.34f, 0.08f }, { 0.68f, 0.5f } };
        float d = U(frame, U(poly(p, m, 3), poly(p, m2, 3)));
        return U(d, circle(p, 0.4f, -0.3f, 0.13f));
    }
    case IC_TAG: {
        v2 t[5] = { { -0.84f, -0.84f }, { 0.0f, -0.84f }, { 0.86f, 0.02f }, { 0.02f, 0.86f }, { -0.84f, 0.0f } };
        float d = poly(p, t, 5) - 0.04f;
        return S(d, circle(p, -0.44f, -0.44f, 0.14f));
    }
    case IC_SORT: {
        float d = box(p, -0.1f, -0.55f, 0.72f, 0.1f, 0.05f);
        d = U(d, box(p, -0.28f, 0, 0.54f, 0.1f, 0.05f));
        d = U(d, box(p, -0.46f, 0.55f, 0.36f, 0.1f, 0.05f));
        return d;
    }
    case IC_PALETTE: {
        float d = S(circle(p, 0, 0, 0.84f), circle(p, 0.38f, 0.42f, 0.24f));
        d = S(d, circle(p, -0.36f, -0.3f, 0.13f));
        d = S(d, circle(p, 0.08f, -0.48f, 0.13f));
        d = S(d, circle(p, 0.48f, -0.18f, 0.13f));
        d = S(d, circle(p, -0.44f, 0.18f, 0.13f));
        return d;
    }
    case IC_SOUND: {
        v2 sp[6] = { { -0.86f, -0.24f }, { -0.5f, -0.24f }, { -0.06f, -0.66f }, { -0.06f, 0.66f },
                     { -0.5f, 0.24f }, { -0.86f, 0.24f } };
        float d = poly(p, sp, 6) - 0.03f;
        float w1 = X(ring(p, -0.06f, 0, 0.36f, 0.065f), 0.1f - p.x);
        float w2 = X(ring(p, -0.06f, 0, 0.66f, 0.065f), 0.1f - p.x);
        return U(d, U(w1, w2));
    }
    case IC_MOON:
        return S(circle(p, 0, 0, 0.78f), circle(p, 0.42f, -0.3f, 0.62f));
    case IC_CAROUSEL: {
        float d = box(p, 0, 0, 0.36f, 0.6f, 0.08f);
        d = U(d, box(p, -0.72f, 0, 0.18f, 0.42f, 0.06f));
        d = U(d, box(p, 0.72f, 0, 0.18f, 0.42f, 0.06f));
        return d;
    }
    case IC_REFRESH: {
        float d = ring(p, 0, 0, 0.62f, 0.1f);
        d = S(d, X(p.x - 0.05f, -p.y));          // gap in the upper right
        v2 ah[3] = { { 0.3f, -0.95f }, { 0.3f, -0.3f }, { 0.78f, -0.62f } };
        return U(d, poly(p, ah, 3));
    }
    case IC_POWER: {
        float d = ring(p, 0, 0.06f, 0.66f, 0.1f);
        d = S(d, X(fabsf(p.x) - 0.26f, p.y + 0.2f));
        return U(d, seg(p, 0, -0.86f, 0, -0.1f, 0.1f));
    }
    case IC_ARROW_UP:
    case IC_ARROW_DOWN: {
        float s = id == IC_ARROW_UP ? 1.0f : -1.0f;
        v2 q = V2(p.x, p.y * s);
        v2 ah[3] = { { -0.6f, 0.0f }, { 0, -0.66f }, { 0.6f, 0.0f } };
        return U(poly(q, ah, 3) - 0.04f, box(q, 0, 0.3f, 0.14f, 0.5f, 0.04f));
    }
    case IC_BACK: {
        v2 ah[3] = { { -0.1f, -0.62f }, { -0.78f, 0.0f }, { -0.1f, 0.62f } };
        return U(poly(p, ah, 3) - 0.03f, box(p, 0.22f, 0, 0.5f, 0.14f, 0.05f));
    }
    }
    return 1.0f;
}

static const char *k_names[IC_CATEGORY_COUNT] = {
    "Mando", "Disparos", "Aventura", "Carreras", "Puzles", "Estrella", "Corazón", "Música",
    "Deportes", "Acción", "Terror", "Ciencia ficción", "Trofeo", "Corona", "Multijugador", "Rol",
    "Familia", "Joya", "Fuego", "Espacio", "Relax", "Fiesta", "Plataformas", "Exploración",
    "Historia", "Reloj", "Colección", "Competición",
};

const char *icon_name(int id)
{
    return id >= 0 && id < IC_CATEGORY_COUNT ? k_names[id] : "";
}

static int build_atlas(void *arg)
{
    (void)arg;
    uint8_t *px = s_px;
    int aw = s_aw, ah = s_ah;
    const float inv = 2.0f / ICON_PX;
    // The distance at a block's center bounds every pixel of the block, so
    // blocks far from the outline are filled without evaluating each pixel.
    const int B = 8;
    const float reach = (B * 0.7072f + 1.0f) * inv;
    for (int i = 0; i < aw * ah; i++)
        px[i * 4 + 0] = px[i * 4 + 1] = px[i * 4 + 2] = 255;
    uint32_t coop = SDL_GetTicks(), t0 = coop;
    for (int id = 0; id < IC_COUNT; id++) {
        int ox = (id % ATLAS_COLS) * ICON_PX, oy = (id / ATLAS_COLS) * ICON_PX;
        for (int by = 0; by < ICON_PX; by += B) {
            plat_coop(&coop);
            for (int bx = 0; bx < ICON_PX; bx += B) {
                v2 c = V2((bx + B * 0.5f) * inv - 1.0f, (by + B * 0.5f) * inv - 1.0f);
                float dc = icon_sdf(id, c);
                if (dc > reach || dc < -reach) {
                    uint8_t a = dc < 0 ? 255 : 0;
                    for (int y = by; y < by + B; y++) {
                        uint8_t *row = px + ((size_t)(oy + y) * aw + ox + bx) * 4;
                        for (int x = 0; x < B; x++)
                            row[x * 4 + 3] = a;
                    }
                    continue;
                }
                for (int y = by; y < by + B; y++) {
                    uint8_t *row = px + ((size_t)(oy + y) * aw + ox) * 4;
                    for (int x = bx; x < bx + B; x++) {
                        v2 p = V2((x + 0.5f) * inv - 1.0f, (y + 0.5f) * inv - 1.0f);
                        float d = icon_sdf(id, p);
                        float a = clampf(0.5f - d / inv, 0, 1);
                        row[x * 4 + 3] = (uint8_t)(a * 255 + 0.5f);
                    }
                }
            }
        }
    }
    SDL_AtomicSet(&s_ready, 1);
    log_msg("iconos listos en %u ms", SDL_GetTicks() - t0);
    return 0;
}

// The atlas is computed on a worker thread so the first frames appear at
// once; icons show up as soon as it is uploaded (a few frames later).
bool icons_init(SDL_Renderer *r)
{
    int rows = (IC_COUNT + ATLAS_COLS - 1) / ATLAS_COLS;
    s_ren = r;
    s_aw = ATLAS_COLS * ICON_PX;
    s_ah = rows * ICON_PX;
    s_px = calloc((size_t)s_aw * s_ah, 4);
    if (!s_px)
        return false;
    s_thread = plat_thread_start(build_atlas, NULL, "iconos", 64 * 1024, PLAT_CORE_WORK);
    if (!s_thread)
        build_atlas(NULL);
    return true;
}

static void icons_poll(void)
{
    if (s_atlas || !s_px || !SDL_AtomicGet(&s_ready))
        return;
    if (s_thread) {
        plat_thread_join(s_thread);
        s_thread = NULL;
    }
    s_atlas = SDL_CreateTexture(s_ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, s_aw, s_ah);
    if (s_atlas) {
        SDL_UpdateTexture(s_atlas, NULL, s_px, s_aw * 4);
        SDL_SetTextureBlendMode(s_atlas, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(s_atlas, SDL_ScaleModeLinear);
    }
    free(s_px);
    s_px = NULL;
}

void icons_shutdown(void)
{
    if (s_thread) {
        plat_thread_join(s_thread);
        s_thread = NULL;
    }
    free(s_px);
    s_px = NULL;
    if (s_atlas)
        SDL_DestroyTexture(s_atlas);
    s_atlas = NULL;
}

SDL_Texture *icon_tex(int id)
{
    (void)id;
    return s_atlas;
}

void icon_draw(int id, float x, float y, float size, Color c)
{
    icons_poll();
    if (!s_atlas || id < 0 || id >= IC_COUNT)
        return;
    SDL_Rect src = { (id % ATLAS_COLS) * ICON_PX, (id / ATLAS_COLS) * ICON_PX, ICON_PX, ICON_PX };
    gfx_image(s_atlas, &src, x, y, size, size, 0, c);
}

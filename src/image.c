// Carga y procesado de imágenes con stb_image.
#include "image.h"

#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS // the RPX format has no TLS relocations
#define STBI_MAX_DIMENSIONS 8192
#include "stb_image.h"

bool img_alloc(Image *im, int w, int h)
{
    im->w = w;
    im->h = h;
    im->px = malloc((size_t)w * h * 4);
    return im->px != NULL;
}

void img_free(Image *im)
{
    if (im->px)
        stbi_image_free(im->px); // same allocator as malloc
    im->px = NULL;
    im->w = im->h = 0;
}

bool img_load_mem(const uint8_t *data, size_t size, Image *out)
{
    int w, h, comp;
    memset(out, 0, sizeof(*out));
    uint8_t *px = stbi_load_from_memory(data, (int)size, &w, &h, &comp, 4);
    if (!px)
        return false;
    out->w = w;
    out->h = h;
    out->px = px;
    return true;
}

bool img_load_file(const char *path, Image *out)
{
    size_t size;
    memset(out, 0, sizeof(*out));
    uint8_t *data = file_read(path, &size);
    if (!data)
        return false;
    bool ok = img_load_mem(data, size, out);
    free(data);
    return ok;
}

static Image halve(const Image *src)
{
    Image dst;
    int w = src->w / 2, h = src->h / 2;
    if (!img_alloc(&dst, w, h))
        return dst;
    for (int y = 0; y < h; y++) {
        const uint8_t *r0 = src->px + (size_t)(y * 2) * src->w * 4;
        const uint8_t *r1 = r0 + (size_t)src->w * 4;
        uint8_t *o = dst.px + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            for (int c = 0; c < 4; c++)
                o[c] = (uint8_t)((r0[c] + r0[c + 4] + r1[c] + r1[c + 4] + 2) >> 2);
            r0 += 8;
            r1 += 8;
            o += 4;
        }
    }
    return dst;
}

static Image bilinear(const Image *src, int w, int h)
{
    Image dst;
    if (!img_alloc(&dst, w, h))
        return dst;
    float sx = (float)src->w / w, sy = (float)src->h / h;
    for (int y = 0; y < h; y++) {
        float fy = (y + 0.5f) * sy - 0.5f;
        int y0 = (int)floorf(fy);
        float ty = fy - y0;
        int y1 = y0 + 1;
        if (y0 < 0) y0 = 0;
        if (y1 >= src->h) y1 = src->h - 1;
        if (y0 >= src->h) y0 = src->h - 1;
        for (int x = 0; x < w; x++) {
            float fx = (x + 0.5f) * sx - 0.5f;
            int x0 = (int)floorf(fx);
            float tx = fx - x0;
            int x1 = x0 + 1;
            if (x0 < 0) x0 = 0;
            if (x1 >= src->w) x1 = src->w - 1;
            if (x0 >= src->w) x0 = src->w - 1;
            const uint8_t *p00 = src->px + ((size_t)y0 * src->w + x0) * 4;
            const uint8_t *p10 = src->px + ((size_t)y0 * src->w + x1) * 4;
            const uint8_t *p01 = src->px + ((size_t)y1 * src->w + x0) * 4;
            const uint8_t *p11 = src->px + ((size_t)y1 * src->w + x1) * 4;
            uint8_t *o = dst.px + ((size_t)y * w + x) * 4;
            for (int c = 0; c < 4; c++) {
                float a = p00[c] + (p10[c] - p00[c]) * tx;
                float b = p01[c] + (p11[c] - p01[c]) * tx;
                o[c] = (uint8_t)(a + (b - a) * ty + 0.5f);
            }
        }
    }
    return dst;
}

Image img_resize(const Image *src, int w, int h)
{
    Image cur = *src;
    bool owned = false;
    while (cur.w >= w * 2 && cur.h >= h * 2 && cur.w >= 2 && cur.h >= 2) {
        Image next = halve(&cur);
        if (owned)
            img_free(&cur);
        if (!next.px) {
            Image none = { 0, 0, NULL };
            return none;
        }
        cur = next;
        owned = true;
    }
    if (cur.w == w && cur.h == h) {
        if (owned)
            return cur;
        Image copy;
        if (img_alloc(&copy, w, h))
            memcpy(copy.px, src->px, (size_t)w * h * 4);
        return copy;
    }
    Image out = bilinear(&cur, w, h);
    if (owned)
        img_free(&cur);
    return out;
}

Image img_crop_fill(const Image *src, int w, int h)
{
    // pick the largest centered region with the target aspect ratio
    float ta = (float)w / h, sa = (float)src->w / src->h;
    int cw = src->w, ch = src->h, cx = 0, cy = 0;
    if (sa > ta) {
        cw = (int)(src->h * ta);
        cx = (src->w - cw) / 2;
    } else {
        ch = (int)(src->w / ta);
        cy = (src->h - ch) / 2;
    }
    Image crop;
    if (!img_alloc(&crop, cw, ch)) {
        Image none = { 0, 0, NULL };
        return none;
    }
    for (int y = 0; y < ch; y++)
        memcpy(crop.px + (size_t)y * cw * 4, src->px + ((size_t)(y + cy) * src->w + cx) * 4,
               (size_t)cw * 4);
    Image out = img_resize(&crop, w, h);
    img_free(&crop);
    return out;
}

static void blur_line(uint8_t *line, int count, int stride, int radius, uint8_t *tmp)
{
    int win = radius * 2 + 1;
    for (int c = 0; c < 4; c++) {
        int sum = 0;
        for (int i = -radius; i <= radius; i++) {
            int k = i < 0 ? 0 : i >= count ? count - 1 : i;
            sum += line[k * stride + c];
        }
        for (int i = 0; i < count; i++) {
            tmp[i] = (uint8_t)(sum / win);
            int add = i + radius + 1, sub = i - radius;
            if (add >= count) add = count - 1;
            if (sub < 0) sub = 0;
            sum += line[add * stride + c] - line[sub * stride + c];
        }
        for (int i = 0; i < count; i++)
            line[i * stride + c] = tmp[i];
    }
}

void img_blur(Image *im, int radius, int passes)
{
    if (!im->px || radius < 1)
        return;
    int maxdim = im->w > im->h ? im->w : im->h;
    uint8_t *tmp = malloc((size_t)maxdim);
    if (!tmp)
        return;
    for (int p = 0; p < passes; p++) {
        for (int y = 0; y < im->h; y++)
            blur_line(im->px + (size_t)y * im->w * 4, im->w, 4, radius, tmp);
        for (int x = 0; x < im->w; x++)
            blur_line(im->px + (size_t)x * 4, im->h, im->w * 4, radius, tmp);
    }
    free(tmp);
}

bool img_is_mostly_opaque(const Image *im)
{
    size_t n = (size_t)im->w * im->h, opaque = 0;
    for (size_t i = 0; i < n; i += 7)
        if (im->px[i * 4 + 3] > 200)
            opaque++;
    return opaque * 7 > n * 9 / 10;
}

void img_average(const Image *im, uint8_t rgb_out[3])
{
    double acc[3] = { 0, 0, 0 }, wsum = 0, plain[3] = { 0, 0, 0 };
    size_t n = (size_t)im->w * im->h, cnt = 0;
    for (size_t i = 0; i < n; i += 3) {
        const uint8_t *p = im->px + i * 4;
        int mx = p[0] > p[1] ? (p[0] > p[2] ? p[0] : p[2]) : (p[1] > p[2] ? p[1] : p[2]);
        int mn = p[0] < p[1] ? (p[0] < p[2] ? p[0] : p[2]) : (p[1] < p[2] ? p[1] : p[2]);
        double sat = mx ? (double)(mx - mn) / mx : 0, val = mx / 255.0;
        double w = sat * sat * val;
        for (int c = 0; c < 3; c++) {
            acc[c] += p[c] * w;
            plain[c] += p[c];
        }
        wsum += w;
        cnt++;
    }
    for (int c = 0; c < 3; c++) {
        double v = wsum > cnt * 0.02 ? acc[c] / wsum : (cnt ? plain[c] / cnt : 128);
        rgb_out[c] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
}

SDL_Texture *img_to_texture(SDL_Renderer *r, const Image *im)
{
    if (!im->px || im->w <= 0 || im->h <= 0)
        return NULL;
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, im->w, im->h);
    if (!t)
        return NULL;
    SDL_UpdateTexture(t, NULL, im->px, im->w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    return t;
}

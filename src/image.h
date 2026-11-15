// Imágenes RGBA en memoria: carga (PNG/JPG/TGA), escalado y desenfoque.
#pragma once

#include "common.h"

typedef struct Image {
    int w, h;
    uint8_t *px; // RGBA, w*h*4 bytes
} Image;

bool  img_load_file(const char *path, Image *out);
bool  img_load_mem(const uint8_t *data, size_t size, Image *out);
bool  img_alloc(Image *im, int w, int h);
void  img_free(Image *im);
Image img_resize(const Image *src, int w, int h);           // high quality downscale
Image img_crop_fill(const Image *src, int w, int h);        // scale + crop to fill w x h
void  img_blur(Image *im, int radius, int passes);           // box blur, in place
bool  img_is_mostly_opaque(const Image *im);
void  img_average(const Image *im, uint8_t rgb_out[3]);     // average color (for accents)
SDL_Texture *img_to_texture(SDL_Renderer *r, const Image *im);

// Primitivas de dibujo suavizadas (antialias por geometría) sobre SDL_Renderer.
#pragma once

#include "common.h"

extern SDL_Renderer *g_ren;

void gfx_init(SDL_Renderer *r);

// Clip / alpha stack: every primitive is multiplied by the current global alpha.
void  gfx_push_alpha(float a);
void  gfx_pop_alpha(void);
float gfx_alpha(void);
void  gfx_clip(int x, int y, int w, int h);
void  gfx_unclip(void);

void gfx_rect(float x, float y, float w, float h, Color c);
void gfx_rect_vgrad(float x, float y, float w, float h, Color top, Color bottom);
void gfx_rect_hgrad(float x, float y, float w, float h, Color left, Color right);
void gfx_rrect(float x, float y, float w, float h, float r, Color c);
void gfx_rrect_vgrad(float x, float y, float w, float h, float r, Color top, Color bottom);
void gfx_rrect_outline(float x, float y, float w, float h, float r, float thick, Color c);
void gfx_shadow(float x, float y, float w, float h, float r, float blur, Color c);
void gfx_glow(float x, float y, float w, float h, float r, float size, Color c);
void gfx_circle(float cx, float cy, float rad, Color c);
void gfx_ring(float cx, float cy, float rad, float thick, Color c);
void gfx_line(float x1, float y1, float x2, float y2, float thick, Color c);

// Textured rounded rectangle. src = texture sub-rect in pixels (NULL = whole texture).
void gfx_image(SDL_Texture *t, const SDL_Rect *src, float x, float y, float w, float h, float r, Color tint);
// Fit (contain) or fill (cover) an image into a box, rounded corners.
void gfx_image_cover(SDL_Texture *t, float x, float y, float w, float h, float r, Color tint);
void gfx_image_contain(SDL_Texture *t, float x, float y, float w, float h, float r, Color tint);

// Raw batched geometry (used by the text renderer)
void gfx_geometry(SDL_Texture *t, const SDL_Vertex *v, int nv, const int *idx, int ni);

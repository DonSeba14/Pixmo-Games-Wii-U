// Texto con la fuente del sistema Wii U (stb_truetype + atlas en GPU).
#pragma once

#include "common.h"

enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

bool  font_init(const void *ttf, size_t size);
void  font_shutdown(void);
void  font_prewarm(const int *sizes, int count);

float text_width(int size, const char *s);
float text_line_height(int size);
// Draws one line; (x, y) is the top-left of the line box. Returns the width.
float text_draw(int size, const char *s, float x, float y, Color c, int align);
// Draws with a soft drop shadow (for text over images).
float text_draw_shadow(int size, const char *s, float x, float y, Color c, Color shadow, int align);
// Shortens s with "…" so it fits in maxw.
void  text_fit(int size, const char *s, float maxw, char *out, size_t n);
// Word-wraps into at most maxlines lines (last one ellipsized). Returns line count.
int   text_wrap(int size, const char *s, float maxw, int maxlines, char lines[][256]);
// While set, every vertex text_draw emits goes through fn first (text laid out
// flat, then mapped, e.g. onto the spine of a 3D case). NULL = normal drawing.
void  text_set_transform(void (*fn)(float *x, float *y, void *ud), void *ud);

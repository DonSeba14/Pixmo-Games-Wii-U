// Pixmo Games - frontend de juegos para Wii U.
// Tipos y utilidades compartidas por todos los módulos.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>

#include <SDL.h>

#define SCREEN_W 1280
#define SCREEN_H 720

#define APP_NAME    "Pixmo Games"
#define APP_DIR     "pixmogames"  // data folder: SD:/wiiu/apps/pixmogames
#define APP_DIR_OLD "ludoteca"    // where the app kept its data before it was renamed
#define APP_VERSION "1.6.4"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct Color {
    uint8_t r, g, b, a;
} Color;

static inline Color rgb(uint32_t hex)
{
    Color c = { (uint8_t)(hex >> 16), (uint8_t)(hex >> 8), (uint8_t)hex, 255 };
    return c;
}

static inline Color rgba(uint32_t hex, float a)
{
    Color c = rgb(hex);
    c.a = (uint8_t)(a < 0 ? 0 : a > 1 ? 255 : a * 255.0f + 0.5f);
    return c;
}

static inline Color col_alpha(Color c, float a)
{
    float v = c.a * a;
    c.a = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
    return c;
}

static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static inline float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}

static inline Color col_mix(Color a, Color b, float t)
{
    Color c = {
        (uint8_t)lerpf(a.r, b.r, t), (uint8_t)lerpf(a.g, b.g, t),
        (uint8_t)lerpf(a.b, b.b, t), (uint8_t)lerpf(a.a, b.a, t),
    };
    return c;
}

// Exponential approach: frame-rate independent smoothing toward a target.
static inline float approach(float cur, float target, float rate, float dt)
{
    return target + (cur - target) * expf(-rate * dt);
}

static inline float ease_out_cubic(float t)
{
    t = clampf(t, 0, 1) - 1;
    return t * t * t + 1;
}

static inline float ease_in_out(float t)
{
    t = clampf(t, 0, 1);
    return t * t * (3 - 2 * t);
}

static inline float ease_out_back(float t)
{
    const float c1 = 1.70158f, c3 = c1 + 1;
    t = clampf(t, 0, 1) - 1;
    return 1 + c3 * t * t * t + c1 * t * t;
}

// util.c
void  str_copy(char *dst, size_t n, const char *src);
char *str_trim(char *s);
int   utf8_decode(const char **s);            // returns codepoint, advances; 0 at end
int   utf8_encode(int cp, char *out);         // returns byte count
void  str_fold(const char *src, char *dst, size_t n); // lowercase, accents removed (for search/sort)
bool  str_contains_folded(const char *hay_folded, const char *needle_folded);
uint8_t *file_read(const char *path, size_t *size);
bool  file_write_atomic(const char *path, const void *data, size_t size);
bool  file_exists(const char *path);
void  dir_make(const char *path);
int64_t time_now(void);
void  format_relative_time(int64_t t, char *out, size_t n);
void  format_date(int64_t t, char *out, size_t n);
struct tm;
void  format_clock_date(const struct tm *tm, char *out, size_t n); // "sáb 14 nov" / "Sat Nov 14"
uint32_t hash_str(const char *s);
void  log_msg(const char *fmt, ...);
void  log_file_start(const char *dir);
void  log_file_stop(void);

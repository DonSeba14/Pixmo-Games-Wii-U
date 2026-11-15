// Estado compartido entre los módulos de la interfaz.
#pragma once

#include "audio.h"
#include "font.h"
#include "gfx.h"
#include "i18n.h"
#include "icons.h"
#include "library.h"
#include "ui.h"
#include "brand.h"

typedef struct Theme {
    Color bg_top, bg_bottom, stripe;
    Color bar_top, bar_bottom, bar_line;
    Color panel, panel_head, row_line;
    Color text, text_dim, text_faint;
    Color accent, accent_dark, accent_light, accent_text;
    Color sel_fill, sel_line, shadow, dim;
    Color glyph_bg, glyph_fg, tile_bg, chip_bg;
    float art_alpha, veil;
} Theme;
extern Theme T;

enum { TAB_ALL, TAB_RECENT, TAB_FAV, TAB_CAT };

typedef struct Tab {
    int kind;
    int cat;          // category id for TAB_CAT
    char name[64];
    int icon;
    uint32_t color;
} Tab;

enum { FOCUS_GAMES, FOCUS_TABS };

enum {
    HIT_NONE, HIT_TAB, HIT_GAME, HIT_BUTTON, HIT_PANEL_ITEM, HIT_PANEL_BG,
    HIT_PLAY, HIT_SEARCH_CLEAR, HIT_PANEL_BUTTON, HIT_HIDE,
};

typedef struct Hit {
    float x, y, w, h;
    int kind, param;
} Hit;

enum {
    TT_NONE, TT_SEARCH, TT_NEW_CAT, TT_NEW_CAT_ASSIGN, TT_RENAME_CAT,
};

typedef struct UIState {
    float time;
    Tab tabs[64];
    int ntabs, tab;
    int *view;
    int nview, capview;
    int sel;
    uint64_t sel_tid;
    uint64_t tab_sel_tid[64];
    int focus;
    float scroll;               // carousel position (items) or grid scroll (rows)
    float tab_ind_x, tab_ind_w, tabs_scroll;
    bool tab_ind_init;

    int hero_game, hero_prev;   // game indices (-1 = none)
    float hero_t;
    int hero_dir;
    int bg_game, bg_prev;
    float bg_t;
    float art_fade;             // fade-in of the hero art when it finishes loading

    char search[64], search_fold[64];

    float banner_t;
    int banner_tab;

    char toast[128];
    float toast_t;
    int toast_icon;

    bool launching, launch_fired, launch_ready;
    float launch_t;
    int launch_game;
    uint64_t launch_tid;
    int launch_wii_mode;        // WII_SHOW_* for Wii games (they boot into Wii mode)

    bool quit, background;

    bool held[BTN_COUNT];
    float hold_t[BTN_COUNT], repeat_t[BTN_COUNT];

    bool touching, dragging;
    float tx0, ty0, tx, ty, drag_scroll0, fling_v, last_tx, last_ty;
    Hit touch_hit;
    Hit hits[200];
    int nhits;

    int text_target, text_ctx;
    float theme_t;
    float intro_t;
    bool user_moved;
} UIState;
extern UIState ui;

// ---- panels
enum {
    P_OPTIONS, P_GAME, P_ASSIGN, P_CATMAN, P_CATEDIT, P_ICONPICK, P_COLORPICK,
    P_LOGOPICK, P_CONFIRM, P_DETAILS, P_ABOUT, P_HELP, P_WIIMODE, P_HIDDEN,
};
enum { PI_ACTION, PI_TOGGLE, PI_CHOICE, PI_CHECK, PI_INFO, PI_GRID };

typedef struct PItem {
    int kind, id;
    int icon;
    uint32_t color;           // icon/badge color (0 = theme text color)
    bool badge;               // draw icon as a category badge
    SDL_Texture *logo;
    char label[128];
    char value[128];
    bool on, disabled;
    float anim;
} PItem;

typedef struct Panel {
    int type, ctx, ctx2;
    char title[128];
    char subtitle[160];
    int icon;
    uint32_t icon_color;
    PItem items[80];
    int n, sel;
    float scroll, anim;
    bool closing;
    int cols;                 // > 0 for grid pickers
    float w;
} Panel;

extern Panel panels[8];
extern int npanels;

// ui_home.c
void   views_rebuild(void);
extern uint32_t g_view_built_at; // SDL ticks of the last views_rebuild (the list may still be settling)
void   tabs_rebuild(void);
Game  *sel_game(void);
int    sel_game_index(void);
void   ui_toast(const char *msg, int icon);
void   hit_add(float x, float y, float w, float h, int kind, int param);
void   start_launch(int game_index);        // Wii games first ask how to play (P_WIIMODE)
void   start_launch_wii(int game_index, int wii_mode);
// hides the game (or shows it again), with a toast that says how to get it back
void   game_toggle_hidden(Game *g);
void   request_text(int target, int ctx, const char *hint, const char *initial, const char *ok);
void   home_action(Button b);
// what to say when the current tab shows no games
void   home_empty_texts(const char **title, const char **sub, int *icon);

// ui_cine.c: the "Cine" view (full-screen art, 3D case, game details)
void cine_background(void);
void cine_render(void);
void cine_top_bar(void);
void cine_case_rect(Game *g, float *x, float *y, float *w, float *h);

// ui_panels.c
void panel_open(int type, int ctx);
void panel_close_top(void);
void panels_close_all(void);
bool panels_input(Button b);
void panels_update(float dt);
void panels_render(void);
void panels_rebuild(void);
void panel_touch_item(int idx);
void panel_touch_outside(void);
void panel_text_result(int target, int ctx, const char *text);

// ui_draw.c
void  theme_update(float dt);
Color theme_color_of(uint32_t hex);
float draw_glyph(Button b, float x, float cy, float size);
float glyph_width(Button b, float size);
float draw_hints(const Button *btns, const char *const *labels, int n, float x, float cy, int align);
void  draw_badge(int icon, uint32_t color, SDL_Texture *logo, float x, float y, float size);
void  draw_tab_badge(const Tab *t, float x, float y, float size);
void  draw_cover(Game *g, float x, float y, float w, float h, float alpha);
void  draw_toggle(float x, float y, bool on, float alpha);
void  draw_check(float x, float y, float size, bool on);
void  draw_spinner(float cx, float cy, float r, Color c);
void  draw_tile(Game *g, float x, float y, float size, float sel, float alpha);
void  draw_sel_frame(float x, float y, float w, float h, float r, float amount);
void  draw_background(void);
void  draw_top_bar(void);
void  draw_chip(float x, float y, const char *text, int icon, Color bg, Color fg, float *out_w);
float chip_width(const char *text, int icon);
void  draw_button(float x, float y, float w, float h, Button glyph, const char *label, bool primary,
                  float focus, int hit_param);
// round button with just an icon (same look as the white buttons); touch -> hit_kind/hit_param
void  draw_round_button(float x, float y, float size, int icon, int hit_kind, int hit_param);
Color lighten(Color c, float t);
Color darken(Color c, float t);

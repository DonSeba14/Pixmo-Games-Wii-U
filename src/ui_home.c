// Pantalla principal: pestañas de categorías, vitrina (carátula + datos +
// carrusel) o cuadrícula, búsqueda, lanzamiento de juegos y entrada.
#include "ui_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

UIState ui;

#define GRID_COLS 5
#define CAROUSEL_SPACING 132.0f

// ------------------------------------------------------------ helpers

void hit_add(float x, float y, float w, float h, int kind, int param)
{
    if (ui.nhits < (int)(sizeof(ui.hits) / sizeof(ui.hits[0]))) {
        Hit h2 = { x, y, w, h, kind, param };
        ui.hits[ui.nhits++] = h2;
    }
}

static const Hit *hit_at(float x, float y)
{
    for (int i = ui.nhits - 1; i >= 0; i--) {
        const Hit *h = &ui.hits[i];
        if (x >= h->x && x < h->x + h->w && y >= h->y && y < h->y + h->h)
            return h;
    }
    return NULL;
}

void ui_toast(const char *msg, int icon)
{
    str_copy(ui.toast, sizeof(ui.toast), msg);
    ui.toast_t = 0.0001f;
    ui.toast_icon = icon;
}

int sel_game_index(void)
{
    if (ui.nview <= 0 || ui.sel < 0 || ui.sel >= ui.nview)
        return -1;
    return ui.view[ui.sel];
}

Game *sel_game(void)
{
    int i = sel_game_index();
    return i >= 0 ? &L.games[i] : NULL;
}

void request_text(int target, int ctx, const char *hint, const char *initial, const char *ok)
{
    ui.text_target = target;
    ui.text_ctx = ctx;
    sys_text_input(tr(hint), initial, tr(ok));
}

// ------------------------------------------------------------ tabs & views

void tabs_rebuild(void)
{
    int old_kind = ui.ntabs ? ui.tabs[ui.tab].kind : -1;
    int old_cat = ui.ntabs ? ui.tabs[ui.tab].cat : -1;
    ui.ntabs = 0;
    Tab *t;
    t = &ui.tabs[ui.ntabs++];
    *t = (Tab){ TAB_ALL, -1, "Todos", IC_ALL, 0x009AC7 };
    t = &ui.tabs[ui.ntabs++];
    *t = (Tab){ TAB_RECENT, -1, "Recientes", IC_CLOCK, 0x1FA38F };
    t = &ui.tabs[ui.ntabs++];
    *t = (Tab){ TAB_FAV, -1, "Favoritos", IC_STAR, 0xF5A300 };
    for (int i = 0; i < L.ncats && ui.ntabs < 64; i++) {
        t = &ui.tabs[ui.ntabs++];
        t->kind = TAB_CAT;
        t->cat = L.cats[i].id;
        str_copy(t->name, sizeof(t->name), L.cats[i].name);
        t->icon = L.cats[i].icon;
        t->color = L.cats[i].color;
    }
    int found = -1;
    for (int i = 0; i < ui.ntabs; i++)
        if (ui.tabs[i].kind == old_kind && ui.tabs[i].cat == old_cat)
            found = i;
    if (found >= 0)
        ui.tab = found;
    if (ui.tab >= ui.ntabs)
        ui.tab = ui.ntabs - 1;
    if (ui.tab < 0)
        ui.tab = 0;
}

static int cmp_name(const void *a, const void *b)
{
    const Game *x = &L.games[*(const int *)a], *y = &L.games[*(const int *)b];
    int c = strcmp(x->sortkey, y->sortkey);
    return c ? c : (x->tid < y->tid ? -1 : 1);
}

static int cmp_recent(const void *a, const void *b)
{
    const Game *x = &L.games[*(const int *)a], *y = &L.games[*(const int *)b];
    int64_t tx = game_last_time(x), ty = game_last_time(y);
    if (tx != ty)
        return tx > ty ? -1 : 1;
    return cmp_name(a, b);
}

static int cmp_played(const void *a, const void *b)
{
    const Game *x = &L.games[*(const int *)a], *y = &L.games[*(const int *)b];
    if (x->play_count != y->play_count)
        return x->play_count > y->play_count ? -1 : 1;
    return cmp_recent(a, b);
}

static int cmp_publisher(const void *a, const void *b)
{
    const Game *x = &L.games[*(const int *)a], *y = &L.games[*(const int *)b];
    char px[96], py[96];
    str_fold(x->publisher, px, sizeof(px));
    str_fold(y->publisher, py, sizeof(py));
    int c = strcmp(px, py);
    return c ? c : cmp_name(a, b);
}

static void set_hero(int game_index, int dir)
{
    if (game_index == ui.hero_game)
        return;
    ui.hero_prev = ui.hero_game;
    ui.hero_game = game_index;
    ui.hero_t = ui.hero_prev >= 0 ? 0 : 1;
    ui.hero_dir = dir;
}

static void set_sel(int s, bool sound)
{
    if (ui.nview <= 0) {
        ui.sel = 0;
        set_hero(-1, 0);
        return;
    }
    if (s < 0) s = 0;
    if (s >= ui.nview) s = ui.nview - 1;
    int dir = s > ui.sel ? 1 : s < ui.sel ? -1 : 0;
    bool changed = s != ui.sel;
    ui.sel = s;
    Game *g = &L.games[ui.view[s]];
    ui.sel_tid = g->tid;
    ui.tab_sel_tid[ui.tab] = g->tid;
    set_hero(ui.view[s], dir);
    if (sound && changed)
        audio_play_pitch(SFX_MOVE, 0.96f + 0.08f * ((s * 37) % 10) / 10.0f);
}

uint32_t g_view_built_at;

void views_rebuild(void)
{
    g_view_built_at = SDL_GetTicks();
    if (ui.capview < L.count + 1) {
        ui.capview = L.count + 16;
        ui.view = realloc(ui.view, sizeof(int) * ui.capview);
    }
    const Tab *t = &ui.tabs[ui.tab];
    ui.nview = 0;
    for (int i = 0; i < L.count; i++) {
        Game *g = &L.games[i];
        if (g->hidden && !L.set.show_hidden)
            continue;
        bool ok = true;
        switch (t->kind) {
        case TAB_RECENT: ok = game_last_time(g) > 0; break;
        case TAB_FAV: ok = g->favorite; break;
        case TAB_CAT: ok = (g->catmask >> t->cat) & 1; break;
        default: break;
        }
        if (ok && ui.search_fold[0]) {
            char pub[128];
            str_fold(g->publisher, pub, sizeof(pub));
            ok = str_contains_folded(g->sortkey, ui.search_fold) || str_contains_folded(pub, ui.search_fold);
        }
        if (ok)
            ui.view[ui.nview++] = i;
    }
    int (*cmp)(const void *, const void *) = cmp_name;
    if (t->kind == TAB_RECENT)
        cmp = cmp_recent;
    else if (L.set.sort == 1)
        cmp = cmp_recent;
    else if (L.set.sort == 2)
        cmp = cmp_played;
    else if (L.set.sort == 3)
        cmp = cmp_publisher;
    qsort(ui.view, ui.nview, sizeof(int), cmp);
    if (t->kind == TAB_RECENT && ui.nview > 30)
        ui.nview = 30;

    if (!ui.user_moved) {
        // until the user moves, stay on the last game started (or the first one)
        ui.sel_tid = L.set.last_tid;
        ui.sel = 0;
    }
    int s = -1;
    for (int i = 0; i < ui.nview; i++)
        if (L.games[ui.view[i]].tid == ui.sel_tid)
            s = i;
    if (s < 0)
        s = ui.sel < ui.nview ? ui.sel : ui.nview - 1;
    if (s < 0)
        s = 0;
    int keep = ui.sel;
    ui.sel = s;
    if (ui.nview > 0) {
        ui.sel_tid = L.games[ui.view[s]].tid;
        set_hero(ui.view[s], s >= keep ? 1 : -1);
    } else {
        set_hero(-1, 0);
    }
    L.list_dirty = false;
}

static void set_tab(int t, bool banner)
{
    if (ui.ntabs == 0)
        return;
    t = (t % ui.ntabs + ui.ntabs) % ui.ntabs;
    if (t == ui.tab)
        return;
    ui.tab_sel_tid[ui.tab] = ui.sel_tid;
    ui.tab = t;
    if (ui.tab_sel_tid[t])
        ui.sel_tid = ui.tab_sel_tid[t];
    views_rebuild();
    if (L.set.view != 1)
        ui.scroll = (float)ui.sel;
    if (banner) {
        ui.banner_t = 0.0001f;
        ui.banner_tab = t;
    }
    audio_play(SFX_TAB);
    L.set.last_tab = t;
    lib_save_soon();
}

static int letter_of(const Game *g)
{
    return (unsigned char)g->sortkey[0];
}

static void jump_letter(int dir)
{
    if (ui.nview == 0)
        return;
    if (L.set.sort != 0 || ui.tabs[ui.tab].kind == TAB_RECENT) {
        set_sel(ui.sel + dir * (L.set.view == 1 ? GRID_COLS * 3 : 5), true);
        return;
    }
    int cur = letter_of(&L.games[ui.view[ui.sel]]);
    int s = ui.sel;
    if (dir > 0) {
        while (s < ui.nview - 1 && letter_of(&L.games[ui.view[s]]) == cur)
            s++;
    } else {
        // go to the first game of the previous letter group
        while (s > 0 && letter_of(&L.games[ui.view[s]]) == cur)
            s--;
        int prev = letter_of(&L.games[ui.view[s]]);
        while (s > 0 && letter_of(&L.games[ui.view[s - 1]]) == prev)
            s--;
    }
    set_sel(s, true);
    char msg[8] = { 0 };
    int c = letter_of(&L.games[ui.view[ui.sel]]);
    msg[0] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
    if ((unsigned char)msg[0] >= 0x80)
        msg[0] = '#';
    ui_toast(msg, -1);
}

// ------------------------------------------------------------ actions

static void launch_now(int gi)
{
    ui.launching = true;
    ui.launch_t = 0;
    ui.launch_game = gi;
    ui.launch_fired = false;
    ui.launch_ready = false;
    ui.launch_tid = L.games[gi].tid;
    lib_request_boot_full(gi);
    panels_close_all();
    audio_play(SFX_LAUNCH);
    audio_fade_music(0, 0.8f);
}

void start_launch(int gi)
{
    if (ui.launching || gi < 0 || gi >= L.count)
        return;
    if (L.games[gi].pt.wii) {
        // like the Wii U Menu: first ask how to play (GamePad or Wii Remote, which screen)
        panels_close_all();
        audio_play(SFX_OPEN);
        panel_open(P_WIIMODE, gi);
        return;
    }
    launch_now(gi);
}

void start_launch_wii(int gi, int mode)
{
    if (ui.launching || gi < 0 || gi >= L.count)
        return;
    ui.launch_wii_mode = mode;
    launch_now(gi);
}

void game_toggle_hidden(Game *g)
{
    if (!g)
        return;
    g->hidden = !g->hidden;
    audio_play(g->hidden ? SFX_TOGGLE_OFF : SFX_TOGGLE_ON);
    if (!g->hidden)
        ui_toast("El juego vuelve a mostrarse", IC_EYE);
    else if (L.set.show_hidden)
        ui_toast("Juego oculto", IC_EYE_OFF);
    else
        ui_toast("Juego oculto · lo recuperas en Ajustes > Juegos ocultos", IC_EYE_OFF);
    L.list_dirty = true;
    lib_save_soon();
}

static void toggle_fav(Game *g)
{
    g->favorite = !g->favorite;
    audio_play(g->favorite ? SFX_FAV : SFX_TOGGLE_OFF);
    ui_toast(g->favorite ? "Añadido a Favoritos" : "Quitado de Favoritos", g->favorite ? IC_STAR : IC_STAR_OUTLINE);
    lib_save_soon();
    if (ui.tabs[ui.tab].kind == TAB_FAV)
        views_rebuild();
}

void home_action(Button b)
{
    if (ui.launching)
        return;
    Game *g = sel_game();
    if (ui.focus == FOCUS_TABS) {
        switch (b) {
        case BTN_LEFT: set_tab(ui.tab - 1, false); return;
        case BTN_RIGHT: set_tab(ui.tab + 1, false); return;
        case BTN_DOWN: case BTN_A: case BTN_B:
            ui.focus = FOCUS_GAMES;
            audio_play(SFX_MOVE);
            return;
        default: break;
        }
    }
    int cols = L.set.view == 1 ? GRID_COLS : 1;
    switch (b) {
    case BTN_LEFT: set_sel(ui.sel - 1, true); break;
    case BTN_RIGHT: set_sel(ui.sel + 1, true); break;
    case BTN_UP:
        if (L.set.view == 1 && ui.sel >= cols)
            set_sel(ui.sel - cols, true);
        else if (ui.focus != FOCUS_TABS) {
            ui.focus = FOCUS_TABS;
            audio_play(SFX_MOVE);
        }
        break;
    case BTN_DOWN:
        if (L.set.view == 1 && ui.sel + cols < ui.nview)
            set_sel(ui.sel + cols, true);
        else if (L.set.view == 1 && ui.sel / cols < (ui.nview - 1) / cols)
            set_sel(ui.nview - 1, true);
        break;
    case BTN_L: set_tab(ui.tab - 1, true); break;
    case BTN_R: set_tab(ui.tab + 1, true); break;
    case BTN_ZL: jump_letter(-1); break;
    case BTN_ZR: jump_letter(1); break;
    case BTN_A:
        if (g)
            start_launch(sel_game_index());
        else
            audio_play(SFX_ERROR);
        break;
    case BTN_X:
        if (g) {
            audio_play(SFX_OPEN);
            panel_open(P_GAME, sel_game_index());
        } else {
            audio_play(SFX_ERROR);
        }
        break;
    case BTN_Y:
        if (g)
            toggle_fav(g);
        break;
    case BTN_PLUS:
        audio_play(SFX_OPEN);
        panel_open(P_OPTIONS, 0);
        break;
    case BTN_MINUS:
        audio_play(SFX_SELECT);
        request_text(TT_SEARCH, 0, "Buscar juego por nombre o editor", ui.search, "Buscar");
        break;
    case BTN_B:
        if (ui.search[0]) {
            ui.search[0] = ui.search_fold[0] = 0;
            views_rebuild();
            audio_play(SFX_BACK);
        } else if (ui.focus == FOCUS_TABS) {
            ui.focus = FOCUS_GAMES;
        } else {
            audio_play(SFX_OPEN);
            panel_open(P_CONFIRM, 0);
        }
        break;
    default: break;
    }
}

static void dispatch(Button b)
{
    if (ui.background)
        return;
    ui.user_moved = true;
    if (npanels > 0) {
        panels_input(b);
        return;
    }
    home_action(b);
}

void ui_button(Button b, bool down)
{
    if (b >= BTN_COUNT)
        return;
    if (down && !ui.held[b]) {
        ui.held[b] = true;
        ui.hold_t[b] = 0;
        ui.repeat_t[b] = 0;
        dispatch(b);
    } else if (!down) {
        ui.held[b] = false;
    }
}

void ui_text_result(const char *text, bool ok)
{
    int target = ui.text_target;
    ui.text_target = TT_NONE;
    if (!ok || target == TT_NONE)
        return;
    if (target == TT_SEARCH) {
        char tmp[64];
        str_copy(tmp, sizeof(tmp), text);
        str_copy(ui.search, sizeof(ui.search), str_trim(tmp));
        str_fold(ui.search, ui.search_fold, sizeof(ui.search_fold));
        // searches look through the whole library
        if (ui.search[0] && ui.tabs[ui.tab].kind != TAB_ALL) {
            ui.tab_sel_tid[ui.tab] = ui.sel_tid;
            ui.tab = 0;
        }
        ui.sel = 0;
        views_rebuild();
        set_sel(0, false);
        ui.scroll = 0;
        if (ui.search[0] && ui.nview == 0)
            audio_play(SFX_ERROR);
        return;
    }
    panel_text_result(target, ui.text_ctx, text);
}

// ------------------------------------------------------------ touch

void ui_touch(int type, float x, float y)
{
    if (ui.background || ui.launching)
        return;
    if (type == TOUCH_DOWN) {
        ui.user_moved = true;
        ui.touching = true;
        ui.dragging = false;
        ui.tx0 = ui.tx = ui.last_tx = x;
        ui.ty0 = ui.ty = ui.last_ty = y;
        const Hit *h = hit_at(x, y);
        ui.touch_hit = h ? *h : (Hit){ 0, 0, 0, 0, HIT_NONE, 0 };
        ui.drag_scroll0 = ui.scroll;
        ui.fling_v = 0;
        if (npanels > 0)
            ui.drag_scroll0 = panels[npanels - 1].scroll;
        return;
    }
    if (!ui.touching)
        return;
    if (type == TOUCH_MOVE) {
        float vx = x - ui.last_tx, vy = y - ui.last_ty;
        ui.last_tx = x;
        ui.last_ty = y;
        ui.tx = x;
        ui.ty = y;
        if (!ui.dragging && (fabsf(x - ui.tx0) > 16 || fabsf(y - ui.ty0) > 16))
            ui.dragging = true;
        if (!ui.dragging)
            return;
        if (npanels > 0) {
            Panel *p = &panels[npanels - 1];
            p->scroll = ui.drag_scroll0 - (y - ui.ty0) / 62.0f;
            return;
        }
        if (ui.touch_hit.kind == HIT_TAB || ui.ty0 < 76)
            return;
        if (L.set.view == 0 && ui.ty0 > 490) {
            ui.scroll = ui.drag_scroll0 - (x - ui.tx0) / CAROUSEL_SPACING;
            ui.fling_v = ui.fling_v * 0.5f + (-vx / CAROUSEL_SPACING) * 0.5f;
        } else if (L.set.view == 1 && ui.tx0 < 820) {
            ui.scroll = ui.drag_scroll0 - (y - ui.ty0) / 150.0f;
            ui.fling_v = ui.fling_v * 0.5f + (-vy / 150.0f) * 0.5f;
        }
        return;
    }
    // TOUCH_UP
    ui.touching = false;
    if (ui.dragging) {
        ui.dragging = false;
        if (npanels > 0)
            return;
        if (L.set.view == 0 && ui.ty0 > 490) {
            float target = ui.scroll + ui.fling_v * 8.0f;
            int s = (int)floorf(target + 0.5f);
            set_sel(s, true);
        } else if (L.set.view == 2 && ui.ty0 > 76) {
            float dx = x - ui.tx0, dy = y - ui.ty0;
            if (fabsf(dx) > 40 && fabsf(dx) > fabsf(dy)) {
                // the cover rail follows the finger (~65 px per game); elsewhere one swipe = one game
                int steps = (ui.ty0 > 560 && ui.tx0 > 600) ? (int)floorf(fabsf(dx) / 65.0f + 0.5f) : 1;
                if (steps < 1)
                    steps = 1;
                set_sel(ui.sel + (dx < 0 ? steps : -steps), true);
            }
        }
        return;
    }
    const Hit *h = &ui.touch_hit;
    const Hit *now = hit_at(x, y);
    if (!now || now->kind != h->kind || now->param != h->param)
        return;
    switch (h->kind) {
    case HIT_TAB:
        if (npanels == 0) {
            ui.focus = FOCUS_GAMES;
            set_tab(h->param, true);
        }
        break;
    case HIT_GAME:
        if (npanels == 0) {
            if (h->param == ui.sel)
                start_launch(sel_game_index());
            else
                set_sel(h->param, true);
        }
        break;
    case HIT_PLAY:
        if (npanels == 0)
            start_launch(sel_game_index());
        break;
    case HIT_BUTTON:
        dispatch((Button)h->param);
        break;
    case HIT_SEARCH_CLEAR:
        ui.search[0] = ui.search_fold[0] = 0;
        views_rebuild();
        audio_play(SFX_BACK);
        break;
    case HIT_HIDE:
        if (npanels == 0 && h->param >= 0 && h->param < L.count)
            game_toggle_hidden(&L.games[h->param]);
        break;
    case HIT_PANEL_ITEM:
        panel_touch_item(h->param);
        break;
    case HIT_PANEL_BG:
        panel_touch_outside();
        break;
    default:
        break;
    }
}

// ------------------------------------------------------------ lifecycle

void ui_init(void)
{
    memset(&ui, 0, sizeof(ui));
    ui.hero_game = ui.hero_prev = ui.bg_game = ui.bg_prev = -1;
    ui.art_fade = 1;
    ui.bg_t = 1;
    tabs_rebuild();
    int start = 0;
    if (L.set.start_tab == 1)
        start = 1;
    else if (L.set.start_tab == 2)
        start = L.set.last_tab;
    if (start < 0 || start >= ui.ntabs)
        start = 0;
    ui.tab = start;
    ui.sel_tid = L.set.last_tid;
    views_rebuild();
    ui.scroll = (float)ui.sel;
    theme_update(0);
    audio_set(L.set.sfx, L.set.music);
}

void ui_shutdown(void)
{
    free(ui.view);
    ui.view = NULL;
}

void ui_background(bool bg)
{
    ui.background = bg;
    if (bg) {
        for (int i = 0; i < BTN_COUNT; i++)
            ui.held[i] = false;
        ui.touching = false;
        lib_save();
    }
}

bool ui_is_background(void)
{
    return ui.background;
}

bool ui_quit_requested(void)
{
    return ui.quit;
}

bool ui_take_launch(uint64_t *tid)
{
    if (!ui.launch_ready)
        return false;
    ui.launch_ready = false;
    *tid = ui.launch_tid;
    return true;
}

void ui_launch_failed(void)
{
    ui.launching = false;
    audio_fade_music(1, 1.0f);
    ui_toast("No se pudo iniciar el juego", IC_INFO);
}

static void want_add(int *want, int *nwant, int gi)
{
    if (gi < 0 || gi >= L.count)
        return;
    for (int i = 0; i < *nwant; i++)
        if (want[i] == gi)
            return;
    want[(*nwant)++] = gi;
}

// Art to load, nearest first. The first *nbig entries also get the large
// art (boot screen; background and title logo in the cine view).
static void collect_wanted(int *want, int *nwant, int *nbig)
{
    *nwant = *nbig = 0;
    if (ui.nview == 0)
        return;
    if (L.set.view == 2) {
        static const int near[3] = { 0, 1, -1 };
        for (int k = 0; k < 3; k++) {
            int s = ui.sel + near[k];
            if (s >= 0 && s < ui.nview)
                want_add(want, nwant, ui.view[s]);
        }
        // the backgrounds on screen must not be evicted mid-fade
        want_add(want, nwant, ui.bg_game);
        if (ui.bg_t < 1)
            want_add(want, nwant, ui.bg_prev);
        *nbig = *nwant;
        // the rail draws thumbnails: big covers only just beyond the selection
        // (ready when it moves) and for games that still have no thumbnail
        // (loading the cover makes one)
        for (int d = 2; d <= 6; d++) {
            int s1 = ui.sel + d, s0 = ui.sel - d;
            if (s1 < ui.nview && (d <= 2 || !L.games[ui.view[s1]].thumb))
                want_add(want, nwant, ui.view[s1]);
            if (s0 >= 0 && (d <= 2 || !L.games[ui.view[s0]].thumb))
                want_add(want, nwant, ui.view[s0]);
        }
    } else {
        static const int order[5] = { 0, 1, -1, 2, -2 };
        for (int k = 0; k < 5; k++) {
            int s = ui.sel + order[k];
            if (s >= 0 && s < ui.nview)
                want_add(want, nwant, ui.view[s]);
        }
        *nbig = *nwant;
    }
    if (ui.launching && ui.launch_game >= 0)
        want_add(want, nwant, ui.launch_game);
}

void ui_update(float dt)
{
    ui.time += dt;
    ui.intro_t += dt;
    theme_update(dt);

    // key repeat
    static const Button rep[] = { BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT, BTN_L, BTN_R, BTN_ZL, BTN_ZR };
    for (size_t i = 0; i < sizeof(rep) / sizeof(rep[0]); i++) {
        Button b = rep[i];
        if (!ui.held[b])
            continue;
        ui.hold_t[b] += dt;
        if (ui.hold_t[b] < 0.36f)
            continue;
        float period = ui.hold_t[b] > 1.6f ? 0.045f : 0.085f;
        if (b == BTN_L || b == BTN_R)
            period = 0.22f;
        ui.repeat_t[b] += dt;
        while (ui.repeat_t[b] >= period) {
            ui.repeat_t[b] -= period;
            dispatch(b);
        }
    }

    if (L.list_dirty) {
        tabs_rebuild();
        views_rebuild();
        if (!ui.user_moved) {
            ui.scroll = L.set.view == 0 ? (float)ui.sel : 0;
            ui.hero_t = 1;
        }
    }

    // scrolling
    if (!ui.dragging) {
        if (L.set.view != 1) {
            ui.scroll = approach(ui.scroll, (float)ui.sel, 13.0f, dt);
        } else {
            int row = ui.sel / GRID_COLS;
            int rows = (ui.nview + GRID_COLS - 1) / GRID_COLS;
            float target = ui.scroll;
            if (row < target + 0.2f)
                target = row - 0.2f;
            if (row > target + 2.4f)
                target = row - 2.4f;
            float maxs = rows - 3.6f;
            if (target > maxs) target = maxs;
            if (target < 0) target = 0;
            if (ui.touching)
                target = ui.scroll;
            ui.scroll = approach(ui.scroll, target, 12.0f, dt);
        }
    } else if (L.set.view == 1) {
        int rows = (ui.nview + GRID_COLS - 1) / GRID_COLS;
        ui.scroll = clampf(ui.scroll, -0.5f, fmaxf(0, rows - 3.6f) + 0.5f);
    }

    // hero and background transitions
    if (ui.hero_t < 1)
        ui.hero_t = fminf(1, ui.hero_t + dt / 0.28f);
    static float hero_stable;
    static int hero_seen = -2;
    if (hero_seen != ui.hero_game) {
        hero_seen = ui.hero_game;
        hero_stable = 0;
    }
    hero_stable += dt;
    if (ui.bg_t < 1)
        ui.bg_t = fminf(1, ui.bg_t + dt / 0.55f);
    if (ui.bg_game != ui.hero_game && hero_stable > 0.12f && ui.bg_t >= 1) {
        Game *h = ui.hero_game >= 0 ? &L.games[ui.hero_game] : NULL;
        bool ready;
        if (!h)
            ready = true;
        else if (L.set.view == 2) // its own background, or the boot screen when it has none
            ready = h->fondo || (h->fondo_state == ASSET_FAILED && (h->boot || h->boot_state == ASSET_FAILED));
        else
            ready = h->boot_blur || h->boot_state == ASSET_FAILED;
        if (ready) {
            ui.bg_prev = ui.bg_game;
            ui.bg_game = ui.hero_game;
            ui.bg_t = 0;
        }
    }

    if (ui.banner_t > 0) {
        ui.banner_t += dt / 0.95f;
        if (ui.banner_t >= 1)
            ui.banner_t = 0;
    }
    if (ui.toast_t > 0) {
        ui.toast_t += dt / 2.2f;
        if (ui.toast_t >= 1)
            ui.toast_t = 0;
    }

    if (ui.launching) {
        ui.launch_t += dt;
        if (!ui.launch_fired && ui.launch_t >= 1.2f) {
            ui.launch_fired = true;
            Game *lg = &L.games[ui.launch_game];
            game_launched(lg);
            if (lg->pt.wii)
                plat_wii_display(ui.launch_wii_mode); // screen + controller for Wii mode
            ui.launch_ready = true;
        }
        if (!plat_is_console() && ui.launch_t > 3.2f) {
            // PC test build: pretend the game was closed
            ui.launching = false;
            audio_fade_music(1, 1.0f);
        }
    }

    panels_update(dt);

    int want[24], nwant, nbig;
    collect_wanted(want, &nwant, &nbig);
    lib_update(want, nwant, nbig, sel_game_index());

    // icons near the visible range first
    int vis[40], nvis = 0;
    if (ui.nview > 0) {
        int lo, hi;
        if (L.set.view != 1) {
            lo = (int)ui.scroll - 7;
            hi = (int)ui.scroll + 8;
        } else {
            lo = ((int)ui.scroll - 1) * GRID_COLS;
            hi = ((int)ui.scroll + 5) * GRID_COLS;
        }
        if (lo < 0) lo = 0;
        if (hi >= ui.nview) hi = ui.nview - 1;
        // nearest to the selection first
        vis[nvis++] = ui.view[ui.sel];
        for (int d = 1; nvis < 40 && (ui.sel - d >= lo || ui.sel + d <= hi); d++) {
            if (ui.sel + d <= hi) vis[nvis++] = ui.view[ui.sel + d];
            if (ui.sel - d >= lo && nvis < 40) vis[nvis++] = ui.view[ui.sel - d];
        }
    }
    static uint32_t last_req;
    uint32_t h = (uint32_t)nvis * 2654435761u;
    for (int i = 0; i < nvis; i++)
        h = h * 31 + (uint32_t)vis[i];
    if (h != last_req) {
        last_req = h;
        lib_request_icons(vis, nvis);
    }

    // a song from the SD card starts: show its title, as Pixmo Games does
    static int song_seen;
    char song[128];
    int seq = audio_song(song, sizeof(song));
    if (seq != song_seen) {
        song_seen = seq;
        if (song[0] && L.set.music && !ui.launching && seq > 0)
            ui_toast(song, IC_NOTE);
    }

    // per-game animation for the selection highlight
    for (int i = 0; i < ui.nview; i++) {
        Game *g = &L.games[ui.view[i]];
        float target = (i == ui.sel && ui.focus == FOCUS_GAMES) ? 1.0f : 0.0f;
        g->anim_sel = approach(g->anim_sel, target, 16.0f, dt);
    }
}

// ------------------------------------------------------------ rendering

static void info_row(float x, float y, int icon, const char *text, Color c)
{
    icon_draw(icon, x, y + 2, 22, T.text_faint);
    text_draw(20, text, x + 32, y, c, ALIGN_LEFT);
}

// one cell of the "fact sheet": icon tile, small caption and value
static void fact(float x, float y, float w, int icon, const char *caption, const char *value)
{
    gfx_rrect(x, y + 4, 42, 42, 12, col_alpha(T.panel, 0.75f));
    gfx_rrect_outline(x, y + 4, 42, 42, 12, 1, col_alpha(T.bar_line, 0.6f));
    icon_draw(icon, x + 10, y + 14, 22, T.accent);
    text_draw(15, caption, x + 54, y + 3, T.text_faint, ALIGN_LEFT);
    char buf[160];
    text_fit(20, value, w - 62, buf, sizeof(buf));
    text_draw(20, buf, x + 54, y + 23, T.text, ALIGN_LEFT);
}

static void controllers_text(const Game *g, char *out, size_t n)
{
    str_copy(out, n, "GamePad");
    if (g->ext_flags & EXT_PRO) strncat(out, ", Pro Controller", n - strlen(out) - 1);
    if (g->ext_flags & (EXT_NUNCHUK | EXT_CLASSIC)) strncat(out, ", Wiimote", n - strlen(out) - 1);
    if (g->ext_flags & EXT_BOARD) strncat(out, ", Balance Board", n - strlen(out) - 1);
}

static void draw_chips(Game *g, float x, float y, float w)
{
    float cx = x, cmax = x + w, cw;
    if (g->favorite) {
        draw_chip(cx, y, "Favorito", IC_STAR, rgb(0xFFB300), rgb(0xFFFFFF), &cw);
        cx += cw + 8;
    }
    for (int i = 0; i < L.ncats; i++) {
        Category *c = &L.cats[i];
        if (!((g->catmask >> c->id) & 1))
            continue;
        float need = chip_width(c->name, c->icon);
        if (cx + need > cmax)
            break;
        draw_chip(cx, y, c->name, c->icon, theme_color_of(c->color), rgb(0xFFFFFF), &cw);
        cx += cw + 8;
    }
    if (g->has_dlc && cx + chip_width("DLC instalado", IC_PLUS) <= cmax) {
        draw_chip(cx, y, "DLC instalado", IC_PLUS, col_alpha(T.panel, 0.8f), T.text_dim, &cw);
        cx += cw + 8;
    }
}

static void draw_hero_info(Game *g, float x, float y, float w, float alpha, bool compact)
{
    gfx_push_alpha(alpha);
    char buf[256], when[64], line[160];
    format_relative_time(game_last_time(g), when, sizeof(when));
    if (!compact) {
        char lines[2][256];
        int n = text_wrap(40, g->name[0] ? g->name : "...", w, 2, lines);
        int ts = n > 1 ? 36 : 40;
        if (n > 1)
            n = text_wrap(ts, g->name, w, 2, lines);
        for (int i = 0; i < n; i++)
            text_draw(ts, lines[i], x, y + i * ts * 1.16f, T.text, ALIGN_LEFT);
        y += n * ts * 1.16f + 4;
        text_fit(22, g->publisher[0] ? g->publisher : " ", w, buf, sizeof(buf));
        text_draw(22, buf, x, y, T.text_dim, ALIGN_LEFT);
        y += 38;
        gfx_rect(x, y, w, 1, col_alpha(T.text_faint, 0.45f));
        y += 14;
        // fact sheet: 3 columns x 2 rows
        float cw = w / 3;
        fact(x, y, cw, IC_CLOCK, "Última vez", when);
        if (g->play_count == 0)
            str_copy(line, sizeof(line), "Aún no desde aquí");
        else if (g->play_count == 1)
            str_copy(line, sizeof(line), "1 vez");
        else
            snprintf(line, sizeof(line), tr("%u veces"), g->play_count);
        fact(x + cw, y, cw, IC_PLAY, "Iniciado", line);
        int dev_icon = !strcmp(g->pt.device, "usb") ? IC_USB : !strcmp(g->pt.device, "odd") ? IC_DISC : IC_CHIP;
        fact(x + cw * 2, y, cw, dev_icon, "Ubicación", game_device_name(g));
        y += 56;
        if (g->has_update)
            snprintf(line, sizeof(line), tr("Actualización v%u"), g->update_version);
        else
            str_copy(line, sizeof(line), "Sin actualizaciones");
        fact(x, y, cw, IC_DOWNLOAD, "Versión", line);
        controllers_text(g, line, sizeof(line));
        fact(x + cw, y, cw, IC_GAMEPAD, "Mandos", line);
        if (g->product[0])
            str_copy(line, sizeof(line), g->product);
        else
            snprintf(line, sizeof(line), "%08X", (unsigned)(g->tid & 0xFFFFFFFF));
        fact(x + cw * 2, y, cw, IC_TAG, "Código", line);
        y += 64;
        draw_chips(g, x, y, w);
    } else {
        char lines[2][256];
        int n = text_wrap(26, g->name[0] ? g->name : "...", w, 2, lines);
        for (int i = 0; i < n; i++)
            text_draw(26, lines[i], x, y + i * 31, T.text, ALIGN_LEFT);
        y += n * 31 + 2;
        text_fit(18, g->publisher[0] ? g->publisher : " ", w, buf, sizeof(buf));
        text_draw(18, buf, x, y, T.text_dim, ALIGN_LEFT);
        y += 30;
        gfx_rect(x, y, w, 1, col_alpha(T.text_faint, 0.45f));
        y += 12;
        snprintf(line, sizeof(line), tr("Última vez: %s"), when);
        info_row(x, y, IC_CLOCK, line, T.text);
        y += 30;
        int dev_icon = !strcmp(g->pt.device, "usb") ? IC_USB : !strcmp(g->pt.device, "odd") ? IC_DISC : IC_CHIP;
        text_fit(20, game_device_name(g), w - 32, buf, sizeof(buf));
        info_row(x, y, dev_icon, buf, T.text_dim);
        y += 38;
        draw_chips(g, x, y, w);
    }
    gfx_pop_alpha();
}

void home_empty_texts(const char **out_title, const char **out_sub, int *out_icon)
{
    const Tab *t = &ui.tabs[ui.tab];
    const char *title, *sub;
    int icon = t->icon;
    if (L.count == 0) {
        title = "No se encontraron juegos instalados";
        sub = "Pixmo Games muestra los juegos de la memoria del sistema y del disco USB.";
        icon = IC_GAMEPAD;
    } else if (ui.search[0]) {
        title = "Ningún juego coincide con la búsqueda";
        sub = "Pulsa B para quitar la búsqueda.";
        icon = IC_SEARCH;
    } else if (t->kind == TAB_FAV) {
        title = "Aún no tienes favoritos";
        sub = "Pulsa Y sobre un juego para marcarlo como favorito.";
    } else if (t->kind == TAB_RECENT) {
        title = "Todavía no hay juegos recientes";
        sub = "Los juegos que inicies aparecerán aquí.";
    } else if (t->kind == TAB_CAT) {
        title = "Esta categoría está vacía";
        sub = "Pulsa X sobre un juego y elige «Categorías» para añadirlo.";
    } else {
        title = "Cargando biblioteca...";
        sub = "";
    }
    *out_title = title;
    *out_sub = sub;
    *out_icon = icon;
}

static void draw_empty(float x, float y, float w, float h)
{
    const Tab *t = &ui.tabs[ui.tab];
    const char *title, *sub;
    int icon;
    home_empty_texts(&title, &sub, &icon);
    float cy = y + h * 0.5f;
    if (t->kind == TAB_CAT || t->kind == TAB_FAV || t->kind == TAB_RECENT || L.count == 0 || ui.search[0]) {
        if (icon == t->icon)
            draw_tab_badge(t, x + w * 0.5f - 48, cy - 120, 96);
        else
            draw_badge(icon, 0x8C96A0, NULL, x + w * 0.5f - 48, cy - 120, 96);
    }
    text_draw(28, title, x + w * 0.5f, cy, T.text, ALIGN_CENTER);
    text_draw(19, sub, x + w * 0.5f, cy + 44, T.text_dim, ALIGN_CENTER);
}

static void draw_section_header(float x, float y)
{
    const Tab *t = &ui.tabs[ui.tab];
    draw_tab_badge(t, x, y, 26);
    char s[128];
    if (ui.nview == 1)
        snprintf(s, sizeof(s), tr("%s  ·  1 juego"), t->kind == TAB_ALL ? tr("Todos los juegos") : tr(t->name));
    else
        snprintf(s, sizeof(s), tr("%s  ·  %d juegos"), t->kind == TAB_ALL ? tr("Todos los juegos") : tr(t->name), ui.nview);
    float w = text_draw(19, s, x + 36, y + 13 - 19 * 0.62f, T.text_dim, ALIGN_LEFT);
    if (ui.search[0]) {
        char q[96], fit[96];
        snprintf(q, sizeof(q), tr("«%s»"), ui.search);
        text_fit(17, q, 260, fit, sizeof(fit));
        float cx = x + 36 + w + 16, cw;
        draw_chip(cx, y - 3, fit, IC_SEARCH, T.accent, T.accent_text, &cw);
        hit_add(cx, y - 3, cw, 32, HIT_SEARCH_CLEAR, 0);
    }
}

static void render_showcase(void)
{
    const float CX = 64, CY = 96, CW = 252, CH = 356;
    Game *g = ui.hero_game >= 0 ? &L.games[ui.hero_game] : NULL;
    Game *pg = ui.hero_prev >= 0 ? &L.games[ui.hero_prev] : NULL;
    float t = ease_out_cubic(ui.hero_t);
    float dir = (float)(ui.hero_dir ? ui.hero_dir : 1);

    if (ui.nview == 0) {
        draw_empty(0, 80, SCREEN_W, 480);
    } else {
        if (pg && t < 1)
            draw_cover(pg, CX - dir * 46 * t, CY, CW, CH, 1 - t);
        if (g) {
            float launch_hide = ui.launching ? 0 : 1;
            draw_cover(g, CX + dir * 46 * (1 - t), CY, CW, CH, t * launch_hide);
            hit_add(CX, CY, CW, CH, HIT_PLAY, 0);
        }
        const float IX = 352, IW = 866;
        if (g) {
            draw_hero_info(g, IX + dir * 20 * (1 - t), CY + 4, IW, t, false);
            float by = CY + CH - 60;
            draw_button(IX, by, 230, 60, BTN_A, "Jugar", true, 0, BTN_A);
            draw_button(IX + 246, by, 220, 60, BTN_X, "Opciones", false, 0, BTN_X);
            draw_button(IX + 482, by, 250, 60, BTN_Y, g->favorite ? "Quitar favorito" : "Favorito", false, 0,
                        BTN_Y);
            draw_round_button(IX + 748, by, 60, g->hidden ? IC_EYE : IC_EYE_OFF, HIT_HIDE, ui.hero_game);
        }
    }

    // carousel
    draw_section_header(64, 466);
    if (ui.nview > 0) {
        char pos[32];
        snprintf(pos, sizeof(pos), "%d / %d", ui.sel + 1, ui.nview);
        text_draw(18, pos, SCREEN_W - 64, 468, T.text_faint, ALIGN_RIGHT);
    }
    const float cy = 580, base = 104, big = 132;
    int first = (int)floorf(ui.scroll) - 6, last = (int)ceilf(ui.scroll) + 6;
    gfx_clip(0, 494, SCREEN_W, 176);
    // draw far items first so the selected one ends on top
    for (int pass = 0; pass < 2; pass++) {
        for (int i = first; i <= last; i++) {
            if (i < 0 || i >= ui.nview)
                continue;
            Game *gi = &L.games[ui.view[i]];
            float rel = i - ui.scroll;
            float k = gi->anim_sel;
            if ((pass == 0) == (k > 0.5f))
                continue;
            float size = base + (big - base) * k;
            float x = SCREEN_W * 0.5f + rel * CAROUSEL_SPACING + (rel > 0 ? 1 : -1) * 0 - size * 0.5f;
            float y = cy - size * 0.5f - 6 * k;
            float fade = clampf(1.3f - fabsf(rel) / 5.2f, 0.25f, 1);
            if (ui.launching && i == ui.sel)
                fade *= clampf(1 - ui.launch_t * 3, 0, 1);
            draw_tile(gi, x, y, size, k, fade);
            draw_sel_frame(x, y, size, size, size * 0.15f, k * fade);
            hit_add(x, y, size, size, HIT_GAME, i);
        }
    }
    gfx_unclip();
}

static void render_grid(void)
{
    const float GX = 46, GY = 124, CELL = 148, TILE = 120;
    if (ui.nview == 0) {
        draw_empty(0, 90, 820, 560);
    } else {
        gfx_clip(0, 112, 820, 556);
        int rows = (ui.nview + GRID_COLS - 1) / GRID_COLS;
        int r0 = (int)floorf(ui.scroll) - 1, r1 = (int)ceilf(ui.scroll) + 4;
        for (int pass = 0; pass < 2; pass++) {
            for (int r = r0; r <= r1; r++) {
                if (r < 0 || r >= rows)
                    continue;
                for (int c = 0; c < GRID_COLS; c++) {
                    int i = r * GRID_COLS + c;
                    if (i >= ui.nview)
                        break;
                    Game *gi = &L.games[ui.view[i]];
                    float k = gi->anim_sel;
                    if ((pass == 0) == (k > 0.5f))
                        continue;
                    float size = TILE * (1 + 0.1f * k);
                    float x = GX + c * CELL + (CELL - size) * 0.5f;
                    float y = GY + (r - ui.scroll) * CELL + (CELL - size) * 0.5f;
                    draw_tile(gi, x, y, size, k, 1);
                    draw_sel_frame(x, y, size, size, size * 0.15f, k);
                    hit_add(x, y, size, size, HIT_GAME, i);
                }
            }
        }
        gfx_unclip();
        // scroll bar
        if (rows > 3) {
            float vis = 3.55f / rows;
            float bh = 530 * vis, by = 124 + (530 - bh) * clampf(ui.scroll / fmaxf(0.01f, rows - 3.6f), 0, 1);
            gfx_rrect(806, 124, 6, 530, 3, col_alpha(T.text_faint, 0.25f));
            gfx_rrect(806, by, 6, bh, 3, col_alpha(T.text_dim, 0.7f));
        }
    }

    // detail card
    const float PX = 832, PY = 92, PW = 416, PH = 566;
    gfx_shadow(PX, PY + 4, PW, PH, 20, 26, col_alpha(T.shadow, 0.7f));
    gfx_rrect(PX, PY, PW, PH, 20, col_alpha(T.panel, 0.94f));
    gfx_rrect_outline(PX, PY, PW, PH, 20, 1, col_alpha(T.bar_line, 0.7f));
    Game *g = ui.hero_game >= 0 ? &L.games[ui.hero_game] : NULL;
    Game *pg = ui.hero_prev >= 0 ? &L.games[ui.hero_prev] : NULL;
    float t = ease_out_cubic(ui.hero_t);
    const float cw = 186, ch = 264;
    float cx = PX + (PW - cw) * 0.5f, cyy = PY + 22;
    if (pg && t < 1)
        draw_cover(pg, cx, cyy, cw, ch, 1 - t);
    if (g) {
        draw_cover(g, cx, cyy, cw, ch, t * (ui.launching ? 0 : 1));
        draw_hero_info(g, PX + 26, cyy + ch + 18, PW - 52, t, true);
        draw_button(PX + 26, PY + PH - 82, PW - 52, 58, BTN_A, "Jugar", true, 0, BTN_A);
    }
    draw_section_header(52, 90);
}

static void render_bottom_bar(void)
{
    const float y = 670, h = 50;
    gfx_rect_vgrad(0, y - 8, SCREEN_W, 8, col_alpha(T.shadow, 0), col_alpha(T.shadow, 0.18f));
    gfx_rect_vgrad(0, y, SCREEN_W, h, col_alpha(T.bar_bottom, 0.97f), col_alpha(T.bar_top, 0.97f));
    gfx_rect(0, y, SCREEN_W, 1, T.bar_line);
    float cy = y + h * 0.5f;
    if (ui.focus == FOCUS_TABS) {
        static const Button b1[] = { BTN_LEFT, BTN_A };
        static const char *const l1[] = { "Cambiar categoría", "Ver juegos" };
        draw_hints(b1, l1, 2, 28, cy, ALIGN_LEFT);
    } else {
        static const Button b1[] = { BTN_A, BTN_X, BTN_Y };
        static const char *const l1[] = { "Jugar", "Opciones del juego", "Favorito" };
        draw_hints(b1, l1, ui.nview ? 3 : 0, 28, cy, ALIGN_LEFT);
    }
    static const Button b2[] = { BTN_MINUS, BTN_PLUS, BTN_L, BTN_R };
    static const char *const l2[] = { "Buscar", "Ajustes", "", "Categoría" };
    draw_hints(b2, l2, 4, SCREEN_W - 28, cy, ALIGN_RIGHT);
}

static void render_banner(void)
{
    if (ui.banner_t <= 0 || ui.banner_tab >= ui.ntabs || ui.nview == 0)
        return;
    float t = ui.banner_t;
    float in = ease_out_back(clampf(t / 0.3f, 0, 1));
    float out = 1 - ease_in_out(clampf((t - 0.6f) / 0.4f, 0, 1));
    float a = clampf(t / 0.12f, 0, 1) * out;
    const Tab *tb = &ui.tabs[ui.banner_tab];
    float s = 0.82f + 0.18f * in;
    float w = 360 * s, h = 250 * s;
    float x = SCREEN_W * 0.5f - w * 0.5f, y = 250 - h * 0.5f + 20;
    gfx_push_alpha(a);
    gfx_shadow(x, y + 6, w, h, 28, 36, T.shadow);
    gfx_rrect(x, y, w, h, 28 * s, col_alpha(T.panel, 0.97f));
    float bs = 120 * s;
    draw_tab_badge(tb, SCREEN_W * 0.5f - bs * 0.5f, y + 26 * s, bs);
    text_draw((int)(30 * s), tb->name, SCREEN_W * 0.5f, y + 160 * s, T.text, ALIGN_CENTER);
    char n[48];
    snprintf(n, sizeof(n), tr(ui.nview == 1 ? "1 juego" : "%d juegos"), ui.nview);
    text_draw((int)(18 * s), n, SCREEN_W * 0.5f, y + 202 * s, T.text_dim, ALIGN_CENTER);
    gfx_pop_alpha();
}

static void render_toast(void)
{
    if (ui.toast_t <= 0)
        return;
    float t = ui.toast_t;
    float a = clampf(t / 0.08f, 0, 1) * (1 - clampf((t - 0.82f) / 0.18f, 0, 1));
    float yoff = (1 - ease_out_cubic(clampf(t / 0.12f, 0, 1))) * 20;
    bool big = ui.toast[0] && !ui.toast[1];
    if (big) {
        // letter jump indicator
        float s = 120;
        gfx_push_alpha(a);
        gfx_shadow(SCREEN_W * 0.5f - s * 0.5f, 300 - s * 0.5f, s, s, 26, 30, T.shadow);
        gfx_rrect(SCREEN_W * 0.5f - s * 0.5f, 300 - s * 0.5f, s, s, 26, col_alpha(T.panel, 0.95f));
        text_draw(72, ui.toast, SCREEN_W * 0.5f, 300 - 72 * 0.6f, T.accent, ALIGN_CENTER);
        gfx_pop_alpha();
        return;
    }
    float tw = text_width(20, ui.toast) + 40 + (ui.toast_icon >= 0 ? 30 : 0);
    float x = SCREEN_W * 0.5f - tw * 0.5f, y = 84 - yoff;
    gfx_push_alpha(a);
    gfx_shadow(x, y + 3, tw, 44, 22, 16, T.shadow);
    gfx_rrect(x, y, tw, 44, 22, col_alpha(rgb(0x2B3036), 0.94f));
    float tx = x + 20;
    if (ui.toast_icon >= 0) {
        icon_draw(ui.toast_icon, tx, y + 11, 22, ui.toast_icon == IC_STAR ? rgb(0xFFC107) : rgb(0xFFFFFF));
        tx += 30;
    }
    text_draw(20, ui.toast, tx, y + 22 - 20 * 0.62f, rgb(0xFFFFFF), ALIGN_LEFT);
    gfx_pop_alpha();
}

static void render_launch(void)
{
    if (!ui.launching)
        return;
    Game *g = &L.games[ui.launch_game];
    float t = ui.launch_t;
    // 1) the cover flies to the center and grows
    float p = ease_in_out(clampf(t / 0.55f, 0, 1));
    float sx = L.set.view == 0 ? 66 : 832 + (416 - 186) * 0.5f, sy = L.set.view == 0 ? 98 : 114;
    float sw = L.set.view == 0 ? 262 : 186, sh = L.set.view == 0 ? 372 : 264;
    if (L.set.view == 2)
        cine_case_rect(g, &sx, &sy, &sw, &sh);
    float tw = 300, th = 426;
    float x = lerpf(sx, SCREEN_W * 0.5f - tw * 0.5f, p), y = lerpf(sy, SCREEN_H * 0.5f - th * 0.5f, p);
    float w = lerpf(sw, tw, p), h = lerpf(sh, th, p);
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, col_alpha(T.bg_top, 0.85f * p));
    // 2) the game's own boot screen fades in, like the console does
    float b = ease_in_out(clampf((t - 0.5f) / 0.5f, 0, 1));
    if (g->boot || g->boot_full) {
        gfx_image_cover(g->boot_full ? g->boot_full : g->boot, 0, 0, SCREEN_W, SCREEN_H, 0, rgba(0xFFFFFF, b));
    } else {
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, col_alpha(rgb(0xFFFFFF), b));
        if (g->icon) {
            float s = 160;
            gfx_image(g->icon, NULL, SCREEN_W * 0.5f - s * 0.5f, SCREEN_H * 0.5f - s * 0.5f - 30, s, s, 24,
                      rgba(0xFFFFFF, b));
        }
        text_draw(30, g->name1, SCREEN_W * 0.5f, SCREEN_H * 0.5f + 80, rgba(0x333333, b), ALIGN_CENTER);
    }
    draw_cover(g, x, y, w, h, 1 - b);
    // 3) "starting" indicator
    float a = clampf((t - 0.7f) / 0.3f, 0, 1);
    if (a > 0) {
        gfx_push_alpha(a);
        float px = SCREEN_W - 250, py = SCREEN_H - 70;
        gfx_rrect(px, py, 220, 46, 23, rgba(0x000000, 0.55f));
        draw_spinner(px + 28, py + 23, 9, rgb(0xFFFFFF));
        text_draw(20, "Iniciando...", px + 50, py + 23 - 20 * 0.62f, rgb(0xFFFFFF), ALIGN_LEFT);
        gfx_pop_alpha();
    }
}

void ui_render(void)
{
    ui.nhits = 0;
    bool cine = L.set.view == 2;
    if (cine)
        cine_background();
    else
        draw_background();

    // gentle intro fade
    float intro = ease_out_cubic(clampf(ui.intro_t / 0.5f, 0, 1));
    gfx_push_alpha(intro);
    if (cine)
        cine_render();
    else if (L.set.view == 0)
        render_showcase();
    else
        render_grid();
    render_banner();
    gfx_pop_alpha();

    if (cine) {
        cine_top_bar();
    } else {
        draw_top_bar();
        render_bottom_bar();
    }
    render_toast();
    if (L.status_until && SDL_GetTicks() < L.status_until) {
        float tw = text_width(18, L.status) + 36;
        gfx_rrect(SCREEN_W * 0.5f - tw * 0.5f, 134, tw, 36, 18, col_alpha(rgb(0x3A4048), 0.92f));
        text_draw(18, L.status, SCREEN_W * 0.5f, 152 - 18 * 0.62f, rgb(0xFFFFFF), ALIGN_CENTER);
    }
    panels_render();
    render_launch();
}

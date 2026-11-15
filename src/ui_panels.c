// Paneles modales al estilo de los ajustes de Wii U: ajustes, menú de un
// juego, categorías (crear, editar, icono, color, logo), confirmaciones,
// información y controles.
#include "ui_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Panel panels[8];
int npanels;

enum {
    A_NONE, A_VIEW, A_SORT, A_THEME, A_START, A_BGART, A_DOWNLOAD, A_SHOWHIDDEN, A_SFX, A_MUSIC, A_CLOCK,
    A_READTITLES,
    A_CATMAN, A_RESCAN, A_HELP, A_ABOUT, A_EXIT,
    A_PLAY, A_ASSIGN, A_FAV, A_HIDE, A_RECOVER, A_RESET, A_DETAILS,
    A_NAME, A_ICON, A_COLOR, A_LOGO, A_DELCAT,
    A_YES, A_NO, A_NEWCAT, A_NOLOGO,
    A_WII_GAMEPAD, A_WII_TV, A_WII_DRC, A_HIDDENMAN, A_AUTOBOOT, A_RETURN, A_LANG,
    A_CAT_BASE = 1000, A_LOGO_BASE = 2000, A_GAME_BASE = 3000,
};

enum { CONF_EXIT, CONF_DELCAT, CONF_RESET };

static const uint32_t k_palette[14] = {
    0xE5533D, 0xF57C00, 0xF2A900, 0x7CB342, 0x2E9E5B, 0x00A3A3, 0x0097D6,
    0x3F6FD8, 0x5C6BC0, 0x6A4FC8, 0xB4479C, 0xE05A8A, 0x8D6E63, 0x7A8793,
};

static const char *k_view[3] = { "Vitrina", "Cuadrícula", "Cine" };
static const char *k_sort[4] = { "Nombre", "Última vez jugado", "Más jugados", "Editor" };
static const char *k_theme[2] = { "Claro", "Oscuro" };
static const char *k_start[3] = { "Todos", "Recientes", "La última pestaña" };
static const char *k_lang[3] = { "Automático", "Español", "English" };

static char s_logos[64][64];
static int s_nlogos;

// ------------------------------------------------------------ building

static PItem *add(Panel *p, int kind, int id, int icon, const char *label, const char *value)
{
    if (p->n >= 80)
        return &p->items[79];
    PItem *it = &p->items[p->n++];
    memset(it, 0, sizeof(*it));
    it->kind = kind;
    it->id = id;
    it->icon = icon;
    str_copy(it->label, sizeof(it->label), label);
    str_copy(it->value, sizeof(it->value), value ? value : "");
    return it;
}

static void info(Panel *p, const char *label, const char *value)
{
    add(p, PI_INFO, 0, -1, label, value);
}

static Game *ctx_game(const Panel *p)
{
    return p->ctx >= 0 && p->ctx < L.count ? &L.games[p->ctx] : NULL;
}

// game indices by name (list of hidden games)
static int cmp_by_name(const void *a, const void *b)
{
    return strcmp(L.games[*(const int *)a].sortkey, L.games[*(const int *)b].sortkey);
}

static void region_name(uint32_t r, char *out, size_t n)
{
    if (r == 0xFFFFFFFF || (r & 7) == 7) {
        str_copy(out, n, "Todas las regiones");
        return;
    }
    out[0] = 0;
    const char *names[7] = { "Japón", "América", "Europa", "Australia", "China", "Corea", "Taiwán" };
    for (int i = 0; i < 7; i++)
        if (r & (1u << i)) {
            if (out[0])
                strncat(out, ", ", n - strlen(out) - 1);
            strncat(out, tr(names[i]), n - strlen(out) - 1);
        }
    if (!out[0])
        str_copy(out, n, "—");
}

static void build(Panel *p)
{
    int keep = p->sel;
    p->n = 0;
    p->cols = 0;
    p->w = 720;
    p->icon = -1;
    p->icon_color = 0x009AC7;
    p->subtitle[0] = 0;
    char v[128];
    switch (p->type) {
    case P_OPTIONS:
        str_copy(p->title, sizeof(p->title), "Ajustes");
        p->icon = IC_GEAR;
        p->icon_color = 0x6C7A89;
        add(p, PI_CHOICE, A_VIEW, IC_CAROUSEL, "Vista", k_view[L.set.view % 3]);
        add(p, PI_CHOICE, A_SORT, IC_SORT, "Ordenar juegos por", k_sort[L.set.sort & 3]);
        add(p, PI_CHOICE, A_THEME, IC_MOON, "Tema", k_theme[L.set.theme & 1]);
        add(p, PI_CHOICE, A_START, IC_ALL, "Al abrir, mostrar", k_start[L.set.start_tab % 3]);
        add(p, PI_CHOICE, A_LANG, IC_FLAG, "Idioma", k_lang[L.set.lang % 3]);
        add(p, PI_TOGGLE, A_BGART, IC_IMAGE, "Fondo con el arte del juego", NULL)->on = L.set.bg_art;
        add(p, PI_TOGGLE, A_DOWNLOAD, IC_DOWNLOAD, "Descargar carátulas de GameTDB", NULL)->on = L.set.download;
        add(p, PI_TOGGLE, A_READTITLES, IC_CHIP, "Leer iconos de la consola", NULL)->on = L.set.read_titles;
        add(p, PI_TOGGLE, A_SHOWHIDDEN, IC_EYE, "Mostrar juegos ocultos", NULL)->on = L.set.show_hidden;
        {
            int nh = 0;
            for (int i = 0; i < L.count; i++)
                nh += L.games[i].hidden;
            snprintf(v, sizeof(v), tr(nh == 1 ? "1 juego" : "%d juegos"), nh);
            add(p, PI_ACTION, A_HIDDENMAN, IC_EYE_OFF, "Juegos ocultos", v);
        }
        add(p, PI_TOGGLE, A_SFX, IC_SOUND, "Efectos de sonido", NULL)->on = L.set.sfx;
        add(p, PI_TOGGLE, A_MUSIC, IC_NOTE, "Música ambiental", NULL)->on = L.set.music;
        add(p, PI_TOGGLE, A_CLOCK, IC_CLOCK, "Mostrar reloj", NULL)->on = L.set.show_clock;
        // these two need the Aroma plugin pixmo_inicio.wps (LEEME: ABRIR AL ENCENDER)
        add(p, PI_TOGGLE, A_AUTOBOOT, IC_POWER, "Abrir Pixmo Games al encender", NULL)->on = L.set.al_encender;
        add(p, PI_TOGGLE, A_RETURN, IC_BACK, "Volver aquí al cerrar un juego", NULL)->on = L.set.volver;
        if (!plat_boot_plugin_installed())
            info(p, "Plugin de inicio (pixmo_inicio.wps)", "No instalado: ver LEEME");
        snprintf(v, sizeof(v), "%d", L.ncats);
        add(p, PI_ACTION, A_CATMAN, IC_TAG, "Gestionar categorías", v);
        add(p, PI_ACTION, A_RESCAN, IC_REFRESH, "Volver a leer la biblioteca", NULL);
        add(p, PI_ACTION, A_HELP, IC_GAMEPAD, "Controles", NULL);
        add(p, PI_ACTION, A_ABOUT, IC_INFO, "Acerca de Pixmo Games", NULL);
        add(p, PI_ACTION, A_EXIT, IC_POWER, "Salir al menú Wii U", NULL);
        break;
    case P_GAME: {
        Game *g = ctx_game(p);
        if (!g)
            break;
        str_copy(p->title, sizeof(p->title), g->name1);
        str_copy(p->subtitle, sizeof(p->subtitle), g->publisher);
        add(p, PI_ACTION, A_PLAY, IC_PLAY, "Jugar", NULL);
        add(p, PI_ACTION, A_HIDE, g->hidden ? IC_EYE : IC_EYE_OFF,
            g->hidden ? "Volver a mostrar este juego" : "Ocultar este juego", NULL);
        int nc = 0;
        for (int i = 0; i < L.ncats; i++)
            if ((g->catmask >> L.cats[i].id) & 1)
                nc++;
        if (nc == 0)
            str_copy(v, sizeof(v), "Ninguna");
        else if (nc == 1)
            str_copy(v, sizeof(v), "1 categoría");
        else
            snprintf(v, sizeof(v), tr("%d categorías"), nc);
        add(p, PI_ACTION, A_ASSIGN, IC_TAG, "Categorías", v);
        add(p, PI_ACTION, A_FAV, g->favorite ? IC_STAR_OUTLINE : IC_STAR,
            g->favorite ? "Quitar de Favoritos" : "Añadir a Favoritos", NULL);
        if (L.set.download && g->tdb_id[0] && !g->tdb_shared)
            add(p, PI_ACTION, A_RECOVER, IC_DOWNLOAD, "Buscar la carátula otra vez", NULL);
        add(p, PI_ACTION, A_RESET, IC_REFRESH, "Reiniciar estadísticas", NULL);
        add(p, PI_ACTION, A_DETAILS, IC_INFO, "Información del juego", NULL);
        break;
    }
    case P_ASSIGN: {
        Game *g = ctx_game(p);
        if (!g)
            break;
        str_copy(p->title, sizeof(p->title), "Categorías");
        str_copy(p->subtitle, sizeof(p->subtitle), g->name1);
        p->icon = IC_TAG;
        for (int i = 0; i < L.ncats; i++) {
            Category *c = &L.cats[i];
            PItem *it = add(p, PI_CHECK, A_CAT_BASE + c->id, c->icon, c->name, NULL);
            it->badge = true;
            it->color = c->color;
            it->logo = c->logo_tex;
            it->on = (g->catmask >> c->id) & 1;
        }
        add(p, PI_ACTION, A_NEWCAT, IC_PLUS, "Nueva categoría...", NULL);
        break;
    }
    case P_CATMAN:
        str_copy(p->title, sizeof(p->title), "Gestionar categorías");
        str_copy(p->subtitle, sizeof(p->subtitle), "Crea categorías como «Juegos de disparos» y dales un logo");
        p->icon = IC_TAG;
        for (int i = 0; i < L.ncats; i++) {
            Category *c = &L.cats[i];
            int n = cat_count_games(c->id);
            if (n == 1)
                str_copy(v, sizeof(v), "1 juego");
            else
                snprintf(v, sizeof(v), tr("%d juegos"), n);
            PItem *it = add(p, PI_ACTION, A_CAT_BASE + c->id, c->icon, c->name, v);
            it->badge = true;
            it->color = c->color;
            it->logo = c->logo_tex;
        }
        if (L.ncats < MAX_CATEGORIES)
            add(p, PI_ACTION, A_NEWCAT, IC_PLUS, "Nueva categoría...", NULL);
        break;
    case P_CATEDIT: {
        Category *c = cat_by_id(p->ctx);
        if (!c)
            break;
        str_copy(p->title, sizeof(p->title), c->name);
        str_copy(p->subtitle, sizeof(p->subtitle), "Editar categoría");
        add(p, PI_ACTION, A_NAME, IC_PENCIL, "Nombre", c->name);
        PItem *it = add(p, PI_ACTION, A_ICON, c->icon, "Icono", icon_name(c->icon));
        it->badge = true;
        it->color = c->color;
        add(p, PI_CHOICE, A_COLOR, IC_PALETTE, "Color", "");
        add(p, PI_ACTION, A_LOGO, IC_IMAGE, "Logo personalizado", c->logo[0] ? c->logo : "Ninguno");
        add(p, PI_ACTION, A_DELCAT, IC_TRASH, "Eliminar categoría", NULL);
        break;
    }
    case P_ICONPICK: {
        Category *c = cat_by_id(p->ctx);
        str_copy(p->title, sizeof(p->title), "Elige un icono");
        str_copy(p->subtitle, sizeof(p->subtitle), c ? c->name : "");
        p->icon = IC_STAR;
        p->cols = 7;
        p->w = 760;
        for (int i = 0; i < IC_CATEGORY_COUNT; i++) {
            PItem *it = add(p, PI_GRID, i, i, icon_name(i), NULL);
            it->badge = true;
            it->color = c ? c->color : 0x009AC7;
            it->on = c && c->icon == i;
        }
        break;
    }
    case P_COLORPICK: {
        Category *c = cat_by_id(p->ctx);
        str_copy(p->title, sizeof(p->title), "Elige un color");
        str_copy(p->subtitle, sizeof(p->subtitle), c ? c->name : "");
        p->icon = IC_PALETTE;
        p->cols = 7;
        p->w = 760;
        for (int i = 0; i < 14; i++) {
            PItem *it = add(p, PI_GRID, i, c ? c->icon : IC_STAR, "", NULL);
            it->badge = true;
            it->color = k_palette[i];
            it->on = c && c->color == k_palette[i];
        }
        break;
    }
    case P_LOGOPICK: {
        Category *c = cat_by_id(p->ctx);
        str_copy(p->title, sizeof(p->title), "Logo personalizado");
        str_copy(p->subtitle, sizeof(p->subtitle), "Imágenes PNG/JPG de SD:/wiiu/apps/pixmogames/logos/");
        p->icon = IC_IMAGE;
        s_nlogos = logos_list(s_logos, 64);
        add(p, PI_CHECK, A_NOLOGO, c ? c->icon : IC_STAR, "Sin logo (usar el icono)", NULL)->on = c && !c->logo[0];
        p->items[p->n - 1].badge = true;
        p->items[p->n - 1].color = c ? c->color : 0x009AC7;
        for (int i = 0; i < s_nlogos; i++)
            add(p, PI_CHECK, A_LOGO_BASE + i, IC_IMAGE, s_logos[i], NULL)->on = c && !strcmp(c->logo, s_logos[i]);
        if (s_nlogos == 0)
            info(p, "No hay imágenes todavía.", "Copia archivos PNG a la carpeta logos de la SD");
        break;
    }
    case P_CONFIRM:
        p->icon = IC_INFO;
        if (p->ctx == CONF_EXIT) {
            str_copy(p->title, sizeof(p->title), "¿Salir de Pixmo Games?");
            str_copy(p->subtitle, sizeof(p->subtitle), "Volverás al menú Wii U.");
            p->icon = IC_POWER;
            add(p, PI_ACTION, A_YES, IC_POWER, "Salir al menú Wii U", NULL);
            add(p, PI_ACTION, A_NO, IC_BACK, "Cancelar", NULL);
        } else if (p->ctx == CONF_DELCAT) {
            Category *c = cat_by_id(p->ctx2);
            snprintf(p->title, sizeof(p->title), tr("¿Eliminar «%s»?"), c ? c->name : "");
            str_copy(p->subtitle, sizeof(p->subtitle), "Los juegos no se borran, solo la categoría.");
            p->icon = IC_TRASH;
            add(p, PI_ACTION, A_NO, IC_BACK, "Cancelar", NULL);
            add(p, PI_ACTION, A_YES, IC_TRASH, "Eliminar categoría", NULL);
        } else {
            str_copy(p->title, sizeof(p->title), "¿Reiniciar estadísticas?");
            str_copy(p->subtitle, sizeof(p->subtitle), "Se borran las veces jugado y la última fecha guardada por Pixmo Games.");
            p->icon = IC_REFRESH;
            add(p, PI_ACTION, A_NO, IC_BACK, "Cancelar", NULL);
            add(p, PI_ACTION, A_YES, IC_REFRESH, "Reiniciar", NULL);
        }
        break;
    case P_HIDDEN: {
        str_copy(p->title, sizeof(p->title), "Juegos ocultos");
        str_copy(p->subtitle, sizeof(p->subtitle), "Desmarca un juego para que vuelva a aparecer");
        p->icon = IC_EYE_OFF;
        p->icon_color = 0x6C747D;
        int *ids = malloc(sizeof(int) * (size_t)(L.count + 1));
        int nh = 0;
        for (int i = 0; ids && i < L.count; i++)
            if (L.games[i].hidden)
                ids[nh++] = i;
        if (ids)
            qsort(ids, (size_t)nh, sizeof(int), cmp_by_name);
        for (int k = 0; k < nh && p->n < 77; k++)
            add(p, PI_CHECK, A_GAME_BASE + ids[k], -1, L.games[ids[k]].name1, NULL)->on = true;
        if (nh > 77) {
            snprintf(v, sizeof(v), tr("y %d más (se ven con «Mostrar juegos ocultos»)"), nh - 77);
            info(p, v, "");
        }
        if (nh == 0)
            info(p, "No hay juegos ocultos.",
                 "Para ocultar uno: el botón del ojo en su ficha, o X > Ocultar este juego");
        free(ids);
        break;
    }
    case P_WIIMODE: {
        // Wii and GameCube games boot into Wii mode: the same question the Wii U Menu asks
        Game *g = ctx_game(p);
        if (!g)
            break;
        str_copy(p->title, sizeof(p->title), "¿Cómo quieres jugar?");
        snprintf(p->subtitle, sizeof(p->subtitle), tr("Juego de %s · se inicia en modo Wii"), game_system_name(g));
        p->icon = IC_GAMEPAD;
        p->icon_color = 0x8A939D;
        if (g->drc_use == 65537) // meta.xml: playable with the GamePad
            add(p, PI_ACTION, A_WII_GAMEPAD, IC_GAMEPAD, "Con el Wii U GamePad", "control y pantalla");
        add(p, PI_ACTION, A_WII_TV, IC_IMAGE, "Con Wiimote, en la TV", NULL);
        add(p, PI_ACTION, A_WII_DRC, IC_EYE, "Con Wiimote, en el GamePad", "solo la pantalla");
        add(p, PI_ACTION, A_NO, IC_BACK, "Cancelar", NULL);
        int last = game_system(g) == SYS_GC ? L.set.gc_mode : L.set.wii_mode;
        int want = last == WII_SHOW_TV ? A_WII_TV : last == WII_SHOW_DRC ? A_WII_DRC : A_WII_GAMEPAD;
        for (int k = 0; k < p->n; k++)
            if (p->items[k].id == want)
                p->sel = k; // the way it was played last time
        break;
    }
    case P_DETAILS: {
        Game *g = ctx_game(p);
        if (!g)
            break;
        str_copy(p->title, sizeof(p->title), g->name1);
        str_copy(p->subtitle, sizeof(p->subtitle), "Información del juego");
        snprintf(v, sizeof(v), "%08X-%08X", (unsigned)(g->tid >> 32), (unsigned)(g->tid & 0xFFFFFFFF));
        info(p, "Title ID", v);
        info(p, "Código de producto", g->product[0] ? g->product : "—");
        if (g->tdb_shared)
            snprintf(v, sizeof(v), tr("%s (lo usan varios juegos)"), g->tdb_id);
        else
            str_copy(v, sizeof(v), g->tdb_id[0] ? g->tdb_id : "—");
        info(p, "ID de GameTDB", v);
        region_name(g->region, v, sizeof(v));
        info(p, "Región", v);
        info(p, "Ubicación", game_device_name(g));
        if (g->has_update)
            snprintf(v, sizeof(v), tr("v%u instalada"), g->update_version);
        else
            str_copy(v, sizeof(v), "No instalada");
        info(p, "Actualización", v);
        info(p, "Contenido descargable", g->has_dlc ? "Instalado" : "No");
        str_copy(v, sizeof(v), "GamePad");
        if (g->ext_flags & EXT_PRO) strncat(v, ", Pro Controller", sizeof(v) - strlen(v) - 1);
        if (g->ext_flags & EXT_NUNCHUK) strncat(v, tr(", Wiimote + Nunchuk"), sizeof(v) - strlen(v) - 1);
        if (g->ext_flags & EXT_CLASSIC) strncat(v, tr(", Mando Clásico"), sizeof(v) - strlen(v) - 1);
        if (g->ext_flags & EXT_BOARD) strncat(v, ", Balance Board", sizeof(v) - strlen(v) - 1);
        info(p, "Mandos compatibles", v);
        info(p, "Carátula", g->cover ? (g->dl_state == DL_OK ? "Descargada de GameTDB" : "Imagen de la SD")
                                     : "Generada con el arte del juego");
        if (g->save_time > 0) {
            format_date(g->save_time, v, sizeof(v));
            info(p, "Último guardado", v);
        }
        break;
    }
    case P_ABOUT:
        str_copy(p->title, sizeof(p->title), APP_NAME " " APP_VERSION);
        str_copy(p->subtitle, sizeof(p->subtitle), "Tu biblioteca de juegos de Wii U");
        p->icon = IC_GAMEPAD;
        snprintf(v, sizeof(v), "%d", L.count);
        info(p, "Juegos instalados", v);
        snprintf(v, sizeof(v), "%d", L.ncats);
        info(p, "Categorías", v);
        info(p, "Datos", "SD:/wiiu/apps/pixmogames/");
        info(p, "Carátulas propias", "covers/ID.jpg  (ej. ARPE01.jpg)");
        info(p, "Logos de categorías", "logos/*.png");
        info(p, "Carátulas en línea", "GameTDB · art.gametdb.com");
        info(p, "Hecho con", "SDL2, wut y Aroma");
        break;
    case P_HELP: {
        str_copy(p->title, sizeof(p->title), "Controles");
        p->icon = IC_GAMEPAD;
        static const struct { int b; const char *what; } rows[] = {
            { BTN_A + 1, "Jugar al juego seleccionado" },
            { BTN_X + 1, "Opciones del juego: categorías, ocultar..." },
            { BTN_Y + 1, "Marcar o quitar de Favoritos" },
            { BTN_L + 1, "Categoría anterior / siguiente (con R)" },
            { BTN_ZL + 1, "Saltar a la letra anterior / siguiente (con ZR)" },
            { BTN_UP + 1, "Ir a la barra de categorías" },
            { BTN_MINUS + 1, "Buscar por nombre o editor" },
            { BTN_PLUS + 1, "Ajustes" },
            { BTN_B + 1, "Atrás / quitar búsqueda / salir" },
            { 0, "Pantalla táctil: toca un juego para elegirlo y otra vez para jugar" },
        };
        for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
            PItem *it = add(p, PI_INFO, rows[i].b, -1, "", rows[i].what);
            (void)it;
        }
        break;
    }
    }
    if (keep >= p->n)
        keep = p->n - 1;
    if (keep < 0)
        keep = 0;
    p->sel = keep;
    // never rest on a non-selectable row
    if (p->n && p->items[p->sel].kind == PI_INFO) {
        for (int i = 0; i < p->n; i++)
            if (p->items[i].kind != PI_INFO) {
                p->sel = i;
                break;
            }
    }
}

void panels_rebuild(void)
{
    for (int i = 0; i < npanels; i++)
        build(&panels[i]);
}

void panel_open(int type, int ctx)
{
    if (npanels >= 8)
        return;
    Panel *p = &panels[npanels++];
    memset(p, 0, sizeof(*p));
    p->type = type;
    p->ctx = ctx;
    p->anim = 0;
    build(p);
}

static void panel_open2(int type, int ctx, int ctx2)
{
    panel_open(type, ctx);
    panels[npanels - 1].ctx2 = ctx2;
    build(&panels[npanels - 1]);
    if (type == P_CONFIRM && ctx == CONF_EXIT)
        panels[npanels - 1].sel = 0;
}

static int top_index(void)
{
    for (int i = npanels - 1; i >= 0; i--)
        if (!panels[i].closing)
            return i;
    return -1;
}

void panel_close_top(void)
{
    int t = top_index();
    if (t >= 0)
        panels[t].closing = true;
}

void panels_close_all(void)
{
    for (int i = 0; i < npanels; i++)
        panels[i].closing = true;
}

static bool all_info(const Panel *p)
{
    for (int i = 0; i < p->n; i++)
        if (p->items[i].kind != PI_INFO)
            return false;
    return true;
}

// ------------------------------------------------------------ actions

static void sanitize_name(const char *in, char *out, size_t n)
{
    char tmp[128];
    str_copy(tmp, sizeof(tmp), in);
    for (char *c = tmp; *c; c++)
        if (*c == '\n' || *c == '\r' || *c == '\t')
            *c = ' ';
    str_copy(out, n < 41 ? n : 41, str_trim(tmp));
}

static uint32_t next_color(void)
{
    return k_palette[(L.ncats * 5 + 1) % 14];
}

void panel_text_result(int target, int ctx, const char *text)
{
    char name[64];
    sanitize_name(text, name, sizeof(name));
    if (!name[0])
        return;
    if (target == TT_NEW_CAT || target == TT_NEW_CAT_ASSIGN) {
        Category *c = cat_create(name, (L.ncats * 3 + 1) % IC_CATEGORY_COUNT, next_color());
        if (!c) {
            audio_play(SFX_ERROR);
            ui_toast("No se pueden crear más categorías", IC_INFO);
            return;
        }
        int id = c->id;
        if (target == TT_NEW_CAT_ASSIGN && ctx >= 0 && ctx < L.count)
            L.games[ctx].catmask |= 1ull << id;
        audio_play(SFX_SELECT);
        ui_toast("Categoría creada", IC_TAG);
        tabs_rebuild();
        L.list_dirty = true;
        panels_rebuild();
        if (target == TT_NEW_CAT) {
            // go straight to the editor so the user can pick an icon or logo
            panel_open(P_CATEDIT, id);
        }
        lib_save_soon();
    } else if (target == TT_RENAME_CAT) {
        Category *c = cat_by_id(ctx);
        if (c) {
            str_copy(c->name, sizeof(c->name), name);
            tabs_rebuild();
            panels_rebuild();
            lib_save_soon();
            audio_play(SFX_SELECT);
        }
    }
}

static void cycle_choice(Panel *p, PItem *it, int dir)
{
    switch (it->id) {
    case A_VIEW:
        L.set.view = (L.set.view + dir + 3) % 3;
        ui.scroll = L.set.view == 1 ? (float)(ui.sel / 5) : (float)ui.sel;
        break;
    case A_SORT:
        L.set.sort = (L.set.sort + dir + 4) % 4;
        L.list_dirty = true;
        break;
    case A_THEME:
        L.set.theme = (L.set.theme + dir + 2) % 2;
        break;
    case A_START:
        L.set.start_tab = (L.set.start_tab + dir + 3) % 3;
        break;
    case A_LANG:
        L.set.lang = (L.set.lang + dir + 3) % 3;
        i18n_set(L.set.lang);
        tabs_rebuild();
        break;
    case A_COLOR: {
        Category *c = cat_by_id(p->ctx);
        if (!c)
            return;
        int cur = 0;
        for (int i = 0; i < 14; i++)
            if (k_palette[i] == c->color)
                cur = i;
        c->color = k_palette[(cur + dir + 14) % 14];
        tabs_rebuild();
        break;
    }
    default:
        return;
    }
    audio_play(SFX_TAB);
    lib_save_soon();
    panels_rebuild();
}

static void set_toggle(PItem *it, bool on)
{
    bool *t = NULL;
    switch (it->id) {
    case A_BGART: t = &L.set.bg_art; break;
    case A_DOWNLOAD: t = &L.set.download; break;
    case A_SHOWHIDDEN: t = &L.set.show_hidden; break;
    case A_SFX: t = &L.set.sfx; break;
    case A_MUSIC: t = &L.set.music; break;
    case A_CLOCK: t = &L.set.show_clock; break;
    case A_READTITLES: t = &L.set.read_titles; break;
    case A_AUTOBOOT: t = &L.set.al_encender; break;
    case A_RETURN: t = &L.set.volver; break;
    default: return;
    }
    if (*t == on)
        return;
    *t = on;
    it->on = on;
    audio_set(L.set.sfx, L.set.music);
    audio_play(on ? SFX_TOGGLE_ON : SFX_TOGGLE_OFF);
    if (it->id == A_SHOWHIDDEN)
        L.list_dirty = true;
    if (it->id == A_DOWNLOAD && on)
        L.net_failed = false;
    if (it->id == A_READTITLES && on)
        lib_reload_title_art();
    lib_save_soon();
}

static void activate(Panel *p, PItem *it)
{
    if (it->disabled)
        return;
    if (it->kind == PI_TOGGLE) {
        set_toggle(it, !it->on);
        return;
    }
    if (it->kind == PI_CHOICE) {
        if (it->id == A_COLOR) {
            audio_play(SFX_OPEN);
            panel_open(P_COLORPICK, p->ctx);
            return;
        }
        cycle_choice(p, it, 1);
        return;
    }
    Game *g = ctx_game(p);
    if (p->type == P_ASSIGN && it->id >= A_CAT_BASE && g) {
        int id = it->id - A_CAT_BASE;
        g->catmask ^= 1ull << id;
        it->on = (g->catmask >> id) & 1;
        audio_play(it->on ? SFX_TOGGLE_ON : SFX_TOGGLE_OFF);
        L.list_dirty = true;
        lib_save_soon();
        return;
    }
    if (p->type == P_HIDDEN && it->id >= A_GAME_BASE) {
        int gi = it->id - A_GAME_BASE;
        if (gi < L.count) {
            Game *hg = &L.games[gi];
            hg->hidden = !hg->hidden;
            it->on = hg->hidden; // stays in the list: tick it again to undo
            audio_play(it->on ? SFX_TOGGLE_OFF : SFX_TOGGLE_ON);
            L.list_dirty = true;
            lib_save_soon();
        }
        return;
    }
    if (p->type == P_CATMAN && it->id >= A_CAT_BASE) {
        audio_play(SFX_SELECT);
        panel_open(P_CATEDIT, it->id - A_CAT_BASE);
        return;
    }
    if (p->type == P_ICONPICK || p->type == P_COLORPICK) {
        Category *c = cat_by_id(p->ctx);
        if (c) {
            if (p->type == P_ICONPICK) {
                c->icon = it->id;
                if (c->logo[0]) {
                    // choosing an icon replaces the custom logo
                    c->logo[0] = 0;
                    cat_load_logo(c);
                }
            } else {
                c->color = k_palette[it->id];
            }
            tabs_rebuild();
            lib_save_soon();
        }
        audio_play(SFX_SELECT);
        panel_close_top();
        return;
    }
    if (p->type == P_LOGOPICK) {
        Category *c = cat_by_id(p->ctx);
        if (!c || it->kind == PI_INFO)
            return;
        if (it->id == A_NOLOGO)
            c->logo[0] = 0;
        else if (it->id >= A_LOGO_BASE && it->id - A_LOGO_BASE < s_nlogos)
            str_copy(c->logo, sizeof(c->logo), s_logos[it->id - A_LOGO_BASE]);
        cat_load_logo(c);
        tabs_rebuild();
        lib_save_soon();
        audio_play(SFX_SELECT);
        panel_close_top();
        return;
    }
    switch (it->id) {
    case A_CATMAN: audio_play(SFX_SELECT); panel_open(P_CATMAN, 0); break;
    case A_RESCAN:
        audio_play(SFX_SELECT);
        panels_close_all();
        lib_rescan();
        ui_toast("Leyendo la biblioteca de nuevo...", IC_REFRESH);
        break;
    case A_HELP: audio_play(SFX_SELECT); panel_open(P_HELP, 0); break;
    case A_ABOUT: audio_play(SFX_SELECT); panel_open(P_ABOUT, 0); break;
    case A_EXIT: audio_play(SFX_SELECT); panel_open2(P_CONFIRM, CONF_EXIT, 0); break;
    case A_PLAY:
        if (g)
            start_launch(p->ctx);
        break;
    case A_ASSIGN: audio_play(SFX_SELECT); panel_open(P_ASSIGN, p->ctx); break;
    case A_FAV:
        if (g) {
            g->favorite = !g->favorite;
            audio_play(g->favorite ? SFX_FAV : SFX_TOGGLE_OFF);
            ui_toast(g->favorite ? "Añadido a Favoritos" : "Quitado de Favoritos",
                     g->favorite ? IC_STAR : IC_STAR_OUTLINE);
            L.list_dirty = true;
            lib_save_soon();
            panels_rebuild();
        }
        break;
    case A_HIDE:
        if (g) {
            game_toggle_hidden(g);
            panel_close_top();
        }
        break;
    case A_HIDDENMAN: audio_play(SFX_SELECT); panel_open(P_HIDDEN, 0); break;
    case A_RECOVER:
        if (g) {
            game_retry_cover(g);
            audio_play(SFX_SELECT);
            ui_toast("Buscando carátula...", IC_DOWNLOAD);
            panel_close_top();
        }
        break;
    case A_RESET: audio_play(SFX_SELECT); panel_open2(P_CONFIRM, CONF_RESET, p->ctx); break;
    case A_DETAILS: audio_play(SFX_SELECT); panel_open(P_DETAILS, p->ctx); break;
    case A_NEWCAT:
        audio_play(SFX_SELECT);
        request_text(p->type == P_ASSIGN ? TT_NEW_CAT_ASSIGN : TT_NEW_CAT, p->ctx,
                     "Nombre de la categoría (ej. Juegos de disparos)", "", "Crear");
        break;
    case A_NAME: {
        Category *c = cat_by_id(p->ctx);
        audio_play(SFX_SELECT);
        request_text(TT_RENAME_CAT, p->ctx, "Nombre de la categoría", c ? c->name : "", "Aceptar");
        break;
    }
    case A_ICON: audio_play(SFX_OPEN); panel_open(P_ICONPICK, p->ctx); break;
    case A_LOGO: audio_play(SFX_OPEN); panel_open(P_LOGOPICK, p->ctx); break;
    case A_DELCAT: audio_play(SFX_SELECT); panel_open2(P_CONFIRM, CONF_DELCAT, p->ctx); break;
    case A_NO: audio_play(SFX_BACK); panel_close_top(); break;
    case A_WII_GAMEPAD:
    case A_WII_TV:
    case A_WII_DRC: {
        int mode = it->id == A_WII_GAMEPAD ? WII_SHOW_GAMEPAD : it->id == A_WII_TV ? WII_SHOW_TV : WII_SHOW_DRC;
        int gi = p->ctx; // p is gone once the panels close
        if (gi >= 0 && gi < L.count && game_system(&L.games[gi]) == SYS_GC)
            L.set.gc_mode = mode;
        else
            L.set.wii_mode = mode;
        lib_save_soon();
        audio_play(SFX_SELECT);
        panels_close_all();
        start_launch_wii(gi, mode);
        break;
    }
    case A_YES:
        if (p->ctx == CONF_EXIT) {
            audio_play(SFX_BACK);
            ui.quit = true;
        } else if (p->ctx == CONF_DELCAT) {
            cat_delete(p->ctx2);
            audio_play(SFX_BACK);
            ui_toast("Categoría eliminada", IC_TRASH);
            tabs_rebuild();
            // close the confirmation and the editor of the deleted category
            for (int i = 0; i < npanels; i++)
                if ((panels[i].type == P_CATEDIT && panels[i].ctx == p->ctx2) || &panels[i] == p)
                    panels[i].closing = true;
            panels_rebuild();
        } else if (p->ctx == CONF_RESET) {
            if (p->ctx2 >= 0 && p->ctx2 < L.count)
                game_reset_stats(&L.games[p->ctx2]);
            audio_play(SFX_SELECT);
            ui_toast("Estadísticas reiniciadas", IC_REFRESH);
            panel_close_top();
        }
        break;
    }
}

// ------------------------------------------------------------ input

static void move_sel(Panel *p, int dir)
{
    if (all_info(p)) {
        p->scroll = clampf(p->scroll + dir, 0, fmaxf(0, p->n - 7.0f));
        return;
    }
    int s = p->sel;
    for (int i = 0; i < p->n; i++) {
        s += dir;
        if (s < 0 || s >= p->n)
            return;
        if (p->items[s].kind != PI_INFO)
            break;
    }
    if (s != p->sel && s >= 0 && s < p->n && p->items[s].kind != PI_INFO) {
        p->sel = s;
        audio_play(SFX_MOVE);
    }
}

bool panels_input(Button b)
{
    if (npanels == 0)
        return false;
    int ti = top_index();
    if (ti < 0)
        return true;
    Panel *p = &panels[ti];
    PItem *it = p->n ? &p->items[p->sel] : NULL;
    if (p->cols > 0) {
        int s = p->sel;
        switch (b) {
        case BTN_LEFT: s--; break;
        case BTN_RIGHT: s++; break;
        case BTN_UP: s -= p->cols; break;
        case BTN_DOWN: s += p->cols; break;
        default: break;
        }
        if (s != p->sel && s >= 0 && s < p->n) {
            p->sel = s;
            audio_play(SFX_MOVE);
            return true;
        }
    }
    switch (b) {
    case BTN_UP:
        if (p->cols == 0)
            move_sel(p, -1);
        break;
    case BTN_DOWN:
        if (p->cols == 0)
            move_sel(p, 1);
        break;
    case BTN_LEFT:
    case BTN_RIGHT:
        if (it && p->cols == 0) {
            if (it->kind == PI_CHOICE)
                cycle_choice(p, it, b == BTN_RIGHT ? 1 : -1);
            else if (it->kind == PI_TOGGLE)
                set_toggle(it, b == BTN_RIGHT);
        }
        break;
    case BTN_A:
        if (it && it->kind != PI_INFO)
            activate(p, it);
        break;
    case BTN_B:
        audio_play(SFX_BACK);
        panel_close_top();
        break;
    case BTN_PLUS:
        if (p->type == P_OPTIONS || panels[0].type == P_OPTIONS) {
            audio_play(SFX_BACK);
            panels_close_all();
        }
        break;
    case BTN_Y:
        if (p->type == P_CATMAN && L.ncats < MAX_CATEGORIES) {
            audio_play(SFX_SELECT);
            request_text(TT_NEW_CAT, 0, "Nombre de la categoría (ej. Juegos de disparos)", "", "Crear");
        }
        break;
    case BTN_X:
        if (p->type == P_CATMAN && it && it->id >= A_CAT_BASE) {
            audio_play(SFX_SELECT);
            panel_open2(P_CONFIRM, CONF_DELCAT, it->id - A_CAT_BASE);
        }
        break;
    case BTN_L:
    case BTN_R:
        if (p->type == P_CATMAN && it && it->id >= A_CAT_BASE) {
            int dir = b == BTN_L ? -1 : 1;
            int s = p->sel + dir;
            if (s >= 0 && s < L.ncats) {
                cat_move(it->id - A_CAT_BASE, dir);
                p->sel = s;
                tabs_rebuild();
                build(p);
                audio_play(SFX_TAB);
            }
        }
        break;
    default:
        break;
    }
    return true;
}

void panel_touch_item(int idx)
{
    int ti = top_index();
    if (ti < 0)
        return;
    Panel *p = &panels[ti];
    if (idx < 0 || idx >= p->n || p->items[idx].kind == PI_INFO)
        return;
    p->sel = idx;
    activate(p, &p->items[idx]);
}

void panel_touch_outside(void)
{
    audio_play(SFX_BACK);
    panel_close_top();
}

// ------------------------------------------------------------ update & render

#define ROW_H 62.0f
#define MAX_ROWS 7

void panels_update(float dt)
{
    for (int i = 0; i < npanels; i++) {
        Panel *p = &panels[i];
        if (p->closing) {
            p->anim -= dt / 0.16f;
            if (p->anim <= 0) {
                // remove this panel
                memmove(&panels[i], &panels[i + 1], sizeof(Panel) * (npanels - i - 1));
                npanels--;
                i--;
                if (npanels > 0)
                    panels_rebuild();
                continue;
            }
        } else if (p->anim < 1) {
            p->anim = fminf(1, p->anim + dt / 0.2f);
        }
        if (p->cols == 0 && !(ui.dragging && i == top_index())) {
            float target = p->scroll;
            if (!all_info(p)) {
                if (p->sel < target + 0.5f)
                    target = p->sel - 0.5f;
                if (p->sel > target + MAX_ROWS - 1.5f)
                    target = p->sel - MAX_ROWS + 1.5f;
            }
            target = clampf(target, 0, fmaxf(0, p->n - (float)MAX_ROWS));
            p->scroll = approach(p->scroll, target, 14, dt);
        }
        for (int k = 0; k < p->n; k++) {
            PItem *it = &p->items[k];
            float tgt = k == p->sel && i == top_index() ? 1.0f : 0.0f;
            it->anim = approach(it->anim, tgt, 20, dt);
        }
    }
}

static void render_header_icon(const Panel *p, float x, float y, float s)
{
    if (p->type == P_GAME || p->type == P_DETAILS) {
        Game *g = ctx_game(p);
        if (g && g->icon) {
            gfx_shadow(x, y + 2, s, s, s * 0.18f, 8, col_alpha(T.shadow, 0.6f));
            gfx_image(g->icon, NULL, x, y, s, s, s * 0.18f, rgb(0xFFFFFF));
            return;
        }
    }
    if (p->type == P_CATEDIT) {
        Category *c = cat_by_id(p->ctx);
        if (c) {
            draw_badge(c->icon, c->color, c->logo_tex, x, y, s);
            return;
        }
    }
    if (p->type == P_ABOUT) {
        brand_draw_icon(x, y, s, 1);
        return;
    }
    if (p->icon >= 0)
        draw_badge(p->icon, p->icon_color, NULL, x, y, s);
}

static void render_item(Panel *p, int k, float x, float y, float w, float h, bool top)
{
    PItem *it = &p->items[k];
    float sel = top ? it->anim : 0;
    if (it->kind != PI_INFO) {
        if (sel > 0.01f) {
            gfx_rrect(x, y, w, h, 14, col_alpha(T.sel_fill, sel));
            draw_sel_frame(x, y, w, h, 14, sel);
        }
        if (top)
            hit_add(x, y, w, h, HIT_PANEL_ITEM, k);
    }
    float cy = y + h * 0.5f;
    float tx = x + 18;
    if (p->type == P_HELP) {
        if (it->id > 0) {
            Button b = (Button)(it->id - 1);
            float gw = draw_glyph(b, tx, cy, 28);
            if (b == BTN_L)
                draw_glyph(BTN_R, tx + gw + 4, cy, 28);
            if (b == BTN_ZL)
                draw_glyph(BTN_ZR, tx + gw + 4, cy, 28);
        } else {
            icon_draw(IC_GAMEPAD, tx, cy - 14, 28, T.text_dim);
        }
        text_draw(20, it->value, x + 108, cy - 20 * 0.62f, T.text, ALIGN_LEFT);
        return;
    }
    if (it->badge) {
        draw_badge(it->icon, it->color, it->logo, tx, cy - 19, 38);
        tx += 52;
    } else if (it->icon >= 0) {
        Color ic = it->id == A_DELCAT || (p->type == P_CONFIRM && it->id == A_YES && p->ctx == CONF_DELCAT)
                       ? rgb(0xE5533D)
                       : T.accent;
        icon_draw(it->icon, tx + 2, cy - 14, 28, ic);
        tx += 50;
    }
    Color lc = it->kind == PI_INFO ? T.text_dim : T.text;
    if (it->id == A_DELCAT)
        lc = rgb(0xD84432);
    float right = x + w - 20;
    char buf[160];
    if (it->kind == PI_INFO) {
        text_draw(19, it->label, tx, cy - 19 * 0.62f, T.text_dim, ALIGN_LEFT);
        text_fit(20, it->value, w * 0.56f, buf, sizeof(buf));
        text_draw(20, buf, right, cy - 20 * 0.62f, T.text, ALIGN_RIGHT);
        return;
    }
    text_fit(22, it->label, right - tx - 200, buf, sizeof(buf));
    text_draw(22, buf, tx, cy - 22 * 0.62f, lc, ALIGN_LEFT);
    switch (it->kind) {
    case PI_TOGGLE:
        draw_toggle(right - 60, cy - 16, it->on, 1);
        break;
    case PI_CHECK:
        draw_check(right - 32, cy - 16, 32, it->on);
        break;
    case PI_CHOICE: {
        if (it->id == A_COLOR) {
            Category *c = cat_by_id(p->ctx);
            float sx = right - 70;
            gfx_circle(sx + 22, cy, 15, rgb(c ? c->color : 0x999999));
            gfx_ring(sx + 22, cy, 15, 2, rgba(0xFFFFFF, 0.8f));
            text_draw(22, "‹", sx - 6, cy - 22 * 0.62f, T.text_faint, ALIGN_CENTER);
            text_draw(22, "›", sx + 50, cy - 22 * 0.62f, T.text_faint, ALIGN_CENTER);
        } else {
            float vw = text_width(20, it->value);
            text_draw(20, it->value, right - 22, cy - 20 * 0.62f, T.accent, ALIGN_RIGHT);
            text_draw(22, "‹", right - 22 - vw - 16, cy - 22 * 0.62f, T.text_faint, ALIGN_CENTER);
            text_draw(22, "›", right - 6, cy - 22 * 0.62f, T.text_faint, ALIGN_CENTER);
        }
        break;
    }
    default:
        if (it->value[0]) {
            text_fit(20, it->value, 230, buf, sizeof(buf));
            text_draw(20, buf, right - 24, cy - 20 * 0.62f, T.text_dim, ALIGN_RIGHT);
        }
        text_draw(22, "›", right - 4, cy - 22 * 0.62f, T.text_faint, ALIGN_CENTER);
        break;
    }
}

static void render_grid_items(Panel *p, float x, float y, float w, bool top)
{
    float cell = (w - 40) / p->cols;
    for (int k = 0; k < p->n; k++) {
        PItem *it = &p->items[k];
        int r = k / p->cols, c = k % p->cols;
        float cx = x + 20 + c * cell, cy = y + r * cell;
        float s = cell * 0.72f;
        float bx = cx + (cell - s) * 0.5f, by = cy + (cell - s) * 0.5f - (p->type == P_ICONPICK ? 6 : 0);
        float sel = top ? it->anim : 0;
        float grow = 1 + 0.08f * sel;
        float gs = s * grow;
        float gx = bx - (gs - s) * 0.5f, gy = by - (gs - s) * 0.5f;
        draw_badge(it->icon, it->color, NULL, gx, gy, gs);
        if (it->on)
            gfx_rrect_outline(gx - 4, gy - 4, gs + 8, gs + 8, gs * 0.26f + 4, 3, T.text);
        draw_sel_frame(gx, gy, gs, gs, gs * 0.26f, sel);
        if (p->type == P_ICONPICK && sel > 0.5f)
            text_draw(15, it->label, cx + cell * 0.5f, cy + cell - 18, T.text_dim, ALIGN_CENTER);
        if (top)
            hit_add(cx, cy, cell, cell, HIT_PANEL_ITEM, k);
    }
}

static void render_panel(Panel *p, bool top, int depth)
{
    float a = ease_out_cubic(p->anim);
    if (a <= 0.01f)
        return;
    bool has_sub = p->subtitle[0] != 0;
    float head = has_sub ? 96 : 80, foot = 64;
    float body;
    int rows = 0;
    if (p->cols > 0) {
        rows = (p->n + p->cols - 1) / p->cols;
        body = rows * ((p->w - 40) / p->cols) + 16;
    } else {
        rows = p->n < MAX_ROWS ? p->n : MAX_ROWS;
        body = rows * ROW_H + 20;
    }
    float w = p->w, h = head + body + foot;
    if (h > 660) h = 660;
    float x = (SCREEN_W - w) * 0.5f + depth * 14, y = (SCREEN_H - h) * 0.5f + (1 - a) * 46 + depth * 10;
    gfx_push_alpha(a);
    gfx_shadow(x, y + 8, w, h, 24, 40, T.shadow);
    gfx_rrect(x, y, w, h, 24, T.panel);
    if (top)
        hit_add(x, y, w, h, HIT_NONE, 0);
    // header band
    gfx_clip((int)x, (int)y, (int)w + 1, (int)head);
    gfx_rrect_vgrad(x, y, w, h, 24, T.panel_head, T.panel);
    gfx_unclip();
    gfx_rect(x, y + head - 1, w, 1, T.row_line);
    render_header_icon(p, x + 24, y + (head - 52) * 0.5f, 52);
    char buf[160];
    text_fit(28, p->title, w - 120, buf, sizeof(buf));
    float ty = has_sub ? y + 18 : y + head * 0.5f - 28 * 0.62f;
    text_draw(28, buf, x + 92, ty, T.text, ALIGN_LEFT);
    if (has_sub) {
        text_fit(18, p->subtitle, w - 120, buf, sizeof(buf));
        text_draw(18, buf, x + 92, y + 56, T.text_dim, ALIGN_LEFT);
    }
    // body
    float by = y + head + 10;
    gfx_clip((int)x, (int)(y + head), (int)w, (int)(h - head - foot));
    if (p->cols > 0) {
        render_grid_items(p, x, by, w, top);
    } else {
        for (int k = 0; k < p->n; k++) {
            float ry = by + (k - p->scroll) * ROW_H;
            if (ry + ROW_H < y + head || ry > y + h - foot)
                continue;
            if (k > 0 && p->items[k].kind == PI_INFO)
                gfx_rect(x + 24, ry - 1, w - 48, 1, col_alpha(T.row_line, 0.8f));
            render_item(p, k, x + 14, ry + 3, w - 28, ROW_H - 6, top);
        }
    }
    gfx_unclip();
    // scroll indicator
    if (p->cols == 0 && p->n > MAX_ROWS) {
        float track = body - 24, frac = (float)MAX_ROWS / p->n;
        float th = track * frac, tyy = by + (track - th) * clampf(p->scroll / (p->n - MAX_ROWS), 0, 1);
        gfx_rrect(x + w - 10, by, 4, track, 2, col_alpha(T.text_faint, 0.3f));
        gfx_rrect(x + w - 10, tyy, 4, th, 2, col_alpha(T.text_dim, 0.8f));
    }
    // footer hints
    float fy = y + h - foot;
    gfx_rect(x, fy, w, 1, T.row_line);
    if (top) {
        Button bt[6];
        const char *lb[6];
        int n = 0;
        bt[n] = BTN_B; lb[n++] = "Atrás";
        if (p->type == P_CATMAN) {
            bt[n] = BTN_A; lb[n++] = "Editar";
            bt[n] = BTN_Y; lb[n++] = "Nueva";
            bt[n] = BTN_X; lb[n++] = "Borrar";
            bt[n] = BTN_L; lb[n++] = "";
            bt[n] = BTN_R; lb[n++] = "Mover";
        } else if (!all_info(p)) {
            PItem *it = p->n ? &p->items[p->sel] : NULL;
            bt[n] = BTN_A;
            lb[n++] = it && (it->kind == PI_TOGGLE || it->kind == PI_CHECK) ? "Cambiar" : "Aceptar";
            if (it && it->kind == PI_CHOICE) {
                bt[n] = BTN_LEFT; lb[n++] = "Cambiar";
            }
        }
        draw_hints(bt, lb, n, x + w - 28, fy + foot * 0.5f, ALIGN_RIGHT);
    }
    gfx_pop_alpha();
}

void panels_render(void)
{
    if (npanels == 0)
        return;
    float dim = 0;
    for (int i = 0; i < npanels; i++)
        dim = fmaxf(dim, ease_out_cubic(panels[i].anim));
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, col_alpha(T.dim, dim));
    hit_add(0, 0, SCREEN_W, SCREEN_H, HIT_PANEL_BG, 0);
    int ti = top_index();
    for (int i = 0; i < npanels; i++) {
        bool top = i == ti;
        render_panel(&panels[i], top, 0);
        if (i + 1 < npanels) {
            // panels below the top one are shown slightly dimmed
            gfx_rect(0, 0, SCREEN_W, SCREEN_H, col_alpha(T.dim, 0.35f * ease_out_cubic(panels[i + 1].anim)));
        }
    }
}

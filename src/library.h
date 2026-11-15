// Biblioteca: juegos instalados, metadatos, estadísticas, categorías y
// carga asíncrona de iconos, carátulas e imágenes de arranque.
#pragma once

#include "common.h"
#include "platform.h"

#define MAX_CATEGORIES 48

enum { ASSET_NONE = 0, ASSET_LOADING, ASSET_READY, ASSET_FAILED };
enum { COVER_SRC_NONE = 0, COVER_SRC_FILE, COVER_SRC_DOWNLOADED };
enum { DL_NONE = 0, DL_QUEUED, DL_OK, DL_NOT_FOUND, DL_FAILED };

typedef struct Game {
    PlatTitle pt;
    uint64_t tid;

    // metadata (from meta.xml / ACP, cached on the SD card)
    bool meta_ok;
    char name[256];        // display name; may contain '\n'
    char name1[256];       // single line
    char sortkey[96];      // folded name for sorting and search
    char publisher[128];
    char product[32];      // WUP-P-ARPE
    char company[8];       // 0001
    char tdb_id[8];        // ARPE01 (GameTDB id)
    bool tdb_shared;       // several installed titles use this id (injects): no online cover
    uint32_t region, ext_flags, drc_use;
    uint16_t update_version;
    bool has_update, has_dlc;

    // statistics (persistent)
    int64_t last_played;   // launched from this app
    int64_t save_time;     // newest file in the save folder
    int64_t first_seen;
    uint32_t play_count;
    bool favorite, hidden;
    bool fresh;            // not in config.txt yet: first time this title is seen
    uint64_t catmask;      // bit = category id

    // assets (owned by the main thread)
    int icon_state, cover_state, boot_state, save_state;
    SDL_Texture *icon, *cover, *boot, *boot_blur, *boot_full;
    int boot_full_state;
    int cover_src, dl_state;
    int64_t dl_checked;    // last time GameTDB said "no cover"
    uint32_t cover_used, boot_used;
    Color tint;            // vivid color from the art (for accents)
    bool tint_ok;

    // Optional game details (fichas.txt on the SD card, format in docs/DESARROLLO.md)
    bool ficha;
    char year[8];
    uint8_t players;       // max players (0 = unknown)
    uint8_t rating10;      // community rating x10, 0..50 (0 = unknown)
    char genres[96];       // "Acción, Aventura"
    char lb_pub[96], lb_dev[96];
    char synopsis[400];
    char lb_platform[48];  // platform from fichas.txt ("Nintendo GameCube")

    // "Cine" view art: fondos/<TID>.jpg (1280x720) and titulos/<TID>.png (clear logo)
    SDL_Texture *fondo, *title_logo;
    SDL_Texture *title_glow; // dark logos only: blurred white silhouette (half size, 10 px margin)
    int fondo_state, title_state;
    uint32_t fondo_used, title_used;

    // small cover for the rail and as a stand-in while the big one loads; every game's
    // is read at start from miniaturas.bin (made from the covers once, then kept)
    SDL_Texture *thumb;
    uint16_t thumb_w, thumb_h;
    uint16_t *thumb_px;    // RGB565 pixels, kept to rewrite miniaturas.bin
    uint32_t thumb_stamp;  // size of the cover file it was made from
    bool thumb_tried;      // a thumbnail was already attempted this session

    // interface animation state
    float anim_sel, anim_new;
} Game;

typedef struct Category {
    int id;                // 0..63, stable
    char name[64];
    int icon;              // IconId
    uint32_t color;        // 0xRRGGBB
    char logo[64];         // custom PNG in logos/ ("" = use icon)
    SDL_Texture *logo_tex;
    int logo_state;
} Category;

typedef struct Settings {
    int view;              // 0 showcase, 1 grid, 2 cine (full-screen art + 3D case)
    int sort;              // 0 name, 1 last played, 2 most played, 3 publisher
    int theme;             // 0 light, 1 dark
    bool sfx, music, bg_art, download, show_hidden, show_clock;
    bool read_titles;      // read icons/boot images/saves from the console storage
    int start_tab;         // 0 all, 1 recent, 2 last used
    int last_tab;          // index into the tab list
    uint64_t last_tid;
    // last way a Wii game / a GameCube game was played (WII_SHOW_*), offered first next
    // time. Separate: GameCube injects run Nintendont, usually with the GamePad, while
    // many Wii injects only work with a Wii Remote (one choice must not leak to the other)
    int wii_mode, gc_mode;
    // read by the Aroma plugin pixmo_inicio.wps (plugin/): open Pixmo Games when the
    // console starts, and come back here when a game started from Pixmo Games closes
    bool al_encender, volver;
    int lang;              // LANG_* (i18n.h): 0 automatic (console language), 1 Spanish, 2 English
} Settings;

typedef struct Library {
    Game *games;
    int count;
    Category cats[MAX_CATEGORIES];
    int ncats;
    Settings set;
    bool loading_list;
    int meta_pending;
    int downloads_total, downloads_done;
    bool net_failed;
    bool safe_mode;        // previous session did not end cleanly: no downloads/save scans
    bool list_dirty;       // names/stats changed: views must be rebuilt
    bool first_run;
    uint32_t save_at;      // SDL ticks when config.txt should be written (0 = clean)
    char status[128];      // short message shown in the top bar
    uint32_t status_until;
} Library;

void lib_save_soon(void);
void lib_status(const char *msg, uint32_t ms);

extern Library L;

void lib_init(void);
void lib_shutdown(void);
// Per-frame: applies finished background jobs, uploads textures, evicts LRU.
// `want` = indices (into L.games) whose covers should be loaded, nearest first;
// the first `nbig` of them also get the large art (boot screen, and in the
// cine view the background and the title logo).
void lib_update(const int *want, int nwant, int nbig, int focus_game);
void lib_request_icons(const int *idx, int n);  // visible icons first
void lib_request_boot_full(int idx);            // 1280x720 boot screen (launch)
void lib_save(void);           // config.txt (settings, categories, stats)
void lib_save_cache(void);     // cache.txt (metadata)
void lib_rescan(void);

Game *lib_find(uint64_t tid);
int64_t game_last_time(const Game *g);

// The system a game is for: Wii U, or the console of a Virtual Console title or
// of an injected game (Wii and GameCube injects run in Wii mode).
enum { SYS_WIIU, SYS_WII, SYS_GC, SYS_N64, SYS_DS, SYS_NES, SYS_SNES, SYS_GBA, SYS_HOMEBREW, SYS_OTHER };
int game_system(const Game *g);
const char *game_system_name(const Game *g);  // "Nintendo 64", "GameCube"; "" for Wii U
const char *game_system_short(const Game *g); // "N64", "Wii", "GC": label of the box band
const char *game_device_name(const Game *g);
void game_launched(Game *g);
void game_retry_cover(Game *g);
void game_reset_stats(Game *g);
void lib_reload_title_art(void);   // after enabling read_titles again
int  lib_downloads_active(void);   // covers being downloaded from the internet right now
bool lib_thumbs_ready(void);
void lib_stop_for_launch(void);     // stop reading the console before a game starts
void lib_resume_after_launch(void);  // ...and go on if it did not start       // every saved thumbnail is a texture (the rail can show its final layout)

// categories
Category *cat_by_id(int id);
Category *cat_create(const char *name, int icon, uint32_t color);
void cat_delete(int id);
void cat_move(int id, int delta);
int  cat_count_games(int id);
void cat_load_logo(Category *c);
// custom logos available on the SD card (logos/*.png)
int  logos_list(char names[][64], int max);

const char *data_path(const char *rel, char *buf, size_t n);

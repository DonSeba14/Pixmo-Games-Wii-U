// Capa de plataforma: Wii U (platform_wiiu.c) o PC de pruebas (platform_pc.c).
#pragma once

#include "common.h"

// Title types returned by plat_list_titles()
enum {
    TITLE_GAME = 0,
    TITLE_UPDATE = 1,
    TITLE_DLC = 2,
    TITLE_OTHER = 3,
};

typedef struct PlatTitle {
    uint64_t tid;
    int kind;            // TITLE_*
    uint16_t version;
    char path[128];      // e.g. /vol/storage_mlc01/usr/title/00050000/101c9500
    char device[8];      // "mlc", "usb", "odd"
    bool wii;            // Wii title for the Wii U (eShop or injected Wii/GameCube game): runs in Wii mode
    bool homebrew;       // .wuhb bundle on the SD card; path = "wiiu/apps/..." (relative to the SD root)
} PlatTitle;

// Extra hardware supported by a title (meta.xml ext_dev_* flags)
enum {
    EXT_NUNCHUK = 1 << 0,
    EXT_CLASSIC = 1 << 1,
    EXT_PRO = 1 << 2,
    EXT_BOARD = 1 << 3,
    EXT_KEYBOARD = 1 << 4,
};

typedef struct PlatMeta {
    char name[256];          // long name in the console language (may contain '\n')
    char short_name[128];
    char publisher[128];
    char product_code[32];   // WUP-P-ARPE
    char company_code[8];    // 0001
    uint32_t region;         // bit 0 JPN, 1 USA, 2 EUR
    uint32_t drc_use;
    uint32_t ext_flags;
    uint32_t title_version;
} PlatMeta;

bool plat_init(void);
void plat_shutdown(void);
bool plat_is_console(void);

// Installed titles (games, updates and DLC). Returns the count; *out is malloc'd.
int  plat_list_titles(PlatTitle **out);
bool plat_read_meta(const PlatTitle *t, PlatMeta *m);
// Whole file from the title's meta folder ("iconTex.tga", "bootTvTex.tga"),
// malloc'd, or NULL. Called from the loader thread only.
uint8_t *plat_read_title_file(const PlatTitle *t, const char *name, size_t *size);
// Newest modification time (unix seconds) in the title's save folder, 0 if none.
int64_t plat_save_time(const PlatTitle *t);

const char *plat_data_dir(void);     // writable folder on the SD card
bool plat_font(const void **data, size_t *size);
int  plat_language(void);            // Wii U language index (0 ja, 1 en, ... 5 es)
bool plat_launch(const PlatTitle *t);
// SD:/wiiu/environments/aroma/plugins/pixmo_inicio.wps is there (the Aroma plugin
// that opens Pixmo Games at power-on and after a game closes; source in plugin/)
bool plat_boot_plugin_installed(void);
#define BOOT_PLUGIN_REL "wiiu/environments/aroma/plugins/pixmo_inicio.wps"
// Wii titles for the Wii U (PlatTitle.wii) boot into Wii mode with the screen
// and controller chosen beforehand, as the Wii U Menu asks ("use the GamePad?").
// Call right before plat_launch.
enum { WII_SHOW_GAMEPAD, WII_SHOW_TV, WII_SHOW_DRC };
void plat_wii_display(int mode);
// Waits (up to timeout_ms, polling *abort) until the network is up. The
// connection itself is started by wut at program start (ACConnectAsync).
bool plat_net_ready(int timeout_ms, volatile bool *abort);

// Background worker threads. On Wii U they run on cores 0/2 (the interface
// runs on core 1) at a lower priority than the interface: Cafe OS does not
// share time between threads of equal priority, so a busy worker on the
// interface's core would freeze it.
// Cafe OS service threads (alarms, callbacks) have a lower priority than any
// application thread, so workers must never compute for long without
// sleeping: plat_coop() sleeps 1 ms once 4 ms have passed since the last pause.
// Heavy workers stay on core 2; core 0 is left nearly free for the system.
enum { PLAT_CORE_WORK, PLAT_CORE_LIGHT };
typedef struct PlatThread PlatThread;
PlatThread *plat_thread_start(int (*fn)(void *), void *arg, const char *name, size_t stack, int core);
void plat_coop(uint32_t *last_pause);
void plat_thread_join(PlatThread *t);
void plat_sleep_ms(int ms);
const char *plat_thread_name(void);

// meta.c: parses a meta.xml document (used on PC and as a console fallback)
bool meta_parse_xml(const char *xml, int language, PlatMeta *m);

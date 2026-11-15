// Plataforma PC (solo pruebas): títulos falsos en pcdata/titles/<titleid>/,
// tarjeta SD simulada en pcdata/sd/.
#ifndef __WIIU__

#include "platform.h"
#include "wuhb.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static uint8_t *s_font;
static size_t s_font_size;

static const char *root(void)
{
    const char *r = getenv("LUDO_PCDATA");
    return r ? r : "pcdata";
}

bool plat_init(void)
{
    return true;
}

void plat_shutdown(void)
{
    free(s_font);
    s_font = NULL;
}

bool plat_is_console(void)
{
    return false;
}

int plat_list_titles(PlatTitle **out)
{
    char dir[512];
    snprintf(dir, sizeof(dir), "%s/titles", root());
    *out = NULL;
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int cap = 64, n = 0;
    PlatTitle *res = calloc(cap, sizeof(*res));
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (strlen(e->d_name) != 16)
            continue;
        if (n == cap) {
            cap *= 2;
            res = realloc(res, cap * sizeof(*res));
        }
        PlatTitle *t = &res[n++];
        memset(t, 0, sizeof(*t));
        t->tid = strtoull(e->d_name, NULL, 16);
        uint32_t hi = (uint32_t)(t->tid >> 32);
        t->kind = hi == 0x0005000E ? TITLE_UPDATE : hi == 0x0005000C ? TITLE_DLC : TITLE_GAME;
        snprintf(t->path, sizeof(t->path), "%s/%s", dir, e->d_name);
        char devpath[600];
        snprintf(devpath, sizeof(devpath), "%s/device.txt", t->path);
        char *dev = (char *)file_read(devpath, NULL);
        str_copy(t->device, sizeof(t->device), dev ? str_trim(dev) : "mlc");
        free(dev);
        snprintf(devpath, sizeof(devpath), "%s/version.txt", t->path);
        char *ver = (char *)file_read(devpath, NULL);
        t->version = ver ? (uint16_t)atoi(ver) : 0;
        free(ver);
        // a test title stands for a Wii/GameCube game for the Wii U if it has wii.txt
        snprintf(devpath, sizeof(devpath), "%s/wii.txt", t->path);
        t->wii = file_exists(devpath);
    }
    closedir(d);
    char sd[512];
    snprintf(sd, sizeof(sd), "%s/sd", root());
    wuhb_scan(sd, &res, &n, &cap); // .wuhb homebrew on the simulated SD card
    *out = res;
    return n;
}

static void homebrew_bundle(const PlatTitle *t, char *out, size_t n)
{
    snprintf(out, n, "%s/sd/%s", root(), t->path);
}

bool plat_read_meta(const PlatTitle *t, PlatMeta *m)
{
    if (t->homebrew) {
        char b[700];
        homebrew_bundle(t, b, sizeof(b));
        return wuhb_meta(b, m);
    }
    char *xml = (char *)plat_read_title_file(t, "meta.xml", NULL);
    if (!xml)
        return false;
    if (getenv("LUDO_SLOW"))
        SDL_Delay(40); // simulate slow console storage
    bool ok = meta_parse_xml(xml, plat_language(), m);
    free(xml);
    return ok;
}

uint8_t *plat_read_title_file(const PlatTitle *t, const char *name, size_t *size)
{
    if (t->homebrew) {
        char b[700], inner[64];
        homebrew_bundle(t, b, sizeof(b));
        snprintf(inner, sizeof(inner), "/meta/%s.gz", name);
        uint8_t *d = NULL;
        size_t n = 0;
        if (!wuhb_read_gz(b, inner, &d, &n))
            return NULL;
        if (size)
            *size = n;
        return d;
    }
    char path[600];
    snprintf(path, sizeof(path), "%s/meta/%s", t->path, name);
    return file_read(path, size);
}

static void scan_saves(const char *path, int depth, int64_t *best, int *budget)
{
    DIR *d = opendir(path);
    if (!d)
        return;
    struct dirent *e;
    while (*budget > 0 && (e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        char p[1024];
        snprintf(p, sizeof(p), "%s/%s", path, e->d_name);
        struct stat st;
        (*budget)--;
        if (stat(p, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < 3)
                scan_saves(p, depth + 1, best, budget);
        } else if ((int64_t)st.st_mtime > *best) {
            *best = (int64_t)st.st_mtime;
        }
    }
    closedir(d);
}

int64_t plat_save_time(const PlatTitle *t)
{
    if (t->homebrew)
        return 0;
    char path[600];
    snprintf(path, sizeof(path), "%s/save", t->path);
    int64_t best = 0;
    int budget = 96;
    scan_saves(path, 0, &best, &budget);
    return best;
}

const char *plat_data_dir(void)
{
    // same rule as the console: an old, not yet moved "ludoteca" folder is still used
    static char dir[512];
    if (!dir[0]) {
        char old[512];
        struct stat st;
        snprintf(dir, sizeof(dir), "%s/sd/wiiu/apps/" APP_DIR, root());
        snprintf(old, sizeof(old), "%s/sd/wiiu/apps/" APP_DIR_OLD "/config.txt", root());
        if (stat(dir, &st) != 0 && stat(old, &st) == 0)
            snprintf(dir, sizeof(dir), "%s/sd/wiiu/apps/" APP_DIR_OLD, root());
    }
    return dir;
}

bool plat_font(const void **data, size_t *size)
{
    if (!s_font) {
        char path[512];
        snprintf(path, sizeof(path), "%s/CafeStd.ttf", root());
        s_font = file_read(path, &s_font_size);
    }
    if (!s_font)
        return false;
    *data = s_font;
    *size = s_font_size;
    return true;
}

int plat_language(void)
{
    const char *l = getenv("LUDO_LANG");
    return l ? atoi(l) : 5;
}

bool plat_launch(const PlatTitle *t)
{
    log_msg("launch %016llx%s%s (PC: simulated)", (unsigned long long)t->tid, t->wii ? " por ruta" : "",
            t->homebrew ? " homebrew" : "");
    return true;
}

bool plat_boot_plugin_installed(void)
{
    char p[512];
    snprintf(p, sizeof(p), "%s/sd/" BOOT_PLUGIN_REL, root());
    return file_exists(p);
}

void plat_wii_display(int mode)
{
    static const char *names[] = { "GamePad", "TV", "pantalla del GamePad" };
    log_msg("modo Wii: %s (PC: simulado)", names[mode >= 0 && mode <= 2 ? mode : 0]);
}

bool plat_net_ready(int timeout_ms, volatile bool *abort)
{
    (void)timeout_ms;
    (void)abort;
    return getenv("LUDO_NONET") == NULL;
}

struct PlatThread {
    SDL_Thread *t;
};

PlatThread *plat_thread_start(int (*fn)(void *), void *arg, const char *name, size_t stack, int core)
{
    (void)core;
    PlatThread *t = calloc(1, sizeof(PlatThread));
    if (!t)
        return NULL;
    t->t = SDL_CreateThreadWithStackSize(fn, name, stack, arg);
    if (!t->t) {
        free(t);
        return NULL;
    }
    return t;
}

void plat_thread_join(PlatThread *t)
{
    if (!t)
        return;
    SDL_WaitThread(t->t, NULL);
    free(t);
}

void plat_sleep_ms(int ms)
{
    SDL_Delay((Uint32)ms);
}

void plat_coop(uint32_t *last_pause)
{
    uint32_t now = SDL_GetTicks();
    if (now - *last_pause >= 4) {
        SDL_Delay(1);
        *last_pause = SDL_GetTicks();
    }
}

const char *plat_thread_name(void)
{
    static char buf[24];
    snprintf(buf, sizeof(buf), "%lx", (unsigned long)SDL_ThreadID());
    return buf;
}

#endif

// Plataforma Wii U: títulos instalados (MCP/ACP), archivos de los títulos
// (iconos, imágenes de arranque, partidas guardadas) con la API FS del sistema,
// fuente del sistema, idioma, red y lanzamiento de juegos.
#ifdef __WIIU__

#include "platform.h"
#include "wuhb.h"

#include <malloc.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <coreinit/filesystem.h>
#include <coreinit/ios.h>
#include <coreinit/mcp.h>
#include <coreinit/memory.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/userconfig.h>
#include <nn/ac/ac_c.h>
#include <nn/acp/client.h>
#include <nn/acp/title.h>
#include <sysapp/launch.h>
#include <coreinit/dynload.h>
#include <nn/cmpt/cmpt.h>

static bool s_acp;
static SDL_mutex *s_lock;
static int s_language = -1;

// Own FS client for reading other titles' files. Regular apps may not read
// /vol/storage_*/usr/title; Aroma's MochaPayload grants full access to a
// client that sends it ioctl 0x28 (the request libmocha's
// Mocha_UnlockFSClient uses). We only send it after confirming MochaPayload
// answers its version query, so a console without Aroma is left untouched.
static FSClient *s_fsc;
static FSCmdBlock *s_fsb;
static bool s_fs_ok, s_fs_unlocked;

#define FSTIME_EPOCH_DIFF 315532800LL // seconds from 1970 to 1980 (FS time epoch)

static bool mocha_present(void)
{
    int fd = IOS_Open("/dev/mcp", IOS_OPEN_READ);
    if (fd < 0)
        return false;
    __attribute__((aligned(0x40))) uint32_t io[0x40 / 4];
    memset(io, 0, sizeof(io));
    io[0] = 0xF8; // IPC_CUSTOM_GET_MOCHA_API_VERSION
    bool ok = IOS_Ioctl(fd, 100, io, 4, io, 8) == IOS_ERROR_OK && io[0] == 0xCAFEBABE && io[1] >= 1;
    IOS_Close(fd);
    return ok;
}

static void fs_setup(void)
{
    FSInit();
    s_fsc = memalign(0x40, sizeof(FSClient));
    s_fsb = memalign(0x40, sizeof(FSCmdBlock));
    if (!s_fsc || !s_fsb)
        return;
    memset(s_fsc, 0, sizeof(FSClient));
    memset(s_fsb, 0, sizeof(FSCmdBlock));
    if (FSAddClient(s_fsc, FS_ERROR_FLAG_ALL) != FS_STATUS_OK)
        return;
    FSInitCmdBlock(s_fsb);
    s_fs_ok = true;
    if (mocha_present()) {
        __attribute__((aligned(0x40))) uint32_t dummy[0x40 / 4];
        memset(dummy, 0, sizeof(dummy));
        FSClientBody *body = FSGetClientBody(s_fsc);
        s_fs_unlocked = body && IOS_Ioctl(body->clientHandle, 0x28, dummy, sizeof(dummy), dummy, sizeof(dummy)) == 0;
    }
}

bool plat_init(void)
{
    s_lock = SDL_CreateMutex();
    ACPInitialize();
    s_acp = true;
    fs_setup();
    plat_language(); // cache it before the worker threads ask for it
    log_msg("init: fs=%d unlocked=%d idioma=%d", s_fs_ok, s_fs_unlocked, s_language);
    return true;
}

void plat_shutdown(void)
{
    if (s_fs_ok) {
        FSDelClient(s_fsc, FS_ERROR_FLAG_ALL);
        s_fs_ok = false;
    }
    free(s_fsc);
    free(s_fsb);
    s_fsc = NULL;
    s_fsb = NULL;
    if (s_acp)
        ACPFinalize();
}

// Reads a whole file with the FS client (path like /vol/storage_mlc01/...).
// Buffers that the system fills through IPC must cover whole 64-byte cache
// lines: the transfer and the cache maintenance work on full lines, so a
// buffer that ends mid-line corrupts whatever shares that line (on hardware
// the heap broke right after the first icon; Cemu copies exact sizes).
#define LINE 0x40u
#define LINE_UP(n) (((n) + LINE - 1) & ~(LINE - 1))

static void *ipc_alloc(size_t n)
{
    void *p = memalign(LINE, LINE_UP(n) + LINE);
    if (p)
        memset(p, 0, LINE_UP(n) + LINE);
    return p;
}

static int s_reads_logged;

static uint8_t *fs_read(const char *path, size_t *size)
{
    if (!s_fs_ok)
        return NULL;
    uint8_t *data = NULL;
    SDL_LockMutex(s_lock);
    FSFileHandle fh;
    FSStatus open_st = FSOpenFile(s_fsc, s_fsb, path, "r", &fh, FS_ERROR_FLAG_ALL);
    static int fails;
    if (open_st != FS_STATUS_OK && fails < 5) {
        fails++;
        log_msg("FSOpenFile %s -> %d", path, (int)open_st);
    }
    if (open_st == FS_STATUS_OK) {
        FSStat *st = ipc_alloc(sizeof(FSStat));
        if (st && FSGetStatFile(s_fsc, s_fsb, fh, st, FS_ERROR_FLAG_ALL) == FS_STATUS_OK && st->size > 0 &&
            st->size < 32 * 1024 * 1024) {
            uint32_t fsz = st->size;
            bool trace = s_reads_logged < 3;
            if (trace)
                log_msg("leyendo %s (%u bytes)", path, (unsigned)fsz);
            data = ipc_alloc(fsz + 1);
            if (data) {
                uint32_t done = 0;
                while (done < fsz) {
                    // whole cache lines per request, at most 1 MiB (like libmocha)
                    uint32_t want = LINE_UP(fsz - done);
                    if (want > 0x100000)
                        want = 0x100000;
                    FSStatus r = FSReadFile(s_fsc, s_fsb, data + done, 1, want, fh, 0, FS_ERROR_FLAG_ALL);
                    if (r <= 0)
                        break;
                    done += (uint32_t)r;
                    if ((uint32_t)r < want)
                        break;
                }
                if (done < fsz) {
                    free(data);
                    data = NULL;
                } else {
                    data[fsz] = 0;
                    if (size)
                        *size = fsz;
                }
            }
            if (trace) {
                s_reads_logged++;
                log_msg("lectura %s", data ? "ok" : "fallida");
            }
        }
        free(st);
        FSCloseFile(s_fsc, s_fsb, fh, FS_ERROR_FLAG_ALL);
    }
    SDL_UnlockMutex(s_lock);
    return data;
}

static void fs_newest(const char *path, int depth, int64_t *best, int *budget)
{
    FSDirectoryHandle dh;
    if (FSOpenDir(s_fsc, s_fsb, path, &dh, FS_ERROR_FLAG_ALL) != FS_STATUS_OK)
        return;
    FSDirectoryEntry *e = ipc_alloc(sizeof(FSDirectoryEntry));
    while (e && *budget > 0 && FSReadDir(s_fsc, s_fsb, dh, e, FS_ERROR_FLAG_ALL) == FS_STATUS_OK) {
        (*budget)--;
        if (e->name[0] == '.')
            continue;
        if (e->info.flags & FS_STAT_DIRECTORY) {
            if (depth < 3) {
                char sub[384];
                snprintf(sub, sizeof(sub), "%s/%s", path, e->name);
                fs_newest(sub, depth + 1, best, budget);
            }
        } else {
            int64_t t = (int64_t)(e->info.modified / 1000000) + FSTIME_EPOCH_DIFF;
            if (t > *best)
                *best = t;
        }
    }
    free(e);
    FSCloseDir(s_fsc, s_fsb, dh, FS_ERROR_FLAG_ALL);
}

#define SD_ROOT "fs:/vol/external01"

// icon and boot screen of a .wuhb live gzipped inside the bundle
static uint8_t *homebrew_file(const PlatTitle *t, const char *name, size_t *size)
{
    char bundle[300], inner[64];
    snprintf(bundle, sizeof(bundle), SD_ROOT "/%s", t->path);
    snprintf(inner, sizeof(inner), "/meta/%s.gz", name);
    uint8_t *d = NULL;
    size_t n = 0;
    if (!wuhb_read_gz(bundle, inner, &d, &n))
        return NULL;
    if (size)
        *size = n;
    return d;
}

uint8_t *plat_read_title_file(const PlatTitle *t, const char *name, size_t *size)
{
    if (t->homebrew)
        return homebrew_file(t, name, size);
    char path[256];
    snprintf(path, sizeof(path), "%s/meta/%s", t->path, name);
    uint8_t *d = fs_read(path, size);
    if (!d) {
        // the meta folder can live elsewhere (e.g. disc titles): ask the system
        char *dir = ipc_alloc(256);
        if (dir && ACPGetTitleMetaDir(t->tid, dir, 256) == ACP_RESULT_SUCCESS) {
            dir[255] = 0;
            snprintf(path, sizeof(path), "%s/%s", dir, name);
            d = fs_read(path, size);
        }
        free(dir);
    }
    return d;
}

int64_t plat_save_time(const PlatTitle *t)
{
    if (!s_fs_ok || t->homebrew)
        return 0;
    const char *dev = strstr(t->path, "storage_usb") ? "storage_usb01" : "storage_mlc01";
    char path[160];
    snprintf(path, sizeof(path), "/vol/%s/usr/save/%08x/%08x/user", dev, (unsigned)(t->tid >> 32),
             (unsigned)(t->tid & 0xFFFFFFFF));
    int64_t best = 0;
    int budget = 96;
    SDL_LockMutex(s_lock);
    fs_newest(path, 0, &best, &budget);
    SDL_UnlockMutex(s_lock);
    return best;
}

bool plat_is_console(void)
{
    return true;
}

int plat_list_titles(PlatTitle **out)
{
    *out = NULL;
    int32_t h = MCP_Open();
    if (h < 0)
        return 0;
    int32_t count = MCP_TitleCount(h);
    if (count <= 0) {
        MCP_Close(h);
        return 0;
    }
    uint32_t cap = (uint32_t)count + 32;
    MCPTitleListType *list = ipc_alloc(sizeof(MCPTitleListType) * cap);
    if (!list) {
        MCP_Close(h);
        return 0;
    }
    memset(list, 0, sizeof(MCPTitleListType) * cap);
    uint32_t got = 0;
    MCPError err = MCP_TitleList(h, &got, list, sizeof(MCPTitleListType) * cap);
    MCP_Close(h);
    if (err < 0 || got == 0) {
        free(list);
        return 0;
    }
    PlatTitle *res = calloc(got, sizeof(*res));
    int n = 0, nwii = 0, nother = 0;
    for (uint32_t i = 0; i < got && res; i++) {
        MCPTitleListType *e = &list[i];
        int kind;
        // games: Wii U (0x80000000) and Wii titles for the Wii U (0x8000002E: Wii
        // games from the eShop and injected Wii/GameCube games, which boot into Wii mode)
        if (e->appType == MCP_APP_TYPE_GAME || e->appType == MCP_APP_TYPE_GAME_WII ||
            (e->appType & 0xF0000000u) == 0x80000000u)
            kind = TITLE_GAME;
        else if (e->appType == MCP_APP_TYPE_GAME_UPDATE)
            kind = TITLE_UPDATE;
        else if (e->appType == MCP_APP_TYPE_GAME_DLC)
            kind = TITLE_DLC;
        else
            continue;
        PlatTitle *t = &res[n++];
        t->tid = e->titleId;
        t->kind = kind;
        t->version = e->titleVersion;
        t->wii = e->appType == MCP_APP_TYPE_GAME_WII;
        if (t->wii)
            nwii++;
        else if (kind == TITLE_GAME && e->appType != MCP_APP_TYPE_GAME)
            nother++;
        char path[57];
        memcpy(path, e->path, 56);
        path[56] = 0;
        size_t len = strlen(path);
        while (len && path[len - 1] == '/')
            path[--len] = 0;
        str_copy(t->path, sizeof(t->path), path);
        if (strstr(path, "storage_usb"))
            str_copy(t->device, sizeof(t->device), "usb");
        else if (strstr(path, "storage_mlc"))
            str_copy(t->device, sizeof(t->device), "mlc");
        else if (strstr(path, "odd") || !strncmp(e->indexedDevice, "odd", 3))
            str_copy(t->device, sizeof(t->device), "odd");
        else {
            char dev[11];
            memcpy(dev, e->indexedDevice, 10);
            dev[10] = 0;
            str_copy(t->device, sizeof(t->device), dev);
        }
    }
    free(list);
    log_msg("MCP: %u titulos instalados; %d juegos/actualizaciones/DLC (juegos de Wii: %d, de otro tipo: %d)",
            (unsigned)got, n, nwii, nother);
    // homebrew bundles (.wuhb) on the SD card
    int res_cap = (int)got, before = n; // res holds `got` entries
    if (res)
        wuhb_scan(SD_ROOT, &res, &n, &res_cap);
    log_msg("homebrew: %d paquetes .wuhb en la SD", n - before);
    *out = res;
    return n;
}

int plat_language(void)
{
    if (s_language >= 0)
        return s_language;
    s_language = 1;
    UCHandle h = UCOpen();
    if (h >= 0) {
        uint32_t *lang = ipc_alloc(sizeof(uint32_t));
        UCSysConfig *cfg = ipc_alloc(sizeof(UCSysConfig));
        if (lang && cfg) {
            *lang = 1;
            str_copy(cfg->name, sizeof(cfg->name), "cafe.language");
            cfg->dataType = UC_DATATYPE_UNSIGNED_INT;
            cfg->dataSize = sizeof(uint32_t);
            cfg->data = lang;
            if (UCReadSysConfig(h, 1, cfg) == UC_ERROR_OK && *lang < 12)
                s_language = (int)*lang;
        }
        free(lang);
        free(cfg);
        UCClose(h);
    }
    return s_language;
}

bool plat_read_meta(const PlatTitle *t, PlatMeta *m)
{
    memset(m, 0, sizeof(*m));
    if (t->homebrew) {
        char bundle[300];
        snprintf(bundle, sizeof(bundle), SD_ROOT "/%s", t->path);
        return wuhb_meta(bundle, m);
    }
    int lang = plat_language();
    if (s_acp) {
        ACPMetaXml *mx = ipc_alloc(sizeof(ACPMetaXml));
        if (mx) {
            memset(mx, 0, sizeof(*mx));
            if (ACPGetTitleMetaXml(t->tid, mx) == ACP_RESULT_SUCCESS) {
                const char *ln[12] = { mx->longname_ja, mx->longname_en, mx->longname_fr,
                                       mx->longname_de, mx->longname_it, mx->longname_es,
                                       mx->longname_zhs, mx->longname_ko, mx->longname_nl,
                                       mx->longname_pt, mx->longname_ru, mx->longname_zht };
                const char *sn[12] = { mx->shortname_ja, mx->shortname_en, mx->shortname_fr,
                                       mx->shortname_de, mx->shortname_it, mx->shortname_es,
                                       mx->shortname_zhs, mx->shortname_ko, mx->shortname_nl,
                                       mx->shortname_pt, mx->shortname_ru, mx->shortname_zht };
                const char *pb[12] = { mx->publisher_ja, mx->publisher_en, mx->publisher_fr,
                                       mx->publisher_de, mx->publisher_it, mx->publisher_es,
                                       mx->publisher_zhs, mx->publisher_ko, mx->publisher_nl,
                                       mx->publisher_pt, mx->publisher_ru, mx->publisher_zht };
                const char **fields[3] = { ln, sn, pb };
                char *dst[3] = { m->name, m->short_name, m->publisher };
                size_t dsz[3] = { sizeof(m->name), sizeof(m->short_name), sizeof(m->publisher) };
                for (int f = 0; f < 3; f++) {
                    const char *pick = fields[f][lang];
                    if (!pick[0])
                        pick = fields[f][1];
                    for (int i = 0; i < 12 && !pick[0]; i++)
                        pick = fields[f][i];
                    str_copy(dst[f], dsz[f], pick);
                    char *tr = str_trim(dst[f]);
                    if (tr != dst[f])
                        memmove(dst[f], tr, strlen(tr) + 1);
                }
                str_copy(m->product_code, sizeof(m->product_code), mx->product_code);
                str_copy(m->company_code, sizeof(m->company_code), mx->company_code);
                m->region = mx->region;
                m->drc_use = mx->drc_use;
                m->title_version = mx->title_version;
                if (mx->ext_dev_nunchaku) m->ext_flags |= EXT_NUNCHUK;
                if (mx->ext_dev_classic) m->ext_flags |= EXT_CLASSIC;
                if (mx->ext_dev_urcc) m->ext_flags |= EXT_PRO;
                if (mx->ext_dev_board) m->ext_flags |= EXT_BOARD;
                if (mx->ext_dev_usb_keyboard) m->ext_flags |= EXT_KEYBOARD;
            }
            free(mx);
            if (m->name[0] || m->short_name[0])
                return true;
        }
    }
    // fall back to reading meta.xml directly
    char *xml = (char *)plat_read_title_file(t, "meta.xml", NULL);
    if (!xml)
        return false;
    bool ok = meta_parse_xml(xml, lang, m);
    free(xml);
    return ok;
}

const char *plat_data_dir(void)
{
    // Before it was renamed the app kept its data in apps/ludoteca: if that
    // folder was not moved, keep using it so nothing is lost.
    static const char *dir;
    if (!dir) {
        static const char now[] = "fs:/vol/external01/wiiu/apps/" APP_DIR;
        static const char old[] = "fs:/vol/external01/wiiu/apps/" APP_DIR_OLD;
        struct stat st;
        bool keep_old = stat(now, &st) != 0 &&
                        stat("fs:/vol/external01/wiiu/apps/" APP_DIR_OLD "/config.txt", &st) == 0;
        dir = keep_old ? old : now;
    }
    return dir;
}

bool plat_font(const void **data, size_t *size)
{
    void *ptr = NULL;
    uint32_t sz = 0;
    if (!OSGetSharedData(OS_SHAREDDATATYPE_FONT_STANDARD, 0, &ptr, &sz) || !ptr || !sz)
        return false;
    *data = ptr;
    *size = sz;
    return true;
}

bool plat_launch(const PlatTitle *t)
{
    if (t->homebrew) {
        // .wuhb on the SD: Aroma's RPX loader (module "homebrew_rpx_loader", the same
        // export librpxloader's RPXLoader_LaunchHomebrew calls); path relative to the SD root
        int32_t (*launch_hb)(const char *) = NULL;
        OSDynLoad_Module rl = NULL;
        if (OSDynLoad_Acquire("homebrew_rpx_loader", &rl) == OS_DYNLOAD_OK && rl)
            OSDynLoad_FindExport(rl, OS_DYNLOAD_EXPORT_FUNC, "RL_LaunchHomebrew", (void **)&launch_hb);
        int32_t r = launch_hb ? launch_hb(t->path) : -999;
        log_msg("launch homebrew %s -> %d", t->path, (int)r);
        return r == 0;
    }
    if (t->wii) {
        // Wii titles (eShop and injected Wii/GameCube games): the Wii U Menu starts
        // titles by their install path; SYSLaunchTitle(tid) failed or hung with them.
        log_msg("launch %016llx por ruta %s", (unsigned long long)t->tid, t->path);
        _SYSLaunchTitleByPathFromLauncher(t->path, (uint32_t)strlen(t->path));
    } else {
        // The way Launchiine (the launcher by Aroma's author) starts a Wii U game:
        // assign the title's update first, then launch. Plain SYSLaunchTitle left the
        // console on the white Wii U screen (Deus Ex, Darksiders II, Dual Core; Nov
        // 2026): the update still assigned was the one of the last title started, so
        // it only worked when that happened to be this game. The Wii U Menu assigns it.
        MCPTitleListType *info = ipc_alloc(sizeof(MCPTitleListType)); // IPC: aligned
        int32_t mcp = MCP_Open();
        int32_t got = info && mcp >= 0 ? (int32_t)MCP_GetTitleInfo(mcp, t->tid, info) : -1;
        if (mcp >= 0)
            MCP_Close(mcp);
        int32_t patch = -1;
        if (got == 0)
            patch = (int32_t)ACPAssignTitlePatch(info);
        log_msg("launch %016llx (info %d, actualizacion asignada %d)", (unsigned long long)t->tid, (int)got,
                (int)patch);
        _SYSLaunchTitleWithStdArgsInNoSplash(t->tid, NULL);
        free(info);
    }
    log_msg("launch: pedido hecho, esperando al sistema");
    return true;
}

bool plat_boot_plugin_installed(void)
{
    struct stat st;
    return stat(SD_ROOT "/" BOOT_PLUGIN_REL, &st) == 0;
}

// What the Wii U Menu does after its "use the GamePad?" question, before it
// starts a Wii title (same calls as the Aroma plugin WiiVCLaunch): the Wii
// mode boot reads the screen type and the GamePad-as-controller switch from
// nn_cmpt. Without them the title stops with "the program could not be started".
void plat_wii_display(int mode)
{
    // (SDL already has the Wii Remote library up; nn_cmpt needs it)
    int32_t (*set_drc_ctrl)(int32_t) = NULL; // exported by nn_cmpt, not declared by wut
    OSDynLoad_Module cmpt = NULL;
    if (OSDynLoad_Acquire("nn_cmpt.rpl", &cmpt) == OS_DYNLOAD_OK && cmpt)
        OSDynLoad_FindExport(cmpt, OS_DYNLOAD_EXPORT_FUNC, "CMPTAcctSetDrcCtrlEnabled", (void **)&set_drc_ctrl);
    int32_t r_screen, r_drc = -1;
    if (mode == WII_SHOW_GAMEPAD) {
        // GamePad as the controller, picture on the GamePad (and the TV if there is one)
        r_screen = CMPTAcctSetScreenType(CMPT_SCREEN_TYPE_BOTH);
        if (set_drc_ctrl)
            r_drc = set_drc_ctrl(1);
    } else {
        // Wii Remote; picture on the TV or on the GamePad screen
        if (set_drc_ctrl)
            r_drc = set_drc_ctrl(0);
        r_screen = CMPTAcctSetScreenType(mode == WII_SHOW_TV ? CMPT_SCREEN_TYPE_TV : CMPT_SCREEN_TYPE_DRC);
    }
    int32_t state = CMPTCheckScreenState();
    if (state < 0) // the chosen screen is not available (no TV): use the GamePad
        CMPTAcctSetScreenType(CMPT_SCREEN_TYPE_DRC);
    log_msg("modo Wii: modo %d, pantalla %d, control %d (funcion %s), estado %d", mode, (int)r_screen, (int)r_drc,
            set_drc_ctrl ? "ok" : "no encontrada", (int)state);
    if (cmpt)
        OSDynLoad_Release(cmpt);
}

bool plat_net_ready(int timeout_ms, volatile bool *abort)
{
    // wut's socket library already called ACInitialize + ACConnectAsync at
    // startup; calling ACConnect again from a worker is not needed (and was
    // the first suspect of a freeze on hardware). Just wait for the link.
    for (int waited = 0; waited <= timeout_ms; waited += 250) {
        BOOL connected = FALSE;
        if (NNResult_IsSuccess(ACIsApplicationConnected(&connected)) && connected)
            return true;
        if (abort && *abort)
            return false;
        OSSleepTicks(OSMillisecondsToTicks(250));
    }
    return false;
}

struct PlatThread {
    OSThread thread;
    void *stack;
    int (*fn)(void *);
    void *arg;
    char name[24];
};

static int thread_entry(int argc, const char **argv)
{
    (void)argc;
    PlatThread *t = (PlatThread *)argv;
    return t->fn(t->arg);
}

PlatThread *plat_thread_start(int (*fn)(void *), void *arg, const char *name, size_t stack, int core)
{
    PlatThread *t = memalign(16, sizeof(PlatThread));
    if (!t)
        return NULL;
    memset(t, 0, sizeof(*t));
    t->stack = memalign(16, stack);
    if (!t->stack) {
        free(t);
        return NULL;
    }
    t->fn = fn;
    t->arg = arg;
    str_copy(t->name, sizeof(t->name), name);
    int prio = OSGetThreadPriority(OSGetCurrentThread()) + 3;
    if (prio > 31)
        prio = 31;
    if (!OSCreateThread(&t->thread, thread_entry, 0, (char *)t, (uint8_t *)t->stack + stack, (uint32_t)stack,
                        prio, core == PLAT_CORE_LIGHT ? OS_THREAD_ATTRIB_AFFINITY_CPU0
                                                      : OS_THREAD_ATTRIB_AFFINITY_CPU2)) {
        free(t->stack);
        free(t);
        return NULL;
    }
    OSSetThreadName(&t->thread, t->name);
    OSResumeThread(&t->thread);
    return t;
}

void plat_thread_join(PlatThread *t)
{
    if (!t)
        return;
    int res = 0;
    OSJoinThread(&t->thread, &res);
    free(t->stack);
    free(t);
}

void plat_sleep_ms(int ms)
{
    OSSleepTicks(OSMillisecondsToTicks(ms));
}

void plat_coop(uint32_t *last_pause)
{
    uint32_t now = SDL_GetTicks();
    if (now - *last_pause >= 4) {
        OSSleepTicks(OSMillisecondsToTicks(1));
        *last_pause = SDL_GetTicks();
    }
}

const char *plat_thread_name(void)
{
    const char *n = OSGetThreadName(OSGetCurrentThread());
    return n ? n : "?";
}

#endif

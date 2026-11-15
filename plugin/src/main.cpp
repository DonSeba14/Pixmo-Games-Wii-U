// Pixmo Games: inicio. Plugin de Aroma (WUPS) que abre Pixmo Games:
//  - al encender la consola (el primer menú Wii U después de cargar Aroma), y
//  - cuando se cierra un juego iniciado desde Pixmo Games (la consola vuelve al
//    menú Wii U y Pixmo Games dejó SD:/wiiu/apps/pixmogames/volver.txt).
// Mantener B en el GamePad mientras aparece el menú Wii U lo deja en el menú.
// Ajustes de Pixmo Games (config.txt): "set al_encender 0" apaga lo primero; con
// "set volver 0" Pixmo Games no deja volver.txt.
//
// Va en SD:/wiiu/environments/aroma/plugins/pixmo_inicio.wps. Para quitarlo, se
// borra ese archivo.
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <rpxloader/rpxloader.h>
#include <sys/stat.h>
#include <sysapp/launch.h>
#include <sysapp/title.h>
#include <vpad/input.h>
#include <wups.h>

WUPS_PLUGIN_NAME("Pixmo Games Autostart");
WUPS_PLUGIN_DESCRIPTION("Opens Pixmo Games at power-on and after closing a game started from it. Hold B when the "
                        "Wii U Menu appears to stay there. / Abre Pixmo Games al encender y al cerrar un juego.");
WUPS_PLUGIN_VERSION("v1.2");
WUPS_PLUGIN_AUTHOR("Pixmo Games");
WUPS_PLUGIN_LICENSE("MIT");

WUPS_USE_WUT_DEVOPTAB(); // fopen/stat on fs:/vol/external01

#define DATA_DIR "fs:/vol/external01/wiiu/apps/pixmogames/"
#define APP_REL  "wiiu/apps/pixmogames/pixmogames.wuhb" // relative to the SD root (RPXLoader)
#define WAIT_MS  700 // the menu runs this long first: time to hold B, and the menu is settled

// plugin memory lives from power-on to power-off (Aroma loads plugins once)
static bool sMenuSeen;     // a Wii U Menu already started since power-on
static bool sPending;      // open Pixmo Games once this Wii U Menu is running
static bool sRpxLoader;    // librpxloader is usable
static OSTime sFirstRead;  // first VPADRead of this Wii U Menu while pending
static const char *sWhy = "";

static bool is_wiiu_menu(uint64_t tid)
{
    return tid == 0x0005001010040000ull || tid == 0x0005001010040100ull || tid == 0x0005001010040200ull;
}

// one line per decision in inicio_log.txt (kept small), to see what happened
static void log_line(const char *fmt, ...)
{
    struct stat st;
    bool big = stat(DATA_DIR "inicio_log.txt", &st) == 0 && st.st_size > 16 * 1024;
    FILE *f = fopen(DATA_DIR "inicio_log.txt", big ? "w" : "a");
    if (!f)
        return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

// "set <key> 0" in Pixmo Games' config.txt turns an option off; missing = on
static bool option_on(const char *key)
{
    FILE *f = fopen(DATA_DIR "config.txt", "r");
    if (!f)
        return true;
    char line[200];
    size_t kl = strlen(key);
    bool on   = true;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "set ", 4) && !strncmp(line + 4, key, kl) && line[4 + kl] == ' ') {
            on = line[5 + kl] != '0';
            break;
        }
    }
    fclose(f);
    return on;
}

// volver.txt, left by Pixmo Games right before starting a game, is taken (removed):
// 0 = none, 1 = Wii U game or homebrew, 2 = Wii game (Wii mode ends with a reboot)
static int take_marker(void)
{
    FILE *f = fopen(DATA_DIR "volver.txt", "r");
    if (!f)
        return 0;
    char buf[8] = {0};
    if (!fgets(buf, sizeof(buf), f))
        buf[0] = 0;
    fclose(f);
    remove(DATA_DIR "volver.txt");
    return strncmp(buf, "wii", 3) == 0 ? 2 : 1;
}

INITIALIZE_PLUGIN()
{
    sRpxLoader = RPXLoader_InitLibrary() == RPX_LOADER_RESULT_SUCCESS;
}

DEINITIALIZE_PLUGIN()
{
    if (sRpxLoader)
        RPXLoader_DeInitLibrary();
    sRpxLoader = false;
}

ON_APPLICATION_START()
{
    sPending   = false;
    sFirstRead = 0;
    if (!is_wiiu_menu(OSGetTitleID()))
        return;
    bool first = !sMenuSeen;
    sMenuSeen  = true;
    int back   = take_marker();
    // A Wii U game returns to a menu of the same Aroma session; on the first menu a
    // Wii U marker is a leftover of powering off during a game: power-on rules apply
    if (back == 2 || (back == 1 && !first)) {
        sPending = true;
        sWhy     = "vuelta de un juego";
    } else if (first && option_on("al_encender")) {
        sPending = true;
        sWhy     = "encendido";
    } else if (back) {
        log_line("encendido: volver.txt quedo de antes de apagar; abrir al encender esta desactivado");
    }
}

ON_APPLICATION_ENDS()
{
    sPending = false;
}

// Pixmo Games is opened the way the Wii U Menu opens a homebrew icon
// (homebrew_on_menu): the RPX of the Health & Safety app is pointed at the .wuhb
// and the menu's launcher starts that app by its path. RPXLoader_LaunchHomebrew
// starts it with _SYSLaunchTitleWithStdArgsInNoSplash instead, and from a Pixmo
// Games opened that way no Wii U game booted: the console stayed on the white
// Wii U screen (Deus Ex, Dual Core, Darksiders II, Nov 2026); opened from its
// icon it always worked.
static void open_pixmo(void)
{
    sPending = false;
    struct stat st;
    if (stat("fs:/vol/external01/" APP_REL, &st) != 0) {
        log_line("%s: no esta " APP_REL ", se queda el menu Wii U", sWhy);
        return;
    }
    if (!sRpxLoader) {
        log_line("%s: RPXLoader no disponible", sWhy);
        return;
    }
    RPXLoaderStatus p = RPXLoader_PrepareLaunchFromSD(APP_REL);
    if (p == RPX_LOADER_RESULT_SUCCESS) {
        uint64_t tid = _SYSGetSystemApplicationTitleId(SYSTEM_APP_ID_HEALTH_AND_SAFETY);
        if (!SYSCheckTitleExists(tid)) // like RPXLoader: the daily log app if H&S is missing
            tid = _SYSGetSystemApplicationTitleId(SYSTEM_APP_ID_DAILY_LOG);
        char path[64];
        snprintf(path, sizeof(path), "/vol/storage_mlc01/sys/title/%08x/%08x", (unsigned)(tid >> 32),
                 (unsigned)(tid & 0xFFFFFFFF));
        _SYSLaunchTitleByPathFromLauncher(path, strlen(path));
        log_line("%s: abrir Pixmo Games como el menu (%s)", sWhy, path);
        return;
    } else {
        log_line("%s: preparar Pixmo Games -> %d", sWhy, (int)p);
    }
    // fallback: the direct way
    RPXLoaderStatus r = RPXLoader_LaunchHomebrew(APP_REL);
    log_line("%s: abrir Pixmo Games (directo) -> %d", sWhy, (int)r);
}

// The Wii U Menu reads the GamePad every frame once it runs: a safe moment to
// start another title (the menu has the foreground), and B can be checked here.
DECL_FUNCTION(int32_t, VPADRead, VPADChan chan, VPADStatus *buffers, uint32_t count, VPADReadError *error)
{
    int32_t res = real_VPADRead(chan, buffers, count, error);
    if (!sPending || chan != VPAD_CHAN_0)
        return res;
    if (res > 0 && (!error || *error == VPAD_READ_SUCCESS) && buffers && (buffers[0].hold & VPAD_BUTTON_B)) {
        sPending = false;
        log_line("%s: B apretado, se queda el menu Wii U", sWhy);
        return res;
    }
    OSTime now = OSGetTime();
    if (!sFirstRead)
        sFirstRead = now;
    else if (OSTicksToMilliseconds(now - sFirstRead) >= WAIT_MS)
        open_pixmo();
    return res;
}

WUPS_MUST_REPLACE_FOR_PROCESS(VPADRead, WUPS_LOADER_LIBRARY_VPAD, VPADRead, WUPS_FP_TARGET_PROCESS_WII_U_MENU);

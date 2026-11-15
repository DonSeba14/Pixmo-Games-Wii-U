// Pixmo Games: frontend de juegos instalados para Wii U (Aroma).
// Bucle principal, mandos (GamePad, Pro, Wiimote), pantalla táctil, teclado
// del sistema y, en PC, un modo guionizado que guarda capturas de pantalla.
#include "audio.h"
#include "brand.h"
#include "font.h"
#include "gfx.h"
#include "icons.h"
#include "library.h"
#include "platform.h"
#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __WIIU__
#include <SDL_syswm.h>
#include <SDL_system.h>
#else
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#endif

static SDL_Window *s_win;
static bool s_text_active;
static bool s_text_collect;
static char s_text_buf[256];
static char s_text_hint[128];

// ------------------------------------------------------------ text input

void sys_text_input(const char *hint, const char *initial, const char *ok_label)
{
    s_text_active = true;
    s_text_buf[0] = 0;
    str_copy(s_text_hint, sizeof(s_text_hint), hint);
#ifdef __WIIU__
    s_text_collect = false;
    SDL_WiiUSetSWKBDHintText(hint);
    SDL_WiiUSetSWKBDInitialText(initial && initial[0] ? initial : NULL);
    SDL_WiiUSetSWKBDHighlightInitialText(SDL_TRUE);
    SDL_WiiUSetSWKBDOKLabel(ok_label);
    SDL_WiiUSetSWKBDShowWordSuggestions(SDL_FALSE);
    SDL_StartTextInput();
#else
    (void)ok_label;
    str_copy(s_text_buf, sizeof(s_text_buf), initial ? initial : "");
    s_text_collect = true;
    if (!getenv("LUDO_SCRIPT"))
        SDL_StartTextInput();
#endif
}

static void text_finish(bool ok)
{
    s_text_active = false;
    s_text_collect = false;
    SDL_StopTextInput();
    ui_text_result(s_text_buf, ok);
}

#ifndef __WIIU__
static void draw_pc_text_box(void)
{
    if (!s_text_active || getenv("LUDO_SCRIPT"))
        return;
    gfx_rect(0, 0, SCREEN_W, SCREEN_H, rgba(0x000000, 0.5f));
    gfx_rrect(240, 250, 800, 200, 20, rgb(0xFFFFFF));
    text_draw(20, s_text_hint, 270, 275, rgb(0x6C747D), ALIGN_LEFT);
    gfx_rrect(270, 320, 740, 56, 12, rgb(0xEEF1F4));
    char shown[300];
    snprintf(shown, sizeof(shown), "%s|", s_text_buf);
    text_draw(26, shown, 290, 332, rgb(0x323539), ALIGN_LEFT);
    text_draw(17, "Intro = aceptar · Esc = cancelar (teclado de pruebas de PC)", 270, 400, rgb(0x6C747D),
              ALIGN_LEFT);
}
#endif

// ------------------------------------------------------------ input mapping

static Button map_controller_button(int b)
{
    switch (b) {
    case SDL_CONTROLLER_BUTTON_A: return BTN_A;
    case SDL_CONTROLLER_BUTTON_B: return BTN_B;
    case SDL_CONTROLLER_BUTTON_X: return BTN_X;
    case SDL_CONTROLLER_BUTTON_Y: return BTN_Y;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return BTN_L;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return BTN_R;
    case SDL_CONTROLLER_BUTTON_START: return BTN_PLUS;
    case SDL_CONTROLLER_BUTTON_BACK: return BTN_MINUS;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return BTN_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return BTN_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return BTN_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return BTN_RIGHT;
    default: return BTN_COUNT;
    }
}

// Raw joystick order used by the Wii U SDL driver (GamePad, Pro, Classic, Wiimote)
static Button map_joy_button(int b)
{
    static const Button m[16] = { BTN_A, BTN_B, BTN_X, BTN_Y, BTN_COUNT, BTN_COUNT, BTN_L, BTN_R,
                                  BTN_ZL, BTN_ZR, BTN_PLUS, BTN_MINUS, BTN_LEFT, BTN_UP, BTN_RIGHT, BTN_DOWN };
    return b >= 0 && b < 16 ? m[b] : BTN_COUNT;
}

static Button map_key(SDL_Keycode k)
{
    switch (k) {
    case SDLK_UP: return BTN_UP;
    case SDLK_DOWN: return BTN_DOWN;
    case SDLK_LEFT: return BTN_LEFT;
    case SDLK_RIGHT: return BTN_RIGHT;
    case SDLK_RETURN: case SDLK_SPACE: case SDLK_a: return BTN_A;
    case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_b: return BTN_B;
    case SDLK_x: return BTN_X;
    case SDLK_y: return BTN_Y;
    case SDLK_q: return BTN_L;
    case SDLK_e: return BTN_R;
    case SDLK_1: return BTN_ZL;
    case SDLK_2: return BTN_ZR;
    case SDLK_p: case SDLK_TAB: return BTN_PLUS;
    case SDLK_m: case SDLK_MINUS: case SDLK_f: return BTN_MINUS;
    default: return BTN_COUNT;
    }
}

typedef struct AxisState {
    bool left, right, up, down, zl, zr;
} AxisState;
static AxisState s_axis;

static void axis_button(bool *state, bool now, Button b)
{
    if (*state != now) {
        *state = now;
        ui_button(b, now);
    }
}

static void handle_axis(int axis, int value)
{
    const int on = 20000, off = 12000;
    switch (axis) {
    case SDL_CONTROLLER_AXIS_LEFTX:
        axis_button(&s_axis.left, s_axis.left ? value < -off : value < -on, BTN_LEFT);
        axis_button(&s_axis.right, s_axis.right ? value > off : value > on, BTN_RIGHT);
        break;
    case SDL_CONTROLLER_AXIS_LEFTY:
        axis_button(&s_axis.up, s_axis.up ? value < -off : value < -on, BTN_UP);
        axis_button(&s_axis.down, s_axis.down ? value > off : value > on, BTN_DOWN);
        break;
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
        axis_button(&s_axis.zl, value > on, BTN_ZL);
        break;
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
        axis_button(&s_axis.zr, value > on, BTN_ZR);
        break;
    }
}

// ------------------------------------------------------------ test script
// PC: LUDO_SCRIPT environment variable. Wii U/Cemu: SD:/wiiu/apps/pixmogames/autotest.txt
// (only for testing; the file does not exist on a normal SD card).

typedef struct ScriptCmd {
    int frame;
    char cmd[16];
    char arg[96];
} ScriptCmd;
static ScriptCmd s_script[256];
static int s_nscript, s_script_pos;
static Button s_release[8];
static uint32_t s_launch_at, s_launch_logged; // a game was launched at this tick (0 = none)
static int s_nrelease;

static void script_load(void)
{
    char buf[8192];
#ifdef __WIIU__
    char path[256];
    snprintf(path, sizeof(path), "%s/autotest.txt", plat_data_dir());
    char *txt = (char *)file_read(path, NULL);
    if (!txt)
        return;
    str_copy(buf, sizeof(buf), txt);
    free(txt);
    log_msg("autotest script loaded");
#else
    const char *s = getenv("LUDO_SCRIPT");
    if (!s)
        return;
    str_copy(buf, sizeof(buf), s);
#endif
    char *save = NULL;
    for (char *tok = strtok_r(buf, " ;\n", &save); tok && s_nscript < 256; tok = strtok_r(NULL, " ;\n", &save)) {
        ScriptCmd *c = &s_script[s_nscript];
        char *colon = strchr(tok, ':');
        if (!colon)
            continue;
        *colon = 0;
        c->frame = atoi(tok);
        char *eq = strchr(colon + 1, '=');
        if (eq) {
            *eq = 0;
            str_copy(c->arg, sizeof(c->arg), eq + 1);
        }
        str_copy(c->cmd, sizeof(c->cmd), colon + 1);
        s_nscript++;
    }
}

#ifndef __WIIU__
static void save_shot(const char *name)
{
    int w = SCREEN_W, h = SCREEN_H;
    uint8_t *px = malloc((size_t)w * h * 4);
    if (!px)
        return;
    if (SDL_RenderReadPixels(g_ren, NULL, SDL_PIXELFORMAT_RGBA32, px, w * 4) == 0) {
        char path[512];
        const char *dir = getenv("LUDO_SHOTDIR");
        snprintf(path, sizeof(path), "%s/%s.png", dir ? dir : ".", name);
        for (size_t i = 0; i < (size_t)w * h; i++)
            px[i * 4 + 3] = 255;
        stbi_write_png(path, w, h, 4, px, w * 4);
        log_msg("screenshot %s", path);
    }
    free(px);
}
#endif

// returns false to quit
static bool script_step(int frame, bool *shot_pending, char *shot_name)
{
    for (int i = 0; i < s_nrelease; i++)
        ui_button(s_release[i], false);
    s_nrelease = 0;
    static const struct { const char *n; Button b; } names[] = {
        { "a", BTN_A }, { "b", BTN_B }, { "x", BTN_X }, { "y", BTN_Y }, { "l", BTN_L }, { "r", BTN_R },
        { "zl", BTN_ZL }, { "zr", BTN_ZR }, { "plus", BTN_PLUS }, { "minus", BTN_MINUS }, { "up", BTN_UP },
        { "down", BTN_DOWN }, { "left", BTN_LEFT }, { "right", BTN_RIGHT },
    };
    while (s_script_pos < s_nscript && s_script[s_script_pos].frame <= frame) {
        ScriptCmd *c = &s_script[s_script_pos++];
        if (!strcmp(c->cmd, "quit"))
            return false;
        if (!strcmp(c->cmd, "shot")) {
            *shot_pending = true;
            str_copy(shot_name, 96, c->arg);
            log_msg("autotest mark %s", c->arg);
            continue;
        }
        if (!strcmp(c->cmd, "text")) {
            char t[96];
            str_copy(t, sizeof(t), c->arg);
            for (char *p = t; *p; p++)
                if (*p == '_')
                    *p = ' ';
            str_copy(s_text_buf, sizeof(s_text_buf), t);
            text_finish(true);
            continue;
        }
        if (!strcmp(c->cmd, "tap")) {
            float x = 0, y = 0;
            sscanf(c->arg, "%f,%f", &x, &y);
            ui_touch(TOUCH_DOWN, x, y);
            ui_touch(TOUCH_UP, x, y);
            continue;
        }
        for (size_t k = 0; k < sizeof(names) / sizeof(names[0]); k++) {
            if (!strcmp(c->cmd, names[k].n)) {
                ui_button(names[k].b, true);
                if (s_nrelease < 8)
                    s_release[s_nrelease++] = names[k].b;
            }
        }
    }
    return true;
}

// ------------------------------------------------------------ main

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK) != 0) {
        log_msg("SDL_Init: %s", SDL_GetError());
        return 1;
    }
#ifdef __WIIU__
    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
#endif
    log_file_start(plat_data_dir());
    log_msg(APP_NAME " " APP_VERSION " (" __DATE__ " " __TIME__ ")");
    bool scripted = false; // PC script mode: software renderer, fixed time step
    script_load();
#ifndef __WIIU__
    scripted = s_nscript > 0;
#endif
    s_win = SDL_CreateWindow(APP_NAME, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, 0);
    if (!s_win) {
        log_msg("SDL_CreateWindow: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    Uint32 rflags = scripted ? SDL_RENDERER_SOFTWARE : (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Renderer *ren = SDL_CreateRenderer(s_win, -1, rflags);
    if (!ren)
        ren = SDL_CreateRenderer(s_win, -1, 0);
    if (!ren) {
        log_msg("SDL_CreateRenderer: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
    gfx_init(ren);

    uint32_t t0 = SDL_GetTicks();
    const void *ttf = NULL;
    size_t ttf_size = 0;
    if (!plat_font(&ttf, &ttf_size) || !font_init(ttf, ttf_size))
        log_msg("font init failed");
    // a loading frame (same look as the .wuhb splash) while the rest starts
    gfx_rect_vgrad(0, 0, SCREEN_W, 461, rgb(0x0D0221), rgb(0x3E0F8A));
    gfx_rect_vgrad(0, 461, SCREEN_W, SCREEN_H - 461, rgb(0x2A0A63), rgb(0x0D0221));
    gfx_rect(0, 459, SCREEN_W, 3, rgba(0x05D9E8, 0.85f));
    brand_draw_icon((SCREEN_W - 245) * 0.5f, 72, 245, 1);
    brand_draw_name((SCREEN_W - brand_name_width(10, false)) * 0.5f, 382, 10, false, rgb(0xFFFFFF));
    SDL_RenderPresent(ren);
    uint32_t t1 = SDL_GetTicks();
    plat_init();
    static const int warm[] = { 19, 20, 22, 28, 40 };
    font_prewarm(warm, 5);
    uint32_t t2 = SDL_GetTicks();
    icons_init(ren);
    uint32_t t3 = SDL_GetTicks();
    lib_init();
    uint32_t t4 = SDL_GetTicks();
    if (!scripted || getenv("LUDO_AUDIO"))
        audio_init();
    ui_init();
    uint32_t t5 = SDL_GetTicks();
    log_msg("startup ms: font %u, glyphs %u, icons %u, library %u, audio+ui %u (%d games)", t1 - t0, t2 - t1,
            t3 - t2, t4 - t3, t5 - t4, L.count);

    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i))
            SDL_GameControllerOpen(i);
        else
            SDL_JoystickOpen(i);

    bool running = true;
    uint64_t last = SDL_GetPerformanceCounter();
    int frame = 0;
    char shot_name[96];
    bool shot_pending = false;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT:
                log_msg("el sistema pide cerrar la app");
                running = false;
                break;
            case SDL_APP_WILLENTERBACKGROUND:
            case SDL_APP_DIDENTERBACKGROUND:
                if (!ui_is_background())
                    log_msg("en segundo plano");
                ui_background(true);
                break;
            case SDL_APP_DIDENTERFOREGROUND:
                log_msg("de vuelta en primer plano");
                ui_background(false);
                break;
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_HIDDEN)
                    ui_background(true);
                else if (e.window.event == SDL_WINDOWEVENT_SHOWN)
                    ui_background(false);
                break;
            case SDL_CONTROLLERDEVICEADDED:
                SDL_GameControllerOpen(e.cdevice.which);
                break;
            case SDL_JOYDEVICEADDED:
                if (!SDL_IsGameController(e.jdevice.which))
                    SDL_JoystickOpen(e.jdevice.which);
                break;
            case SDL_CONTROLLERBUTTONDOWN:
            case SDL_CONTROLLERBUTTONUP:
                if (!s_text_active)
                    ui_button(map_controller_button(e.cbutton.button), e.type == SDL_CONTROLLERBUTTONDOWN);
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (!s_text_active)
                    handle_axis(e.caxis.axis, e.caxis.value);
                break;
            case SDL_JOYBUTTONDOWN:
            case SDL_JOYBUTTONUP:
                if (!s_text_active && !SDL_GameControllerFromInstanceID(e.jbutton.which))
                    ui_button(map_joy_button(e.jbutton.button), e.type == SDL_JOYBUTTONDOWN);
                break;
            case SDL_FINGERDOWN:
            case SDL_FINGERMOTION:
            case SDL_FINGERUP:
                if (!s_text_active)
                    ui_touch(e.type == SDL_FINGERDOWN ? TOUCH_DOWN : e.type == SDL_FINGERUP ? TOUCH_UP : TOUCH_MOVE,
                             e.tfinger.x * SCREEN_W, e.tfinger.y * SCREEN_H);
                break;
#ifndef __WIIU__
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                if (!s_text_active && e.button.button == SDL_BUTTON_LEFT)
                    ui_touch(e.type == SDL_MOUSEBUTTONDOWN ? TOUCH_DOWN : TOUCH_UP, (float)e.button.x,
                             (float)e.button.y);
                break;
            case SDL_MOUSEMOTION:
                if (!s_text_active && (e.motion.state & SDL_BUTTON_LMASK))
                    ui_touch(TOUCH_MOVE, (float)e.motion.x, (float)e.motion.y);
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP:
                if (s_text_active) {
                    if (e.type == SDL_KEYDOWN) {
                        if (e.key.keysym.sym == SDLK_RETURN)
                            text_finish(true);
                        else if (e.key.keysym.sym == SDLK_ESCAPE)
                            text_finish(false);
                        else if (e.key.keysym.sym == SDLK_BACKSPACE) {
                            size_t l = strlen(s_text_buf);
                            while (l > 0 && ((unsigned char)s_text_buf[l - 1] & 0xC0) == 0x80)
                                l--;
                            if (l > 0)
                                s_text_buf[l - 1] = 0;
                        }
                    }
                } else if (!e.key.repeat) {
                    ui_button(map_key(e.key.keysym.sym), e.type == SDL_KEYDOWN);
                }
                break;
#endif
            case SDL_TEXTINPUT:
                if (s_text_active && s_text_collect)
                    strncat(s_text_buf, e.text.text, sizeof(s_text_buf) - strlen(s_text_buf) - 1);
                break;
#ifdef __WIIU__
            case SDL_SYSWMEVENT:
                if (e.syswm.msg && e.syswm.msg->subsystem == SDL_SYSWM_WIIU) {
                    int ev = e.syswm.msg->msg.wiiu.event;
                    if (ev == SDL_WIIU_SYSWM_SWKBD_OK_START_EVENT) {
                        s_text_buf[0] = 0;
                        s_text_collect = true;
                    } else if (ev == SDL_WIIU_SYSWM_SWKBD_OK_FINISH_EVENT) {
                        text_finish(true);
                    } else if (ev == SDL_WIIU_SYSWM_SWKBD_CANCEL_EVENT) {
                        text_finish(false);
                    }
                }
                break;
#endif
            default:
                break;
            }
        }
        if (!running)
            break;

        uint64_t now = SDL_GetPerformanceCounter();
        float dt = (float)((double)(now - last) / (double)SDL_GetPerformanceFrequency());
        last = now;
        if (dt > 0.1f)
            dt = 0.1f;
        if (s_nscript > 0) {
            if (scripted) {
                dt = 1.0f / 60.0f;
                // give the loader threads time so screenshots are deterministic enough
                SDL_Delay(2);
            }
            if (!script_step(frame, &shot_pending, shot_name))
                running = false;
        }
        frame++;

        if (ui_quit_requested())
            running = false;
        uint64_t tid;
        if (ui_take_launch(&tid)) {
            Game *lg = lib_find(tid);
            // volver.txt tells the Aroma plugin (plugin/) to open Pixmo Games again
            // when the Wii U Menu starts after this game is closed
            char marker[256];
            data_path("volver.txt", marker, sizeof(marker));
            // "wii": the console reboots when Wii mode ends (Aroma starts again), so the
            // plugin must not take the marker for one left by powering off in a game
            if (lg && L.set.volver)
                file_write_atomic(marker, lg->pt.wii ? "wii\n" : "1\n", lg->pt.wii ? 4 : 2);
            // nothing may be reading the console's storage while the system prepares the game
            if (lg)
                lib_stop_for_launch();
            if (!lg || !plat_launch(&lg->pt)) {
                remove(marker);
                lib_resume_after_launch();
                ui_launch_failed();
            } else {
                s_launch_at = SDL_GetTicks();
            }
        }
        // after a launch the system takes over: one line per second until it asks the
        // app to close (if a launch hangs, log.txt shows whether the app kept running)
        if (s_launch_at) {
            uint32_t secs = (SDL_GetTicks() - s_launch_at) / 1000;
            if (secs > s_launch_logged) {
                s_launch_logged = secs;
                log_msg("esperando al sistema: %u s%s", secs, ui_is_background() ? " (en segundo plano)" : "");
            }
        }

        if (ui_is_background()) {
            // the GPU is not ours while the HOME Menu is open: textures
            // created now would stay empty, so wait until we are back
            SDL_Delay(16);
            continue;
        }
        uint32_t f0 = SDL_GetTicks();
        ui_update(dt);
        uint32_t f1 = SDL_GetTicks();
        ui_render();
#ifndef __WIIU__
        draw_pc_text_box();
        if (shot_pending)
            save_shot(shot_name);
#endif
        shot_pending = false;
        uint32_t f2 = SDL_GetTicks();
        SDL_RenderPresent(ren);
        uint32_t f3 = SDL_GetTicks();

        // health report in log.txt: slow frames and a heartbeat every 5 s
        static uint32_t beat, worst, frames;
        uint32_t total = f3 - f0;
        frames++;
        if (total > worst)
            worst = total;
        if (total > 400)
            log_msg("cuadro lento: %u ms (logica %u, dibujo %u, presentar %u)", total, f1 - f0, f2 - f1, f3 - f2);
        if (f3 - beat >= 5000) {
            log_msg("vivo: %u cuadros en %u ms, peor %u ms, %d juegos", frames, f3 - beat, worst, L.count);
            beat = f3;
            worst = frames = 0;
        }
    }

    log_msg("saliendo");
    lib_shutdown();
    audio_shutdown();
    ui_shutdown();
    icons_shutdown();
    font_shutdown();
    plat_shutdown();
    log_msg("fin");
    log_file_stop();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(s_win);
    SDL_Quit();
    return 0;
}

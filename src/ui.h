// Interfaz de Pixmo Games: entrada, estado, vistas y paneles.
#pragma once

#include "common.h"

typedef enum Button {
    BTN_A, BTN_B, BTN_X, BTN_Y, BTN_L, BTN_R, BTN_ZL, BTN_ZR,
    BTN_PLUS, BTN_MINUS, BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT,
    BTN_COUNT
} Button;

enum { TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP };

void ui_init(void);
void ui_shutdown(void);
void ui_button(Button b, bool down);
void ui_touch(int type, float x, float y);
void ui_text_result(const char *text, bool ok);
void ui_update(float dt);
void ui_render(void);
void ui_background(bool bg);
bool ui_is_background(void);
bool ui_quit_requested(void);
// set when the user started a game: main launches it after the animation
bool ui_take_launch(uint64_t *tid);
// the system refused to start it: back to the library with a message
void ui_launch_failed(void);

// implemented by main.c (platform text input: Wii U software keyboard)
void sys_text_input(const char *hint, const char *initial, const char *ok_label);

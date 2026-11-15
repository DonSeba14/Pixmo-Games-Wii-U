// Iconos vectoriales generados al iniciar (campos de distancia con signo).
#pragma once

#include "common.h"

typedef enum IconId {
    // icons offered for categories (logo picker)
    IC_GAMEPAD, IC_CROSSHAIR, IC_SWORD, IC_CAR, IC_PUZZLE, IC_STAR, IC_HEART, IC_NOTE,
    IC_BALL, IC_BOLT, IC_GHOST, IC_ROCKET, IC_TROPHY, IC_CROWN, IC_PEOPLE, IC_SHIELD,
    IC_PAW, IC_GEM, IC_FLAME, IC_PLANET, IC_LEAF, IC_DICE, IC_MUSHROOM, IC_MOUNTAIN,
    IC_BOOK, IC_CLOCK, IC_ALL, IC_FLAG,
    IC_CATEGORY_COUNT,
    // interface icons
    IC_SEARCH = IC_CATEGORY_COUNT, IC_GEAR, IC_PLAY, IC_USB, IC_CHIP, IC_DISC, IC_CHECK,
    IC_PLUS, IC_TRASH, IC_PENCIL, IC_EYE, IC_EYE_OFF, IC_INFO, IC_DOWNLOAD, IC_IMAGE,
    IC_TAG, IC_SORT, IC_PALETTE, IC_SOUND, IC_MOON, IC_CAROUSEL, IC_REFRESH, IC_POWER,
    IC_STAR_OUTLINE, IC_ARROW_UP, IC_ARROW_DOWN, IC_HISTORY, IC_BACK,
    IC_COUNT
} IconId;

bool icons_init(SDL_Renderer *r);
void icons_shutdown(void);
SDL_Texture *icon_tex(int id);
// Draws an icon tinted with color c inside a size x size box.
void icon_draw(int id, float x, float y, float size, Color c);
const char *icon_name(int id);

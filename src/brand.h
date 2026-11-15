// Marca de Pixmo Games: el icono (la mascota) y el nombre con la letra de
// píxeles 5x7 de Pixmo Games de PC.
#pragma once

#include "common.h"

// Icon (rounded square with the mascot), size x size.
void  brand_draw_icon(float x, float y, float size, float alpha);
// "PIXMO GAMES" in pixel letters, `cell` px per pixel; two_lines puts GAMES under PIXMO.
// Colors: PIXMO ice, GAMES cyan, multiplied by `tint` (white = brand colors).
void  brand_draw_name(float x, float y, int cell, bool two_lines, Color tint);
float brand_name_width(int cell, bool two_lines);
float brand_name_height(int cell, bool two_lines);

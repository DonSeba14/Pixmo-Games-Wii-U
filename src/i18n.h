// Idioma de la interfaz. El código escribe los textos en español; con la consola
// en otro idioma (o "Idioma: English" en Ajustes) se muestran en inglés.
//
// Las funciones de texto (text_draw, text_width, text_fit, text_wrap) pasan todo
// lo que dibujan por tr(), así que una etiqueta fija solo necesita su entrada en
// la tabla de i18n.c. Los textos armados con snprintf traducen el formato:
//   snprintf(buf, sizeof(buf), tr("%d juegos"), n);
// tools/check_i18n.py revisa que la tabla esté completa.
#pragma once

#include <stdbool.h>

enum { LANG_AUTO = 0, LANG_ES = 1, LANG_EN = 2 };

// setting = LANG_*; LANG_AUTO follows the console language (Spanish or else English)
void i18n_set(int setting);
bool i18n_english(void);
// English text for a Spanish UI string (the same pointer if there is none, or in Spanish)
const char *tr(const char *es) __attribute__((format_arg(1)));

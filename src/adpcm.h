// Archivos WAV con IMA ADPCM (4 bits por muestra, bloques independientes):
// la música de la SD (tools/musica_consola.py). Sin dependencias de SDL.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct AdpcmWav {
    const uint8_t *blocks;  // first block inside the file
    size_t nblocks;
    int rate, channels;     // channels: 1 or 2
    int align;              // bytes per block
    int spb;                // samples per channel in each block
    char title[128];        // LIST/INFO/INAM, UTF-8 ("" if missing)
} AdpcmWav;

// Validates the RIFF/WAVE headers (format 0x11, 4 bits). `file` must stay alive.
bool adpcm_wav_parse(const uint8_t *file, size_t size, AdpcmWav *out);
// Decodes block `b` into `out` (spb * channels interleaved samples). Returns spb.
int adpcm_decode_block(const AdpcmWav *w, size_t b, int16_t *out);

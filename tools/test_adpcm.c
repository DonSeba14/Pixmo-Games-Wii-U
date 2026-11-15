// Prueba del decodificador IMA ADPCM de src/adpcm.c: decodifica un WAV y
// escribe PCM s16le crudo por stdout para compararlo con ffmpeg.
//   gcc -O2 -Isrc tools/test_adpcm.c src/adpcm.c -o /tmp/test_adpcm
//   /tmp/test_adpcm x.wav > mio.raw; ffmpeg -i x.wav -f s16le - > ref.raw; cmp mio.raw ref.raw
#include "adpcm.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc((size_t)size);
    if (!data || fread(data, 1, (size_t)size, f) != (size_t)size)
        return 1;
    fclose(f);
    AdpcmWav w;
    if (!adpcm_wav_parse(data, (size_t)size, &w)) {
        fprintf(stderr, "no es IMA ADPCM\n");
        return 1;
    }
    fprintf(stderr, "rate %d ch %d align %d spb %d bloques %zu titulo '%s'\n", w.rate, w.channels, w.align, w.spb,
            w.nblocks, w.title);
    int16_t *pcm = malloc(sizeof(int16_t) * (size_t)w.spb * (size_t)w.channels);
    for (size_t b = 0; b < w.nblocks; b++) {
        int n = adpcm_decode_block(&w, b, pcm);
        fwrite(pcm, sizeof(int16_t), (size_t)n * (size_t)w.channels, stdout);
    }
    free(pcm);
    free(data);
    return 0;
}

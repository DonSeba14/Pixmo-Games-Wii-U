// IMA ADPCM de Microsoft (WAVE_FORMAT_IMA_ADPCM = 0x11). Cada bloque trae,
// por canal, la muestra inicial (16 bits) y el índice del paso; luego grupos
// de 4 bytes por canal (8 muestras, nibble bajo primero).
#include "adpcm.h"

#include <string.h>

static const int16_t k_step[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767,
};
static const int8_t k_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

bool adpcm_wav_parse(const uint8_t *f, size_t size, AdpcmWav *w)
{
    memset(w, 0, sizeof(*w));
    if (size < 12 || memcmp(f, "RIFF", 4) || memcmp(f + 8, "WAVE", 4))
        return false;
    bool fmt_ok = false;
    size_t pos = 12;
    while (pos + 8 <= size) {
        const uint8_t *c = f + pos;
        size_t len = rd32(c + 4);
        size_t body = pos + 8;
        if (body + len > size)
            len = size - body; // truncated file: use what is there
        if (!memcmp(c, "fmt ", 4) && len >= 20) {
            const uint8_t *b = f + body;
            if (rd16(b) != 0x11 || rd16(b + 14) != 4)
                return false;
            w->channels = rd16(b + 2);
            w->rate = (int)rd32(b + 4);
            w->align = rd16(b + 12);
            w->spb = rd16(b + 18);
            if (w->channels < 1 || w->channels > 2 || w->rate < 8000 || w->rate > 96000 ||
                w->align <= 4 * w->channels)
                return false;
            // samples per block follow from the block size
            int expect = (w->align - 4 * w->channels) * 2 / w->channels + 1;
            if (w->spb != expect)
                w->spb = expect;
            fmt_ok = true;
        } else if (!memcmp(c, "LIST", 4) && len >= 4 && !memcmp(f + body, "INFO", 4)) {
            size_t p = body + 4, end = body + len;
            while (p + 8 <= end) {
                size_t sl = rd32(f + p + 4);
                if (p + 8 + sl > end)
                    break;
                if (!memcmp(f + p, "INAM", 4)) {
                    size_t n = sl < sizeof(w->title) - 1 ? sl : sizeof(w->title) - 1;
                    memcpy(w->title, f + p + 8, n);
                    w->title[n] = 0;
                }
                p += 8 + sl + (sl & 1);
            }
        } else if (!memcmp(c, "data", 4)) {
            if (!fmt_ok)
                return false;
            w->blocks = f + body;
            w->nblocks = len / (size_t)w->align;
            return w->nblocks > 0;
        }
        pos = body + len + (len & 1);
    }
    return false;
}

int adpcm_decode_block(const AdpcmWav *w, size_t b, int16_t *out)
{
    const int ch = w->channels;
    const uint8_t *p = w->blocks + b * (size_t)w->align;
    int pred[2], idx[2];
    for (int c = 0; c < ch; c++) {
        pred[c] = (int16_t)rd16(p + c * 4);
        idx[c] = p[c * 4 + 2];
        if (idx[c] > 88)
            idx[c] = 88;
        out[c] = (int16_t)pred[c];
    }
    p += 4 * ch;
    // after the header: for every 8 samples, 4 bytes of each channel in turn
    int groups = (w->spb - 1) / 8;
    for (int gi = 0; gi < groups; gi++) {
        for (int c = 0; c < ch; c++) {
            int16_t *o = out + (1 + gi * 8) * ch + c;
            for (int k = 0; k < 4; k++) {
                uint8_t byte = *p++;
                for (int h = 0; h < 2; h++) {
                    int n = h ? byte >> 4 : byte & 15;
                    // (2d+1)*step/8, as ffmpeg's encoder models it (the shift-and-add
                    // form of the reference decoder rounds differently and drifts)
                    int diff = ((2 * (n & 7) + 1) * k_step[idx[c]]) >> 3;
                    pred[c] += (n & 8) ? -diff : diff;
                    if (pred[c] > 32767)
                        pred[c] = 32767;
                    else if (pred[c] < -32768)
                        pred[c] = -32768;
                    idx[c] += k_index[n];
                    if (idx[c] < 0)
                        idx[c] = 0;
                    else if (idx[c] > 88)
                        idx[c] = 88;
                    *o = (int16_t)pred[c];
                    o += ch;
                }
            }
        }
    }
    return w->spb;
}

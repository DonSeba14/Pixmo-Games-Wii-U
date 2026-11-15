// Audio: efectos pre-sintetizados al iniciar y una música ambiental suave
// (pad, bajo y campanitas pentatónicas con eco) generada en el callback.
#include "audio.h"
#include "adpcm.h"
#include "platform.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define RATE 48000
#define SINE_BITS 12
#define SINE_N (1 << SINE_BITS)
#define TWO32_OVER_2PI 683565275.57643f

static SDL_AudioDeviceID s_dev;
static PlatThread *s_synth;
static SDL_atomic_t s_sfx_ready;
static float s_sine[SINE_N];
static float *s_sfx[SFX_COUNT];
static int s_sfx_len[SFX_COUNT];
static bool s_sfx_on = true, s_music_on = true;
static float s_music_level, s_music_target = 1.0f, s_music_fade_rate = 1.0f;
static float s_music_user = 1.0f;

typedef struct SfxVoice {
    int id;
    float pos, rate, gain;
} SfxVoice;
static SfxVoice s_voices[12];

static inline float sine_fp(uint32_t phase)
{
    return s_sine[phase >> (32 - SINE_BITS)];
}

static inline uint32_t hz_inc(float hz)
{
    return (uint32_t)(hz * (4294967296.0 / RATE));
}

static float midi_hz(float m)
{
    return 440.0f * powf(2.0f, (m - 69.0f) / 12.0f);
}

// ---------------------------------------------------------------- effects
// Rendered once at startup with table oscillators and multiplicative
// envelopes (no sinf/expf per sample), pausing every few milliseconds:
// on the console a busy thread would starve system services.

static uint32_t s_coop;

static float *make_buf(int id, float seconds)
{
    s_sfx_len[id] = (int)(seconds * RATE);
    s_sfx[id] = calloc((size_t)s_sfx_len[id], sizeof(float));
    return s_sfx[id];
}

// FM bell: carrier hz, modulator 3.5x, envelope e^(-t*decay), index e^(-2t*decay)
static void add_bell(int id, float start_s, float hz, float decay, float index, float gain)
{
    float *b = s_sfx[id];
    int n = s_sfx_len[id], i0 = (int)(start_s * RATE);
    uint32_t pc = 0, pm = 0, ic = hz_inc(hz), im = hz_inc(hz * 3.5f);
    float env = gain, envk = expf(-decay / RATE), idx = index, idxk = expf(-2.0f * decay / RATE);
    for (int i = i0; i < n; i++) {
        pm += im;
        float m = sine_fp(pm) * idx;
        pc += ic;
        float att = i - i0 < 96 ? (i - i0) / 96.0f : 1.0f;
        b[i] += sine_fp(pc + (uint32_t)(int32_t)(m * TWO32_OVER_2PI)) * env * att;
        env *= envk;
        idx *= idxk;
        if (env < 1e-5f)
            break;
        if ((i & 2047) == 0)
            plat_coop(&s_coop);
    }
}

// Sine (plus optional octave) with an exponential attack and decay; the
// frequency glides linearly from hz0 to hz1 during glide_s.
static void add_tone(int id, float start_s, float hz0, float hz1, float glide_s, float attack, float decay,
                     float gain, float octave)
{
    float *b = s_sfx[id];
    int n = s_sfx_len[id], i0 = (int)(start_s * RATE), gl = (int)(glide_s * RATE);
    uint32_t ph = 0, ph2 = 0;
    float a = 1.0f, ak = expf(-attack / RATE), env = gain, envk = expf(-decay / RATE);
    for (int i = i0; i < n; i++) {
        int k = i - i0;
        float hz = gl > 0 && k < gl ? hz0 + (hz1 - hz0) * k / gl : hz1;
        uint32_t inc = hz_inc(hz);
        ph += inc;
        ph2 += inc * 2;
        b[i] += (sine_fp(ph) + sine_fp(ph2) * octave) * env * (1.0f - a);
        a *= ak;
        env *= envk;
        if (env < 1e-5f)
            break;
        if ((k & 2047) == 0)
            plat_coop(&s_coop);
    }
}

static void synth_effects(void)
{
    s_coop = SDL_GetTicks();

    make_buf(SFX_MOVE, 0.07f);
    add_tone(SFX_MOVE, 0, 1650, 1650, 0, 2000, 85, 0.24f * 0.8f, 0.25f);

    make_buf(SFX_TAB, 0.14f);
    add_tone(SFX_TAB, 0, 620, 1240, 0.05f, 900, 30, 0.26f, 0);

    make_buf(SFX_SELECT, 0.6f);
    add_bell(SFX_SELECT, 0, midi_hz(88), 7.0f, 1.2f, 0.55f * 0.24f);
    add_bell(SFX_SELECT, 0.075f, midi_hz(95), 6.0f, 1.0f, 0.5f * 0.24f);

    make_buf(SFX_BACK, 0.35f);
    add_tone(SFX_BACK, 0, midi_hz(79), midi_hz(79), 0, 2000, 22, 0.2f, 0);
    add_tone(SFX_BACK, 0.06f, midi_hz(72), midi_hz(72), 0, 2000, 14, 0.2f, 0);

    float *b = make_buf(SFX_OPEN, 0.4f);
    {
        uint32_t seed = 12345;
        float lp = 0, lp2 = 0;
        int n = s_sfx_len[SFX_OPEN];
        for (int i = 0; i < n; i++) {
            float t = (float)i / RATE;
            seed = seed * 1664525u + 1013904223u;
            float noise = ((seed >> 9) / 4194304.0f) * 2.0f - 1.0f;
            float cut = 0.02f + 0.25f * clampf(t / 0.2f, 0, 1);
            lp += (noise - lp) * cut;
            lp2 += (lp - lp2) * cut;
            float w = clampf(t / 0.3f, 0, 1);
            float env = w < 1 ? sine_fp((uint32_t)(w * 2147483648.0f)) : 0; // sin(w*pi)
            b[i] = lp2 * env * 0.5f * 0.3f;
        }
        add_bell(SFX_OPEN, 0.08f, midi_hz(84), 8.0f, 0.8f, 0.25f * 0.3f);
    }

    make_buf(SFX_TOGGLE_ON, 0.12f);
    add_tone(SFX_TOGGLE_ON, 0, 800, 1450, 0.06f, 1200, 38, 0.24f, 0);
    make_buf(SFX_TOGGLE_OFF, 0.12f);
    add_tone(SFX_TOGGLE_OFF, 0, 1250, 700, 0.06f, 1200, 38, 0.24f, 0);

    make_buf(SFX_LAUNCH, 1.6f);
    {
        static const int notes[6] = { 72, 76, 79, 84, 88, 91 };
        for (int k = 0; k < 6; k++)
            add_bell(SFX_LAUNCH, k * 0.06f, midi_hz(notes[k]), 3.2f, 1.1f, (0.5f - k * 0.04f) * 0.2f);
        // soft pad under the arpeggio
        add_tone(SFX_LAUNCH, 0, midi_hz(60), midi_hz(60), 0, 4, 1.8f, 0.25f * 0.2f, 0);
        add_tone(SFX_LAUNCH, 0, midi_hz(67), midi_hz(67), 0, 4, 1.8f, 0.7f * 0.25f * 0.2f, 0);
    }

    b = make_buf(SFX_ERROR, 0.3f);
    {
        int n = s_sfx_len[SFX_ERROR];
        uint32_t inc = hz_inc(233);
        for (int k = 0; k < 2; k++) {
            int i0 = (int)(k * 0.13f * RATE), len = (int)(0.11f * RATE);
            uint32_t ph = 0;
            for (int i = 0; i < len && i0 + i < n; i++) {
                ph += inc;
                float w = sine_fp((uint32_t)((float)i / len * 2147483648.0f));
                b[i0 + i] += (sine_fp(ph) + sine_fp(ph * 3) * 0.25f) * w * 0.2f;
            }
        }
    }

    make_buf(SFX_FAV, 0.7f);
    {
        static const int notes[3] = { 91, 96, 100 };
        for (int k = 0; k < 3; k++)
            add_bell(SFX_FAV, k * 0.05f, midi_hz(notes[k]), 7.0f, 0.9f, 0.45f * 0.2f);
    }
}

// ---------------------------------------------------------------- music

typedef struct MVoice {
    uint32_t pc, pm, incc, incm;
    float amp, mul, index, index_mul, pan;
    bool on;
} MVoice;

#define NMEL 8
static MVoice s_mel[NMEL];
static int s_mel_next;
static MVoice s_bass;
static uint32_t s_pad_ph[8], s_pad_inc[8], s_pad_inc_next[8];
static float s_pad_amp[8];
static int s_chord = -1;
static int64_t s_eighth;
static int s_to_event;
static uint32_t s_rng = 0x1234567u;
static int s_last_note = -1;

#define DELAY_N 13500
static float s_delay_l[DELAY_N], s_delay_r[DELAY_N];
static int s_delay_pos;

static const int k_roots[4] = { 36, 33, 29, 31 };            // C2 A1 F1 G1
static const int k_pads[4][4] = {
    { 64, 67, 71, 74 },  // Cmaj9: E G B D
    { 60, 64, 67, 71 },  // Am9:  C E G B
    { 57, 60, 64, 67 },  // Fmaj9: A C E G
    { 59, 62, 67, 69 },  // G6/9: B D G A
};
static const int k_scale[10] = { 72, 74, 76, 79, 81, 84, 86, 88, 91, 93 }; // C major pentatonic

#define SAMPLES_PER_8TH 18000   // 80 BPM
#define EIGHTHS_PER_CHORD 16

static uint32_t rnd(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return s_rng >> 8;
}

static void start_bell(MVoice *v, float hz, float amp, float pan)
{
    v->pc = v->pm = 0;
    v->incc = hz_inc(hz);
    v->incm = hz_inc(hz * 3.5f);
    v->amp = amp;
    v->mul = expf(-1.6f / RATE);
    v->index = 1.3f;
    v->index_mul = expf(-5.0f / RATE);
    v->pan = pan;
    v->on = true;
}

static void music_event(int64_t eighth)
{
    int chord = (int)((eighth / EIGHTHS_PER_CHORD) % 4);
    int pos = (int)(eighth % EIGHTHS_PER_CHORD);
    if (chord != s_chord) {
        s_chord = chord;
        for (int i = 0; i < 4; i++) {
            float hz = midi_hz((float)k_pads[chord][i]);
            s_pad_inc_next[i * 2] = hz_inc(hz * 0.9985f);
            s_pad_inc_next[i * 2 + 1] = hz_inc(hz * 1.0015f);
        }
    }
    if (pos == 0 || pos == 8) {
        s_bass.pc = 0;
        s_bass.incc = hz_inc(midi_hz((float)k_roots[chord] + 12));
        s_bass.amp = 0.16f;
        s_bass.mul = expf(-1.4f / RATE);
        s_bass.on = true;
    }
    // melody: sparse pentatonic bells, a bit denser on strong beats
    uint32_t chance = (pos % 4 == 0) ? 60 : (pos % 2 == 0) ? 40 : 22;
    if ((rnd() % 100) < chance) {
        int idx;
        if (s_last_note < 0)
            idx = (int)(rnd() % 10);
        else {
            int step = (int)(rnd() % 5) - 2;
            if (step == 0)
                step = 1;
            idx = s_last_note + step;
            if (idx < 0) idx = 1;
            if (idx > 9) idx = 8;
        }
        s_last_note = idx;
        float pan = ((rnd() % 100) / 100.0f) * 0.8f - 0.4f;
        start_bell(&s_mel[s_mel_next], midi_hz((float)k_scale[idx]), 0.075f, pan);
        s_mel_next = (s_mel_next + 1) % NMEL;
    }
}

static void music_render(float *l, float *r, int n)
{
    for (int i = 0; i < n; i++) {
        if (s_to_event <= 0) {
            music_event(s_eighth++);
            s_to_event = SAMPLES_PER_8TH;
        }
        s_to_event--;
        float ml = 0, mr = 0;
        // pad: crossfade toward the next chord's frequencies by gliding the
        // amplitude down and swapping the oscillator when it is quiet
        for (int k = 0; k < 8; k++) {
            if (s_pad_inc[k] != s_pad_inc_next[k]) {
                s_pad_amp[k] *= 0.99985f;
                if (s_pad_amp[k] < 0.002f)
                    s_pad_inc[k] = s_pad_inc_next[k];
            } else if (s_pad_amp[k] < 1.0f) {
                s_pad_amp[k] += 1.0f / (RATE * 1.2f);
            }
            s_pad_ph[k] += s_pad_inc[k];
            float v = sine_fp(s_pad_ph[k]) * s_pad_amp[k] * 0.022f;
            if (k & 1) mr += v; else ml += v;
        }
        if (s_bass.on) {
            s_bass.pc += s_bass.incc;
            float v = sine_fp(s_bass.pc) * s_bass.amp;
            s_bass.amp *= s_bass.mul;
            if (s_bass.amp < 0.0005f)
                s_bass.on = false;
            ml += v;
            mr += v;
        }
        for (int k = 0; k < NMEL; k++) {
            MVoice *v = &s_mel[k];
            if (!v->on)
                continue;
            v->pm += v->incm;
            float m = sine_fp(v->pm) * v->index;
            v->pc += v->incc;
            float s = sine_fp(v->pc + (uint32_t)(int32_t)(m * TWO32_OVER_2PI)) * v->amp;
            v->amp *= v->mul;
            v->index *= v->index_mul;
            if (v->amp < 0.0003f)
                v->on = false;
            ml += s * (0.5f - v->pan);
            mr += s * (0.5f + v->pan);
        }
        // ping-pong echo for space
        float dl = s_delay_l[s_delay_pos], dr = s_delay_r[s_delay_pos];
        s_delay_l[s_delay_pos] = ml * 0.5f + dr * 0.38f;
        s_delay_r[s_delay_pos] = mr * 0.5f + dl * 0.38f;
        s_delay_pos = (s_delay_pos + 1) % DELAY_N;
        l[i] += ml + dl * 0.45f;
        r[i] += mr + dr * 0.45f;
    }
}

// ---------------------------------------------------------------- songs
// Songs from <data>/musica/*.wav (IMA ADPCM, tools/musica_consola.py): shuffled
// and played one after another, reshuffled at the end of the list, like Pixmo
// Games on PC. A thread on the work core reads the next song while the current
// one plays; the callback decodes and resamples. Without songs, the
// synthesized ambient music plays.

#define SONG_GAIN 0.32f // about -10 dB, the level Pixmo Games uses

typedef struct Track {
    uint8_t *file;
    AdpcmWav w;
    int16_t *pcm; // one decoded block
    size_t blk;
    int npcm, ipcm;
    float a[2], b[2], t; // resampler: two source frames and the position between them
    bool done;
} Track;

static SDL_atomic_t s_songs;      // 1 = songs found: the synth stays quiet
static void *s_next_track;        // loaded by the song thread (atomic pointer)
static void *s_dead_track;        // finished, freed by the song thread (atomic pointer)
static Track *s_cur;              // owned by the callback
static SDL_sem *s_song_sem;
static PlatThread *s_song_thread;
static SDL_atomic_t s_song_quit;
static SDL_atomic_t s_song_seq;   // +1 every time a song starts
static char s_song_title[128];

static void track_free(Track *t)
{
    if (!t)
        return;
    free(t->pcm);
    free(t->file);
    free(t);
}

static void track_frame(Track *t, float out[2])
{
    if (t->ipcm >= t->npcm) {
        if (t->blk >= t->w.nblocks) {
            t->done = true;
            out[0] = out[1] = 0;
            return;
        }
        t->npcm = adpcm_decode_block(&t->w, t->blk++, t->pcm);
        t->ipcm = 0;
    }
    const int16_t *s = t->pcm + t->ipcm * t->w.channels;
    out[0] = s[0] * (1.0f / 32768.0f);
    out[1] = s[t->w.channels - 1] * (1.0f / 32768.0f);
    t->ipcm++;
}

static Track *track_load(const char *path)
{
    size_t size = 0;
    uint8_t *data = file_read(path, &size);
    if (!data)
        return NULL;
    Track *t = calloc(1, sizeof(*t));
    if (!t || !adpcm_wav_parse(data, size, &t->w) ||
        !(t->pcm = malloc(sizeof(int16_t) * (size_t)t->w.spb * (size_t)t->w.channels))) {
        free(data);
        if (t)
            free(t->pcm);
        free(t);
        return NULL;
    }
    t->file = data;
    track_frame(t, t->a);
    track_frame(t, t->b);
    return t;
}

// Adds up to n frames of the track; returns how many it produced before ending.
static int track_render(Track *t, float *l, float *r, int n, float gain)
{
    const float step = (float)t->w.rate / RATE;
    int i = 0;
    for (; i < n && !t->done; i++) {
        l[i] += (t->a[0] + (t->b[0] - t->a[0]) * t->t) * gain;
        r[i] += (t->a[1] + (t->b[1] - t->a[1]) * t->t) * gain;
        t->t += step;
        while (t->t >= 1.0f) {
            t->t -= 1.0f;
            t->a[0] = t->b[0];
            t->a[1] = t->b[1];
            track_frame(t, t->b);
        }
    }
    return i;
}

static void songs_render(float *l, float *r, int n, float level)
{
    int i = 0;
    while (i < n) {
        if (!s_cur || s_cur->done) {
            // hand the finished song to the thread, take the next one
            if (s_cur && !SDL_AtomicCASPtr(&s_dead_track, NULL, s_cur))
                return; // the previous one is not freed yet: silence for now
            s_cur = NULL;
            Track *nx = SDL_AtomicSetPtr(&s_next_track, NULL);
            SDL_SemPost(s_song_sem);
            if (!nx)
                return; // still loading
            s_cur = nx;
            str_copy(s_song_title, sizeof(s_song_title), nx->w.title);
            SDL_AtomicIncRef(&s_song_seq);
        }
        i += track_render(s_cur, l + i, r + i, n - i, level * SONG_GAIN);
    }
}

static int song_cmp(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int song_thread(void *arg)
{
    (void)arg;
    char dir[300];
    snprintf(dir, sizeof(dir), "%s/musica", plat_data_dir());
    char **names = NULL;
    int n = 0, cap = 0;
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            size_t len = strlen(e->d_name);
            if (len < 5 || e->d_name[0] == '.' || strcasecmp(e->d_name + len - 4, ".wav") != 0)
                continue;
            if (n == cap) {
                cap = cap ? cap * 2 : 16;
                char **nn = realloc(names, sizeof(char *) * (size_t)cap);
                if (!nn)
                    break;
                names = nn;
            }
            names[n++] = strdup(e->d_name);
        }
        closedir(d);
    }
    log_msg("musica: %d canciones en %s", n, dir);
    if (n == 0) {
        free(names);
        return 0;
    }
    qsort(names, (size_t)n, sizeof(char *), song_cmp);
    uint32_t seed = (uint32_t)time_now() * 2654435761u ^ SDL_GetTicks();
    int next = n, fails = 0;
    const char *last = NULL;
    SDL_AtomicSet(&s_songs, 1);
    while (!SDL_AtomicGet(&s_song_quit)) {
        track_free(SDL_AtomicSetPtr(&s_dead_track, NULL));
        if (!SDL_AtomicGetPtr(&s_next_track)) {
            if (next >= n) {
                // a new shuffled round; the song that just played does not open it
                for (int i = n - 1; i > 0; i--) {
                    seed = seed * 1664525u + 1013904223u;
                    int j = (int)((seed >> 8) % (uint32_t)(i + 1));
                    char *tmp = names[i];
                    names[i] = names[j];
                    names[j] = tmp;
                }
                if (n > 1 && last && names[0] == last) {
                    names[0] = names[1];
                    names[1] = (char *)last;
                }
                next = 0;
            }
            char path[600];
            snprintf(path, sizeof(path), "%s/%s", dir, names[next]);
            last = names[next++];
            uint32_t t0 = SDL_GetTicks();
            Track *t = track_load(path);
            if (t) {
                log_msg("musica: %s (%u ms)", t->w.title[0] ? t->w.title : last, SDL_GetTicks() - t0);
                SDL_AtomicSetPtr(&s_next_track, t);
                fails = 0;
            } else if (++fails >= n) {
                log_msg("musica: ninguna cancion se pudo leer");
                SDL_AtomicSet(&s_songs, 0);
                break;
            }
        }
        SDL_SemWaitTimeout(s_song_sem, 500);
    }
    track_free(SDL_AtomicSetPtr(&s_next_track, NULL));
    track_free(SDL_AtomicSetPtr(&s_dead_track, NULL));
    for (int i = 0; i < n; i++)
        free(names[i]);
    free(names);
    return 0;
}

int audio_song(char *title, size_t n)
{
    int seq = SDL_AtomicGet(&s_song_seq);
    if (seq > 0)
        str_copy(title, n, s_song_title);
    return seq;
}

// ---------------------------------------------------------------- mixer

static void callback(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    int n = len / 4;
    static float l[4096], r[4096];
    int16_t *out = (int16_t *)stream;
    while (n > 0) {
        int chunk = n > 4096 ? 4096 : n;
        memset(l, 0, sizeof(float) * chunk);
        memset(r, 0, sizeof(float) * chunk);
        float target = (s_music_on ? s_music_target * s_music_user : 0.0f);
        float step = s_music_fade_rate / RATE * chunk;
        if (s_music_level < target)
            s_music_level = fminf(target, s_music_level + step);
        else
            s_music_level = fmaxf(target, s_music_level - step);
        if (s_music_level > 0.001f && SDL_AtomicGet(&s_songs)) {
            songs_render(l, r, chunk, s_music_level);
        } else if (s_music_level > 0.001f) {
            static float ml[4096], mr[4096];
            memset(ml, 0, sizeof(float) * chunk);
            memset(mr, 0, sizeof(float) * chunk);
            music_render(ml, mr, chunk);
            for (int i = 0; i < chunk; i++) {
                l[i] += ml[i] * s_music_level;
                r[i] += mr[i] * s_music_level;
            }
        }
        for (int v = 0; v < 12; v++) {
            SfxVoice *sv = &s_voices[v];
            if (sv->id < 0)
                continue;
            const float *buf = s_sfx[sv->id];
            int blen = s_sfx_len[sv->id];
            for (int i = 0; i < chunk; i++) {
                int p = (int)sv->pos;
                if (p >= blen - 1) {
                    sv->id = -1;
                    break;
                }
                float f = sv->pos - p;
                float s = (buf[p] + (buf[p + 1] - buf[p]) * f) * sv->gain;
                l[i] += s;
                r[i] += s;
                sv->pos += sv->rate;
            }
        }
        for (int i = 0; i < chunk; i++) {
            float a = l[i], b = r[i];
            // soft clip
            a = a > 1 ? 1 : a < -1 ? -1 : a * (1.5f - 0.5f * a * a);
            b = b > 1 ? 1 : b < -1 ? -1 : b * (1.5f - 0.5f * b * b);
            out[i * 2] = (int16_t)(a * 30000);
            out[i * 2 + 1] = (int16_t)(b * 30000);
        }
        out += chunk * 2;
        n -= chunk;
    }
}

static int synth_thread(void *arg)
{
    (void)arg;
    uint32_t t0 = SDL_GetTicks();
    synth_effects();
    SDL_AtomicSet(&s_sfx_ready, 1);
    log_msg("sonidos listos en %u ms", SDL_GetTicks() - t0);
    return 0;
}

bool audio_init(void)
{
    for (int i = 0; i < SINE_N; i++)
        s_sine[i] = sinf(2.0f * (float)M_PI * i / SINE_N);
    for (int v = 0; v < 12; v++)
        s_voices[v].id = -1;
    // the effects take a moment to compute: do it without delaying startup
    s_synth = plat_thread_start(synth_thread, NULL, "sonidos", 64 * 1024, PLAT_CORE_WORK);
    if (!s_synth)
        synth_thread(NULL);
    for (int k = 0; k < 8; k++)
        s_pad_amp[k] = 0;

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
        return false;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = callback;
    s_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!s_dev) {
        log_msg("audio: %s", SDL_GetError());
        return false;
    }
    SDL_PauseAudioDevice(s_dev, 0);
    // songs from the SD card, read on the work core
    s_song_sem = SDL_CreateSemaphore(0);
    if (s_song_sem)
        s_song_thread = plat_thread_start(song_thread, NULL, "musica", 64 * 1024, PLAT_CORE_WORK);
    return true;
}

void audio_shutdown(void)
{
    if (s_synth) {
        plat_thread_join(s_synth);
        s_synth = NULL;
    }
    if (s_dev)
        SDL_CloseAudioDevice(s_dev);
    s_dev = 0;
    if (s_song_thread) {
        SDL_AtomicSet(&s_song_quit, 1);
        SDL_SemPost(s_song_sem);
        plat_thread_join(s_song_thread);
        s_song_thread = NULL;
    }
    track_free(s_cur);
    s_cur = NULL;
    if (s_song_sem)
        SDL_DestroySemaphore(s_song_sem);
    s_song_sem = NULL;
    for (int i = 0; i < SFX_COUNT; i++) {
        free(s_sfx[i]);
        s_sfx[i] = NULL;
    }
}

void audio_play_pitch(int sfx, float pitch)
{
    if (!s_dev || !s_sfx_on || sfx < 0 || sfx >= SFX_COUNT || !SDL_AtomicGet(&s_sfx_ready) || !s_sfx[sfx])
        return;
    SDL_LockAudioDevice(s_dev);
    int best = 0;
    float best_pos = -1;
    for (int v = 0; v < 12; v++) {
        if (s_voices[v].id < 0) {
            best = v;
            break;
        }
        if (s_voices[v].pos > best_pos) {
            best_pos = s_voices[v].pos;
            best = v;
        }
    }
    s_voices[best].id = sfx;
    s_voices[best].pos = 0;
    s_voices[best].rate = pitch;
    s_voices[best].gain = 1.0f;
    SDL_UnlockAudioDevice(s_dev);
}

void audio_play(int sfx)
{
    audio_play_pitch(sfx, 1.0f);
}

void audio_set(bool sfx_on, bool music_on)
{
    s_sfx_on = sfx_on;
    s_music_on = music_on;
}

void audio_fade_music(float target, float seconds)
{
    s_music_target = clampf(target, 0, 1);
    s_music_fade_rate = seconds > 0.01f ? 1.0f / seconds : 100.0f;
}

// Utilidades: cadenas UTF-8, archivos, tiempo y registro.
#include "common.h"
#include "i18n.h"
#include "platform.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef __WIIU__
#include <coreinit/debug.h>
#endif

void str_copy(char *dst, size_t n, const char *src)
{
    if (!n)
        return;
    if (!src)
        src = "";
    size_t len = strlen(src);
    if (len >= n) {
        len = n - 1;
        // never cut a UTF-8 sequence in half
        while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80)
            len--;
    }
    memcpy(dst, src, len);
    dst[len] = 0;
}

char *str_trim(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    size_t len = strlen(s);
    while (len && isspace((unsigned char)s[len - 1]))
        s[--len] = 0;
    return s;
}

int utf8_decode(const char **ps)
{
    const unsigned char *s = (const unsigned char *)*ps;
    int cp;
    if (!s[0])
        return 0;
    if (s[0] < 0x80) {
        cp = s[0];
        *ps += 1;
    } else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        *ps += 2;
    } else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        *ps += 3;
    } else if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 &&
               (s[3] & 0xC0) == 0x80) {
        cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        *ps += 4;
    } else {
        cp = 0xFFFD;
        *ps += 1;
    }
    return cp;
}

int utf8_encode(int cp, char *out)
{
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static int fold_cp(int cp)
{
    if (cp < 0x80)
        return tolower(cp);
    static const struct { int lo, hi; char base; } ranges[] = {
        { 0xC0, 0xC5, 'a' }, { 0xE0, 0xE5, 'a' }, { 0xC7, 0xC7, 'c' }, { 0xE7, 0xE7, 'c' },
        { 0xC8, 0xCB, 'e' }, { 0xE8, 0xEB, 'e' }, { 0xCC, 0xCF, 'i' }, { 0xEC, 0xEF, 'i' },
        { 0xD1, 0xD1, 'n' }, { 0xF1, 0xF1, 'n' }, { 0xD2, 0xD6, 'o' }, { 0xF2, 0xF6, 'o' },
        { 0xD8, 0xD8, 'o' }, { 0xF8, 0xF8, 'o' }, { 0xD9, 0xDC, 'u' }, { 0xF9, 0xFC, 'u' },
        { 0xDD, 0xDD, 'y' }, { 0xFD, 0xFD, 'y' }, { 0xFF, 0xFF, 'y' },
    };
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++)
        if (cp >= ranges[i].lo && cp <= ranges[i].hi)
            return ranges[i].base;
    return cp;
}

void str_fold(const char *src, char *dst, size_t n)
{
    size_t o = 0;
    int cp;
    while ((cp = utf8_decode(&src)) != 0) {
        char buf[4];
        int len;
        cp = fold_cp(cp);
        if (cp == '\n' || cp == '\r' || cp == '\t')
            cp = ' ';
        // skip trademark/registered signs so they don't affect sorting
        if (cp == 0xAE || cp == 0x2122 || cp == 0xA9)
            continue;
        len = utf8_encode(cp, buf);
        if (o + len >= n)
            break;
        memcpy(dst + o, buf, len);
        o += len;
    }
    dst[o] = 0;
}

bool str_contains_folded(const char *hay, const char *needle)
{
    if (!needle[0])
        return true;
    return strstr(hay, needle) != NULL;
}

uint8_t *file_read(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long len = ftell(f);
    if (len < 0 || len > 64 * 1024 * 1024) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = len ? fread(buf, 1, (size_t)len, f) : 0;
    fclose(f);
    if (got != (size_t)len) {
        free(buf);
        return NULL;
    }
    buf[len] = 0;
    if (size)
        *size = (size_t)len;
    return buf;
}

bool file_write_atomic(const char *path, const void *data, size_t size)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f)
        return false;
    bool ok = fwrite(data, 1, size, f) == size;
    ok = (fclose(f) == 0) && ok;
    if (!ok) {
        remove(tmp);
        return false;
    }
    remove(path);
    if (rename(tmp, path) != 0) {
        // fall back to a direct write
        remove(tmp);
        f = fopen(path, "wb");
        if (!f)
            return false;
        ok = fwrite(data, 1, size, f) == size;
        ok = (fclose(f) == 0) && ok;
    }
    return ok;
}

bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

void dir_make(const char *path)
{
    char buf[512];
    str_copy(buf, sizeof(buf), path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/' && p[-1] != ':') {
            *p = 0;
            mkdir(buf, 0777);
            *p = '/';
        }
    }
    mkdir(buf, 0777);
}

int64_t time_now(void)
{
    return (int64_t)time(NULL);
}

static void split_time(int64_t t, struct tm *out)
{
    time_t tt = (time_t)t;
#ifdef __WIIU__
    // the console clock has no time zone: its value already is local time
    gmtime_r(&tt, out);
#else
    localtime_r(&tt, out);
#endif
}

static const char *k_months[12] = { "ene", "feb", "mar", "abr", "may", "jun",
                                    "jul", "ago", "sep", "oct", "nov", "dic" };
static const char *k_months_en[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
static const char *k_days[7] = { "dom", "lun", "mar", "mié", "jue", "vie", "sáb" };
static const char *k_days_en[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };

void format_date(int64_t t, char *out, size_t n)
{
    struct tm tm;
    split_time(t, &tm);
    if (i18n_english())
        snprintf(out, n, "%s %d, %d", k_months_en[tm.tm_mon], tm.tm_mday, tm.tm_year + 1900);
    else
        snprintf(out, n, "%d %s %d", tm.tm_mday, k_months[tm.tm_mon], tm.tm_year + 1900);
}

void format_clock_date(const struct tm *tm, char *out, size_t n)
{
    int d = tm->tm_wday % 7, m = tm->tm_mon % 12;
    if (i18n_english())
        snprintf(out, n, "%s %s %d", k_days_en[d], k_months_en[m], tm->tm_mday);
    else
        snprintf(out, n, "%s %d %s", k_days[d], tm->tm_mday, k_months[m]);
}

void format_relative_time(int64_t t, char *out, size_t n)
{
    if (t <= 0) {
        str_copy(out, n, tr("Nunca"));
        return;
    }
    int64_t now = time_now();
    int64_t diff = now - t;
    struct tm tn, tt;
    split_time(now, &tn);
    split_time(t, &tt);
    int day_now = tn.tm_year * 400 + tn.tm_yday, day_t = tt.tm_year * 400 + tt.tm_yday;
    if (diff < 0)
        diff = 0;
    if (diff < 5 * 60) {
        str_copy(out, n, tr("Hace un momento"));
    } else if (diff < 3600) {
        snprintf(out, n, tr("Hace %d minutos"), (int)(diff / 60));
    } else if (day_now == day_t) {
        snprintf(out, n, tr("Hoy, %02d:%02d"), tt.tm_hour, tt.tm_min);
    } else if (diff < 2 * 86400 && (day_now - day_t == 1 || (tn.tm_yday == 0 && diff < 86400 * 2))) {
        snprintf(out, n, tr("Ayer, %02d:%02d"), tt.tm_hour, tt.tm_min);
    } else if (diff < 7 * 86400) {
        snprintf(out, n, tr("Hace %d días"), (int)(diff / 86400) < 2 ? 2 : (int)(diff / 86400));
    } else if (diff < 30 * 86400) {
        int w = (int)(diff / (7 * 86400));
        snprintf(out, n, tr(w == 1 ? "Hace 1 semana" : "Hace %d semanas"), w);
    } else if (diff < 365 * 86400) {
        int m = (int)(diff / (30 * 86400));
        snprintf(out, n, tr(m <= 1 ? "Hace 1 mes" : "Hace %d meses"), m);
    } else {
        format_date(t, out, n);
    }
}

uint32_t hash_str(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 16777619u;
    }
    return h;
}

// Log lines also go to SD:/wiiu/apps/pixmogames/log.txt (the previous session is
// kept as log_anterior.txt). A low-priority thread writes them every 0.4 s, so
// the file keeps growing even if the interface thread gets stuck.
#define LOG_CAP 32768
static char s_log[LOG_CAP];
static size_t s_log_len;
static unsigned s_log_dropped;
static SDL_mutex *s_log_lock;
static char s_log_path[256];
static PlatThread *s_log_thread;
static volatile bool s_log_quit;

// Writes the pending lines; the whole write happens under the lock so the
// order is kept even when several threads flush.
static void log_flush(void)
{
    SDL_LockMutex(s_log_lock);
    if (s_log_dropped && s_log_len + 64 < LOG_CAP) {
        s_log_len += (size_t)snprintf(s_log + s_log_len, 64, "(%u lineas descartadas)\n", s_log_dropped);
        s_log_dropped = 0;
    }
    if (s_log_len) {
        FILE *f = fopen(s_log_path, "ab");
        if (f) {
            fwrite(s_log, 1, s_log_len, f);
            fclose(f);
        }
        s_log_len = 0;
    }
    SDL_UnlockMutex(s_log_lock);
}

static int log_thread(void *arg)
{
    (void)arg;
    while (!s_log_quit) {
        plat_sleep_ms(400);
        log_flush();
    }
    log_flush();
    return 0;
}

void log_file_start(const char *dir)
{
    char prev[256];
    dir_make(dir);
    snprintf(s_log_path, sizeof(s_log_path), "%s/log.txt", dir);
    snprintf(prev, sizeof(prev), "%s/log_anterior.txt", dir);
    remove(prev);
    rename(s_log_path, prev);
    remove(s_log_path);
    s_log_lock = SDL_CreateMutex();
    if (!s_log_lock)
        return;
    s_log_thread = plat_thread_start(log_thread, NULL, "log", 32 * 1024, PLAT_CORE_LIGHT);
}

void log_file_stop(void)
{
    if (!s_log_thread)
        return;
    s_log_quit = true;
    plat_thread_join(s_log_thread);
    s_log_thread = NULL;
}

void log_msg(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
#ifdef __WIIU__
    OSReport("LUDO: %s\n", buf);
#else
    fprintf(stderr, "LUDO: %s\n", buf);
#endif
    if (!s_log_lock)
        return;
    char line[600];
    uint32_t ms = SDL_GetTicks();
    int n = snprintf(line, sizeof(line), "[%5u.%03u %-8s] %s\n", ms / 1000, ms % 1000, plat_thread_name(), buf);
    if (n <= 0)
        return;
    if (n >= (int)sizeof(line))
        n = (int)sizeof(line) - 1;
    SDL_LockMutex(s_log_lock);
    if (s_log_len + (size_t)n < LOG_CAP) {
        memcpy(s_log + s_log_len, line, (size_t)n);
        s_log_len += (size_t)n;
    } else {
        s_log_dropped++;
    }
    SDL_UnlockMutex(s_log_lock);
    // during the first 20 s every line reaches the SD card at once, so the
    // last step before a freeze is always on the card
    if (ms < 20000)
        log_flush();
}

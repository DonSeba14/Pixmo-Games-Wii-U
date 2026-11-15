// Lectura de meta.xml (nombre, editor, código de producto...).
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *k_lang_suffix[12] = { "ja", "en", "fr", "de", "it", "es",
                                         "zhs", "ko", "nl", "pt", "ru", "zht" };

static void decode_entities(const char *src, size_t len, char *out, size_t n)
{
    size_t o = 0;
    for (size_t i = 0; i < len && o + 4 < n;) {
        if (src[i] == '&') {
            const char *semi = memchr(src + i, ';', len - i);
            if (semi && semi - (src + i) < 10) {
                size_t elen = (size_t)(semi - (src + i)) + 1;
                int cp = -1;
                if (!strncmp(src + i, "&amp;", elen)) cp = '&';
                else if (!strncmp(src + i, "&lt;", elen)) cp = '<';
                else if (!strncmp(src + i, "&gt;", elen)) cp = '>';
                else if (!strncmp(src + i, "&quot;", elen)) cp = '"';
                else if (!strncmp(src + i, "&apos;", elen)) cp = '\'';
                else if (src[i + 1] == '#')
                    cp = (src[i + 2] == 'x' || src[i + 2] == 'X') ? (int)strtol(src + i + 3, NULL, 16)
                                                                  : (int)strtol(src + i + 2, NULL, 10);
                if (cp > 0) {
                    o += utf8_encode(cp, out + o);
                    i += elen;
                    continue;
                }
            }
        }
        if (src[i] != '\r')
            out[o++] = src[i];
        i++;
    }
    out[o] = 0;
}

static bool xml_tag(const char *xml, const char *tag, char *out, size_t n)
{
    char open[64], close[64];
    snprintf(open, sizeof(open), "<%s", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    size_t olen = strlen(open);
    const char *p = xml;
    while ((p = strstr(p, open)) != NULL) {
        char c = p[olen];
        if (c == ' ' || c == '>' || c == '\t' || c == '/') {
            const char *gt = strchr(p, '>');
            if (!gt)
                return false;
            if (gt[-1] == '/') { // <tag/>
                out[0] = 0;
                return true;
            }
            const char *end = strstr(gt + 1, close);
            if (!end)
                return false;
            decode_entities(gt + 1, (size_t)(end - (gt + 1)), out, n);
            return true;
        }
        p += olen;
    }
    return false;
}

static uint32_t xml_u32(const char *xml, const char *tag, int base)
{
    char buf[32];
    if (!xml_tag(xml, tag, buf, sizeof(buf)))
        return 0;
    return (uint32_t)strtoul(str_trim(buf), NULL, base);
}

static void localized(const char *xml, const char *field, int lang, char *out, size_t n)
{
    char tag[32];
    out[0] = 0;
    if (lang >= 0 && lang < 12) {
        snprintf(tag, sizeof(tag), "%s_%s", field, k_lang_suffix[lang]);
        xml_tag(xml, tag, out, n);
    }
    // fall back to English, then to any language with text
    if (!str_trim(out)[0]) {
        snprintf(tag, sizeof(tag), "%s_en", field);
        xml_tag(xml, tag, out, n);
    }
    for (int i = 0; i < 12 && !str_trim(out)[0]; i++) {
        snprintf(tag, sizeof(tag), "%s_%s", field, k_lang_suffix[i]);
        xml_tag(xml, tag, out, n);
    }
    char *t = str_trim(out);
    if (t != out)
        memmove(out, t, strlen(t) + 1);
}

bool meta_parse_xml(const char *xml, int language, PlatMeta *m)
{
    memset(m, 0, sizeof(*m));
    localized(xml, "longname", language, m->name, sizeof(m->name));
    localized(xml, "shortname", language, m->short_name, sizeof(m->short_name));
    localized(xml, "publisher", language, m->publisher, sizeof(m->publisher));
    xml_tag(xml, "product_code", m->product_code, sizeof(m->product_code));
    xml_tag(xml, "company_code", m->company_code, sizeof(m->company_code));
    m->region = xml_u32(xml, "region", 16);
    m->drc_use = xml_u32(xml, "drc_use", 10);
    m->title_version = xml_u32(xml, "title_version", 10);
    if (xml_u32(xml, "ext_dev_nunchaku", 10)) m->ext_flags |= EXT_NUNCHUK;
    if (xml_u32(xml, "ext_dev_classic", 10)) m->ext_flags |= EXT_CLASSIC;
    if (xml_u32(xml, "ext_dev_urcc", 10)) m->ext_flags |= EXT_PRO;
    if (xml_u32(xml, "ext_dev_board", 10)) m->ext_flags |= EXT_BOARD;
    if (xml_u32(xml, "ext_dev_usb_keyboard", 10)) m->ext_flags |= EXT_KEYBOARD;
    return m->name[0] || m->short_name[0];
}

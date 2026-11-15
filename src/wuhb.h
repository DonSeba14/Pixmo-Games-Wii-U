// Paquetes .wuhb de Aroma (homebrew en la SD):
// búsqueda en SD:/wiiu/apps, lectura de archivos internos y de meta.ini.
#pragma once

#include "platform.h"

// Pseudo title id of a homebrew bundle: 0005000F + hash of its SD-relative path.
uint64_t wuhb_title_id(const char *rel_path);
// Appends the .wuhb bundles found in <sd_root>/wiiu/apps (and one folder deeper)
// to list (capacity *cap, count *n, realloc'd). Skips our own bundle (APP_DIR).
void wuhb_scan(const char *sd_root, PlatTitle **list, int *n, int *cap);
// Reads one file of a bundle ("/meta/meta.ini"); malloc'd, zero-terminated.
bool wuhb_read(const char *bundle_path, const char *inner, uint8_t **out, size_t *size);
// Same, gunzipping it ("/meta/iconTex.tga.gz" -> TGA bytes).
bool wuhb_read_gz(const char *bundle_path, const char *inner, uint8_t **out, size_t *size);
// Name and author from /meta/meta.ini ([menu] longname / shortname / author).
bool wuhb_meta(const char *bundle_path, PlatMeta *m);

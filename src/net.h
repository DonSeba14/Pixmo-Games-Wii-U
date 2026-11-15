// Cliente HTTP/1.0 mínimo (solo http://, sin TLS) para bajar carátulas.
#pragma once

#include "common.h"

// Returns true on HTTP 200 with a body. *out is malloc'd. *status gets the HTTP code (or -1).
bool http_get(const char *host, const char *path, uint8_t **out, size_t *len, int *status);
// Aborts a transfer in progress (called from another thread when exiting).
void net_abort(void);
void net_resume(void); // undoes net_abort (a launch that failed: the app goes on)

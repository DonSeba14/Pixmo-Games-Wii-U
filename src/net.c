// HTTP GET por sockets BSD (nsysnet en Wii U, POSIX en PC).
#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define MAX_BODY (4 * 1024 * 1024)

static volatile int s_sock = -1;
static volatile bool s_abort;

void net_resume(void)
{
    s_abort = false;
}

void net_abort(void)
{
    s_abort = true;
    int s = s_sock;
    if (s >= 0)
        shutdown(s, SHUT_RDWR);
}

static bool request(const char *host, const char *path, uint8_t **out, size_t *len, int *status,
                    char *location, size_t locn)
{
    *out = NULL;
    *len = 0;
    *status = -1;
    location[0] = 0;

    if (s_abort)
        return false;
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, "80", &hints, &res) != 0 || !res)
        return false;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    bool found = false;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        if (ai->ai_family == AF_INET && ai->ai_addr && ai->ai_addrlen >= sizeof(struct sockaddr_in)) {
            memcpy(&addr, ai->ai_addr, sizeof(addr));
            found = true;
            break;
        }
    }
    freeaddrinfo(res);
    if (!found)
        return false;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return false;
    s_sock = s;
#if defined(SO_RCVTIMEO) && defined(SO_SNDTIMEO)
    struct timeval tv = { 6, 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
    if (s_abort || connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        s_sock = -1;
        close(s);
        return false;
    }
    char req[640];
    int rl = snprintf(req, sizeof(req),
                      "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: PixmoGames/" APP_VERSION
                      " (Nintendo Wii U)\r\nAccept: image/*\r\nConnection: close\r\n\r\n",
                      path, host);
    for (int sent = 0; sent < rl;) {
        int n = (int)send(s, req + sent, rl - sent, 0);
        if (n <= 0) {
            s_sock = -1;
            close(s);
            return false;
        }
        sent += n;
    }
    size_t cap = 64 * 1024, used = 0;
    uint8_t *buf = malloc(cap);
    if (!buf) {
        s_sock = -1;
        close(s);
        return false;
    }
    for (;;) {
        if (used + 16384 > cap) {
            if (cap >= MAX_BODY + 65536)
                break;
            cap *= 2;
            uint8_t *nb = realloc(buf, cap);
            if (!nb)
                break;
            buf = nb;
        }
        // wait at most 8 s for data (the console's sockets have no SO_RCVTIMEO)
        struct pollfd pfd = { s, POLLIN, 0 };
        if (poll(&pfd, 1, 8000) <= 0)
            break;
        int n = (int)recv(s, buf + used, cap - used - 1, 0);
        if (n <= 0 || s_abort)
            break;
        used += (size_t)n;
    }
    s_sock = -1;
    close(s);
    buf[used] = 0;

    // status line and headers
    char *hdr_end = strstr((char *)buf, "\r\n\r\n");
    if (!hdr_end || strncmp((char *)buf, "HTTP/", 5) != 0) {
        free(buf);
        return false;
    }
    const char *sp = strchr((char *)buf, ' ');
    *status = sp ? atoi(sp + 1) : -1;
    *hdr_end = 0;
    const char *loc = strstr((char *)buf, "\nLocation:");
    if (!loc)
        loc = strstr((char *)buf, "\nlocation:");
    if (loc) {
        loc += 10;
        while (*loc == ' ')
            loc++;
        size_t l = strcspn(loc, "\r\n");
        if (l >= locn)
            l = locn - 1;
        memcpy(location, loc, l);
        location[l] = 0;
    }
    size_t body_off = (size_t)(hdr_end + 4 - (char *)buf);
    size_t body_len = used - body_off;
    if (*status != 200 || body_len == 0) {
        free(buf);
        return false;
    }
    memmove(buf, buf + body_off, body_len);
    *out = buf;
    *len = body_len;
    return true;
}

bool http_get(const char *host, const char *path, uint8_t **out, size_t *len, int *status)
{
    char location[300];
    char h[128], p[300];
    str_copy(h, sizeof(h), host);
    str_copy(p, sizeof(p), path);
    for (int hop = 0; hop < 3; hop++) {
        if (request(h, p, out, len, status, location, sizeof(location)))
            return true;
        if ((*status == 301 || *status == 302 || *status == 307 || *status == 308) &&
            !strncmp(location, "http://", 7)) {
            const char *rest = location + 7;
            const char *slash = strchr(rest, '/');
            size_t hl = slash ? (size_t)(slash - rest) : strlen(rest);
            if (hl >= sizeof(h))
                return false;
            memcpy(h, rest, hl);
            h[hl] = 0;
            str_copy(p, sizeof(p), slash ? slash : "/");
            continue;
        }
        return false;
    }
    return false;
}

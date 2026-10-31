/* A small read-only web page on the local network while the manager is loaded on a track (MPC OS 3.10.1 test):
 * http://<mpc address>:WEB_PORT/ lists what is installed, /api/state gives the same as JSON. MPC creates the plugin
 * more than once, so only one instance holds the port: the others retry the bind every couple of seconds and take
 * over when it is closed. Included by manager.c (it reads mgr_t under mu). */
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <arpa/inet.h>

#ifndef WEB_PORT
#define WEB_PORT 8790
#endif

static void web_addr(char *b, int n) {   /* the first non-loopback IPv4 address, "" none */
    struct ifaddrs *ia, *i;
    b[0] = 0;
    if (getifaddrs(&ia)) return;
    for (i = ia; i; i = i->ifa_next)
        if (i->ifa_addr && i->ifa_addr->sa_family == AF_INET && strcmp(i->ifa_name, "lo")) {
            inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, b, n);
            break;
        }
    freeifaddrs(ia);
}

typedef struct { char *s; size_t n, cap; } wbuf_t;

static void wput(wbuf_t *w, const char *fmt, ...) {
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int k = vsnprintf(w->s ? w->s + w->n : NULL, w->s ? w->cap - w->n : 0, fmt, ap);
        va_end(ap);
        if (k < 0) return;
        if (w->s && w->n + k < w->cap) { w->n += k; return; }
        size_t c = (w->cap + k + 1) * 2;
        char *s = realloc(w->s, c);
        if (!s) return;
        w->s = s, w->cap = c;
    }
}

static void wesc(wbuf_t *w, const char *s, int html) {   /* JSON string body, or HTML text */
    for (; *s; s++) {
        unsigned char c = *s;
        if (html) wput(w, c == '<' ? "&lt;" : c == '>' ? "&gt;" : c == '&' ? "&amp;" : c == '"' ? "&quot;" : "%c", c);
        else if (c == '"' || c == '\\') wput(w, "\\%c", c);
        else if (c < 0x20) wput(w, "\\u%04x", c);
        else wput(w, "%c", c);
    }
}

static void web_state(mgr_t *m, wbuf_t *w) {
    pthread_mutex_lock(&m->mu);
    wput(w, "{\"status\":\"");
    wesc(w, m->status, 0);
    wput(w, "\",\"online\":%d,\"loaded\":%d,\"busy\":%d,\"plugins\":[", m->online, m->loaded, m->busy);
    for (int i = 0, first = 1; i < m->npkg; i++) {
        const pkg_t *p = &m->pkg[i];
        if (!p->installed[0]) continue;
        wput(w, "%s{\"id\":\"", first ? "" : ",");
        wesc(w, p->id, 0);
        wput(w, "\",\"name\":\"");
        wesc(w, p->name, 0);
        wput(w, "\",\"installed\":\"");
        wesc(w, p->installed, 0);
        wput(w, "\",\"latest\":\"");
        wesc(w, p->latest, 0);
        wput(w, "\"}");
        first = 0;
    }
    wput(w, "]}");
    pthread_mutex_unlock(&m->mu);
}

static void web_page(mgr_t *m, wbuf_t *w) {
    wput(w, "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
            "<title>Plugin Manager</title><style>body{font:15px system-ui;margin:0 16px 40px;background:#e9e7e2;color:#1b1c1f}"
            "h1{font:700 28px system-ui;text-transform:uppercase}li{padding:6px 0;border-bottom:1px solid #cdcac2;list-style:none}"
            "ul{padding:0}.v{color:#64666d}.up{color:#d63a30}</style><h1>Plugin Manager</h1>");
    pthread_mutex_lock(&m->mu);
    wput(w, "<p class=v>");
    wesc(w, m->status, 1);
    wput(w, "</p><ul>");
    int k = 0;
    for (int i = 0; i < m->npkg; i++) {
        const pkg_t *p = &m->pkg[i];
        if (!p->installed[0]) continue;
        k++;
        wput(w, "<li><b>");
        wesc(w, p->name, 1);
        wput(w, "</b> <span class=v>");
        wesc(w, p->installed, 1);
        wput(w, "</span>");
        if (p->latest[0] && strcmp(p->installed, p->latest) && strcmp(p->installed, "?")) {
            wput(w, " <span class=up>update: ");
            wesc(w, p->latest, 1);
            wput(w, "</span>");
        }
        wput(w, "</li>");
    }
    pthread_mutex_unlock(&m->mu);
    wput(w, "</ul><p class=v>%d installed. This page is read-only (test build).</p>", k);
}

static void web_reply(int c, mgr_t *m) {
    char req[2048];
    int n = 0, k;
    struct pollfd pf = {c, POLLIN, 0};
    while (n < (int)sizeof req - 1 && poll(&pf, 1, 2000) > 0 && (k = read(c, req + n, sizeof req - 1 - n)) > 0) {
        n += k;
        req[n] = 0;
        if (strstr(req, "\r\n\r\n")) break;
    }
    req[n] = 0;
    wbuf_t w = {0};
    const char *type = "text/html; charset=utf-8", *code = "200 OK";
    if (!strncmp(req, "GET /api/state ", 15)) web_state(m, &w), type = "application/json";
    else if (!strncmp(req, "GET / ", 6)) web_page(m, &w);
    else code = "404 Not Found", wput(&w, "not found\n"), type = "text/plain";
    char h[200];
    int hn = snprintf(h, sizeof h, "HTTP/1.0 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                      code, type, w.n);
    if (write(c, h, hn) == hn && w.s) { ssize_t r = write(c, w.s, w.n); (void)r; }
    free(w.s);
}

static int web_quit(mgr_t *m) {
    pthread_mutex_lock(&m->mu);
    int q = m->quit;
    pthread_mutex_unlock(&m->mu);
    return q;
}

static void *web_thread(void *ud) {
    mgr_t *m = ud;
    int s = -1;
    while (!web_quit(m)) {
        if (s < 0) {
            s = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
            int one = 1;
            struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(WEB_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
            if (s >= 0) setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) || listen(s, 4)) {   /* another instance has it */
                if (s >= 0) close(s);
                s = -1;
                for (int i = 0; i < 4 && !web_quit(m); i++) usleep(500000);
                continue;
            }
        }
        struct pollfd pf = {s, POLLIN, 0};
        if (poll(&pf, 1, 500) <= 0) continue;
        int c = accept4(s, NULL, NULL, SOCK_CLOEXEC);
        if (c < 0) continue;
        web_reply(c, m);
        close(c);
    }
    if (s >= 0) close(s);
    return NULL;
}

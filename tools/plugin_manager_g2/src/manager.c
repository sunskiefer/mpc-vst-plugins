/* Plugin Manager: browse the mpc-vst-plugins catalog from the MPC screen, queue installs, updates and removals, and
 * apply them with one MPC restart. The screen (vst/layout.conf) is three plugin cards per page with tabs, filters,
 * state-dependent buttons and badges, a status line and a progress bar, all driven from here: every value the skin
 * shows or switches on is a parameter computed in get_param, and "display_rev" tells the wrapper when to re-read them
 * (HAS_DISPLAY_REV: text refreshes and changed values are reported, so when= panels follow; docs/NOTES.md).
 *
 * Everything slow runs on a worker thread: the catalog and zips come over HTTPS through the system libcurl
 * (dlopen, nothing bundled); sha256sum and unzip run as children with a clean environment (MPC's LD_PRELOAD
 * dropped). Applying writes a script that stops MPC, runs each package's own install.sh / uninstall.sh and starts
 * MPC again; it is launched with systemd-run so it lives outside acvs.service's cgroup and survives the stop
 * (docs/NOTES.md, "Restarting MPC from inside a plugin via systemd-run"). The plugin makes no sound.
 *
 * Addins (catalog kind "addin": libraries MPC loads at start through LD_PRELOAD, docs/ADDINS.md) go through the same
 * flow. One installs to its own folder under ADDINS_DIR with the install.sh in its zip; what is installed is read from
 * the addin.manifest in each folder; a removal runs the uninstall.sh the folder carries. MPC.settings is not touched. */
#define _GNU_SOURCE   /* dladdr */
#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <ctype.h>
#include <sys/utsname.h>
#include <gnu/libc-version.h>
#include "plugin_list_awk.h"
#include "vst2_migrate_sh.h"
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "shots.h"

/* mpc-vst-plugins wrapper/engine.h, positional */
typedef struct {
    void *(*create)(const char *data_dir);
    void (*destroy)(void *inst);
    void (*midi)(void *inst, const uint8_t *msg, int len);
    void (*set_param)(void *inst, const char *key, const char *val);
    int (*get_param)(void *inst, const char *key, char *buf, int buf_len);
    void (*render)(void *inst, int16_t *out_lr, int frames);
    void (*process)(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames);
} mpc_engine_t;

/* The catalog lives at the OpenMPC org's Pages site; the old address keeps serving the same file for managers that only know it,
 * and is the fallback here when the new one can't be reached or read. Tests set CATALOG_URL to a file:// fixture, and then get no
 * fallback unless they set CATALOG_URL_FALLBACK too (so an offline test never reaches the network). */
#ifndef CATALOG_URL
#define CATALOG_URL "https://open-mpc.github.io/catalog.json"
#ifndef CATALOG_URL_FALLBACK
#define CATALOG_URL_FALLBACK "https://sd88me.github.io/mpc-vst-plugins/catalog.json"
#endif
#endif
#ifndef CATALOG_URL_FALLBACK
#define CATALOG_URL_FALLBACK ""
#endif
/* the device patches list (catalog/patches.json) is published beside the catalog */
#ifndef PATCHES_URL
#define PATCHES_URL "https://open-mpc.github.io/patches.json"
#ifndef PATCHES_URL_FALLBACK
#define PATCHES_URL_FALLBACK "https://sd88me.github.io/mpc-vst-plugins/patches.json"
#endif
#endif
#ifndef PATCHES_URL_FALLBACK
#define PATCHES_URL_FALLBACK ""
#endif
#define WORK "/tmp/openmpc-pm"
#define MAXPKG 256
#define ROWS 5      /* plugin cards per page */
#ifndef ADDINS_DIR
#define ADDINS_DIR "/data/mpc-addins"   /* one folder per addin id, as mpc-vst-plugins' addin installer lays them out */
#endif
#ifndef FS_ANY
#define FS_ANY 0   /* tests: accept the container's overlay filesystem as internal storage */
#endif

typedef struct {
    char id[48], name[48], author[32], kind[16], dist[16], latest[24], url[512], sha[72];
    char style[32], tags[96], channel[16], cpu[8], tested[48];   /* the card's badges and meta line */
    char summary[720];    /* the detail view's text */
    char devices[96];     /* the catalog's "devices" ("|mpc-sample|"): empty = any device; else only these (hidden elsewhere unless installed) */
    char alltags[200];    /* every tag, as "|a|b|" (tag search, the detail view's tag buttons) */
    char date[12], cpu_pct[12], whatsnew[480];   /* the latest version's release date, CPU p99, What's new */
    char updated[12], first[12];   /* last release (catalog "updated"), first release: the sort orders */
    int downloads, nver, wn_kind;   /* wn_kind: 1 notes, 2 "First release."/"No release notes." */
    char *notes, *about;  /* while parsing only: the chosen version's release notes and manifest "about" */
    char *vnotes, *vabout;   /* while parsing only: the same for the version being read */
    long long size;
    char installed[24];   /* "" not installed, "?" installed (version unknown), else the version */
    char synths[200];     /* the Synths folder it is installed in; an addin: its own folder under ADDINS_DIR */
    int ufiles;           /* its mpc-plugin.json declares user_files (ROMs the user adds): the web page offers them */
    char folder[256];     /* its plugin folder (the .so's), "" not installed: the web page's ROM upload writes in it */
    char uids[4][16];     /* build-yourself components' VST uids (catalog "components") */
    int legacy;           /* registered, but not from a plugin folder (old /sdcard/vst layout); an addin folder
                           * without uninstall.sh (made by hand): either is updated in place, not removed */
    int queued;           /* 0, Q_INSTALL, Q_REMOVE, Q_REGISTER */
    int orphan;           /* 1 + its index in device_t.orphan: a folder MPC does not list (the card offers REGISTER) */
    char dir[256];        /* unpacked package (install.sh / uninstall.sh), set while preparing */
} pkg_t;
enum { Q_INSTALL = 1, Q_REMOVE = 2, Q_REGISTER = 3 };
enum { J_NONE, J_REFRESH, J_PREPARE, J_LAUNCH, J_PATCH, J_PAPPLY };

typedef struct device device_t;
typedef struct {   /* PATCHES: one entry of patches.json (a change to MPC itself, run from a script) and what its `status` said */
    char id[40], title[100], summary[400], author[32], os[200], arch[40], backup[120], modifies[160], url[400], sha[72];
    int restarts, reversible;
    int checked, supported;      /* `status` was run: state/reason below; supported = the script's supported=1 */
    char state[20], reason[100];
} patch_t;
typedef struct { char name[96], info[200], state[200], path[200]; int bar, vis, usable, dflt, pick, cand; } locrow_t;   /* LOCATIONS: one drive */
typedef struct {
    pthread_t th;
    pthread_mutex_t mu;
    pthread_cond_t cv;
    int quit, job, busy, ready;   /* ready: a prepared apply.sh waits for the second APPLY press */
    pkg_t pkg[MAXPKG];
    int npkg, page, sel, menu;    /* sel, menu: pkg indexes (-1 none); menu = the card showing Reinstall/Remove */
    char detail[48];              /* the plugin in the detail view (its id: pkg indexes change on a refresh), "" the list */
    char tag[32];                 /* tag search (a tag tapped in the detail view), "" none */
    unsigned vis_rev;             /* visible()'s cache: the rev it was worked out at (+1, so 0 = none), its list */
    int vis_n, vis_idx[MAXPKG];
    int confirm;                  /* the restart prompt is open (the second APPLY); only shown while ready */
    long long theme_t, tap_t;     /* ms of the last accepted theme press / button press (monotonic) */
    char tap_key[32];             /* the button of that press */
    long long kb_t;               /* ms of the last accepted keyboard key (monotonic) */
    char kb_key[16];              /* that key */
    int theme, theme_applied, theme_can;   /* 0 dark, 1 light: the one wanted, the one in `Plugin Skins` at MPC's start, and whether the folder holds both skins */
    char own[300];                /* this plugin's own folder (its skins live there) */
    int tsel, reprompt;           /* which usable location new installs go to; CHANGE re-prepares and reopens the prompt */
    int sort;                     /* 0 last updated, 1 newest (first release), 2 most downloaded, 3 name */
    int menu_open;                /* the header's dropdown that is open: 0 none, 1 filter, 2 sort */
    int kb;                       /* the search keyboard is open */
    char q[28];                   /* the search (typed on the drawn keyboard), "" none */
    int tab, kindf;               /* 0 discover, 1 installed, 2 updates; 0 all, 1 instruments, 2 effects, 3 addins */
    int online, loaded, failed;   /* catalog reachable; ever loaded; the last APPLY failed (offer RETRY) */
    int sticky, status_kind;      /* status set by the worker or an action (else the queue summary); 0 ok, 1 warn, 2 error */
    int downloading;
    float progress, disk_used;
    char disk_free[32];
    unsigned rev;   /* bumped whenever the worker changes text, polled by the wrapper as "display_rev" */
    char status[160];
    char fetch_err[120];   /* the last failed download's reason (libcurl's words), shown when a download gives up */
    device_t *dev;   /* probed by the worker on each refresh */
    int clearcache;   /* SETTINGS: empty MPC's image cache at the next APPLY (it can only be emptied while MPC is stopped) */
    int migrate, migrate_seen;   /* MPC OS 3.10.1: "move to VST2 Plugins" is queued; it was offered once this session (cleared, it stays off) */
    pthread_t web;   /* the local web page (web.c) */
    int web_on;
    patch_t pt[8]; int npt, ptpage, ptidx, ptact, ptconf; long long ptconf_t;   /* PATCHES; ptact: 1 apply, 2 undo (the job); ptconf: 1 + index*2 + undo of the row waiting for its second tap */
       /* PATCHES: the list (loaded on each refresh), the page shown (3 per page), the one being checked */
    int show_beta, web_pref;        /* SETTINGS (saved in <base>/.openmpcpm-settings): list beta plugins in Discover, serve the web page */
    char regq[4][200]; int nregq;   /* LOCATIONS: drives queued for REGISTER (a Synths folder to add to MPC.settings at APPLY) */
    locrow_t locs[16]; int nlocs, locpage;   /* LOCATIONS rows (built by the worker on each refresh), the page shown (4 per page) */
} mgr_t;

/* ---- small helpers ---- */

enum { OK, WARN, ERR };

static void vsay(mgr_t *m, int kind, const char *fmt, va_list ap) {   /* called with mu held */
    vsnprintf(m->status, sizeof m->status, fmt, ap);
    m->status_kind = kind;
    m->sticky = 1;
    m->rev++;
}

static void say_locked(mgr_t *m, int kind, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsay(m, kind, fmt, ap);
    va_end(ap);
}

static void say(mgr_t *m, int kind, const char *fmt, ...) {
    va_list ap;
    pthread_mutex_lock(&m->mu);
    va_start(ap, fmt);
    vsay(m, kind, fmt, ap);
    va_end(ap);
    pthread_mutex_unlock(&m->mu);
}

static void logf_(const char *fmt, ...) {
    FILE *f = fopen(WORK "/manager.log", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static int is_file(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISREG(st.st_mode); }
static int is_addin(const pkg_t *p) { return !strcmp(p->kind, "addin"); }
static int safe(const char *s) { return !strpbrk(s, "'\\\n`$"); }   /* fits in a single-quoted shell word */

/* A child with MPC's LD_PRELOAD and the rest of its environment dropped; output goes to the log. */
static int run(char *const argv[]) {
    static char *const env[] = {"PATH=/usr/sbin:/usr/bin:/sbin:/bin", "HOME=/root", NULL};
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 1, WORK "/manager.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    pid_t pid;
    int rc = posix_spawnp(&pid, argv[0], &fa, NULL, argv, env);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) { logf_("spawn %s: %s", argv[0], strerror(rc)); return -1; }
    int st;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int sh(const char *cmd) { char *argv[] = {"/bin/sh", "-c", (char *)cmd, NULL}; return run(argv); }

/* ---- HTTPS through the system libcurl ---- */

typedef void CURL;
static struct {
    int tried, ok;
    CURL *(*init)(void);
    int (*setopt)(CURL *, int, ...);
    int (*perform)(CURL *);
    void (*cleanup)(CURL *);
    const char *(*strerror)(int);
} cu;
enum { CU_WRITEDATA = 10001, CU_URL = 10002, CU_USERAGENT = 10018, CU_CAINFO = 10065, CU_WRITEFUNCTION = 20011,
       CU_XFERINFOFUNCTION = 20219, CU_XFERINFODATA = 10057, CU_NOPROGRESS = 43, CU_FAILONERROR = 45,
       CU_FOLLOWLOCATION = 52, CU_CONNECTTIMEOUT = 78, CU_LOW_SPEED_LIMIT = 19, CU_LOW_SPEED_TIME = 20, CU_RESUME_FROM_LARGE = 30116 };

static int curl_load(void) {
    if (cu.tried) return cu.ok;
    cu.tried = 1;
    void *h = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
    if (!h) return 0;
    cu.init = (CURL * (*)(void)) dlsym(h, "curl_easy_init");
    cu.setopt = (int (*)(CURL *, int, ...))dlsym(h, "curl_easy_setopt");
    cu.perform = (int (*)(CURL *))dlsym(h, "curl_easy_perform");
    cu.cleanup = (void (*)(CURL *))dlsym(h, "curl_easy_cleanup");
    cu.strerror = (const char *(*)(int))dlsym(h, "curl_easy_strerror");
    cu.ok = cu.init && cu.setopt && cu.perform && cu.cleanup && cu.strerror;
    return cu.ok;
}

typedef struct { char *buf; size_t len, cap; FILE *f; } sink_t;

static size_t on_data(char *p, size_t sz, size_t n, void *ud) {
    sink_t *s = ud;
    size_t k = sz * n;
    if (s->f) return fwrite(p, 1, k, s->f);
    if (s->len + k + 1 > s->cap) {
        size_t cap = (s->len + k + 1) * 2;
        char *b = realloc(s->buf, cap);
        if (!b) return 0;
        s->buf = b, s->cap = cap;
    }
    memcpy(s->buf + s->len, p, k);
    s->len += k;
    s->buf[s->len] = 0;
    return k;
}

typedef struct { mgr_t *m; const char *label; long long base; int pct; } prog_t;

/* the footer's progress bar and status line, once per percent */
static int on_progress(void *ud, long long total, long long now, long long ut, long long un) {
    prog_t *g = ud;
    (void)ut, (void)un;
    if (g->label && total > 0) {
        int pct = (int)((g->base + now) * 100 / (g->base + total));
        if (pct != g->pct) {
            g->pct = pct;
            pthread_mutex_lock(&g->m->mu);
            g->m->progress = pct / 100.0f;
            say_locked(g->m, OK, "Downloading %s  \xc2\xb7  %d%%  \xc2\xb7  then checking its sha256", g->label, pct);
            pthread_mutex_unlock(&g->m->mu);
        }
    }
    return g->m->quit;   /* non-zero aborts the transfer when the plugin is removed */
}

/* to memory (s->f NULL) or to a file, resuming from byte `from`; 0 on success */
static int fetch_from(mgr_t *m, const char *url, sink_t *s, const char *label, long long from) {
    prog_t g = {m, label, from, -1};
    if (!curl_load()) { logf_("libcurl.so.4 not available"); return -1; }
    CURL *c = cu.init();
    if (!c) return -1;
    cu.setopt(c, CU_URL, url);
    cu.setopt(c, CU_FOLLOWLOCATION, 1L);
    cu.setopt(c, CU_FAILONERROR, 1L);
    cu.setopt(c, CU_USERAGENT, "mpc-plugin-manager/0.1");
    if (is_file("/etc/ssl/certs/ca-certificates.crt")) cu.setopt(c, CU_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
    cu.setopt(c, CU_CONNECTTIMEOUT, 15L);
    cu.setopt(c, CU_LOW_SPEED_LIMIT, 100L);
    cu.setopt(c, CU_LOW_SPEED_TIME, 60L);
    if (from > 0) cu.setopt(c, CU_RESUME_FROM_LARGE, (long long)from);
    cu.setopt(c, CU_WRITEFUNCTION, on_data);
    cu.setopt(c, CU_WRITEDATA, s);
    cu.setopt(c, CU_NOPROGRESS, 0L);
    cu.setopt(c, CU_XFERINFOFUNCTION, on_progress);
    cu.setopt(c, CU_XFERINFODATA, &g);
    int rc = cu.perform(c);
    if (rc) {
        logf_("fetch %s: %s", url, cu.strerror(rc));
        snprintf(m->fetch_err, sizeof m->fetch_err, "%s", cu.strerror(rc));
    }
    cu.cleanup(c);
    return rc ? -1 : 0;
}

static int fetch(mgr_t *m, const char *url, sink_t *s) { return fetch_from(m, url, s, NULL, 0); }

static int sha_ok(const char *sha, const char *path) {
    char cmd[700];
    snprintf(cmd, sizeof cmd, "echo '%s  %s' | sha256sum -c -", sha, path);
    return is_file(path) && sh(cmd) == 0;
}

/* Download to path until its sha256 matches: a kept zip that already matches is reused, a broken transfer resumes
 * where it stopped, a wrong file starts over. 0 on success. */
static int download(mgr_t *m, const pkg_t *p, const char *path) {
    m->fetch_err[0] = 0;
    for (int attempt = 1; attempt <= 4 && !m->quit; attempt++) {
        if (sha_ok(p->sha, path)) return 0;
        struct stat st;
        long long have = attempt > 1 && stat(path, &st) == 0 ? (long long)st.st_size : 0;
        if (attempt > 1) say(m, WARN, "%s: connection stalled  \xc2\xb7  resuming, retry %d/3", p->name, attempt - 1);
        sink_t s = {0};
        s.f = fopen(path, have ? "ab" : "wb");
        if (!s.f) { snprintf(m->fetch_err, sizeof m->fetch_err, "cannot write %s: %s", path, strerror(errno)); return -1; }
        int rc = fetch_from(m, p->url, &s, p->name, have);
        fclose(s.f);
        if (rc == 0 && !sha_ok(p->sha, path)) { snprintf(m->fetch_err, sizeof m->fetch_err, "the file did not match its checksum"); unlink(path); }   /* complete but wrong (a server that ignored the range) */
        else if (rc == 0) return 0;
    }
    return -1;
}

/* ---- just enough JSON for catalog.json ---- */

static const char *ws(const char *p) { while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++; return p; }

/* p at '"'; returns past the closing quote. \uXXXX becomes UTF-8 (surrogate pairs: '?'); \n a space, or kept with nl */
static const char *jstr_nl(const char *p, char *out, size_t n, int nl) {
    size_t k = 0;
    if (*p != '"') return NULL;
    for (p++; *p && *p != '"'; p++) {
        char c = *p, u8[4];
        int len = 1;
        u8[0] = c;
        if (c == '\\') {
            c = *++p;
            if (!c) return NULL;
            u8[0] = c == 'n' ? (nl ? '\n' : ' ') : c == 't' || c == 'r' ? ' ' : c;
            if (c == 'u') {
                unsigned cp = 0;
                for (int i = 0; i < 4; i++) {
                    char h = *++p;
                    if (!h) return NULL;
                    cp = cp * 16 + (h >= 'a' ? h - 'a' + 10 : h >= 'A' ? h - 'A' + 10 : h - '0');
                }
                if (cp < 0x80) u8[0] = (char)cp;
                else if (cp < 0x800) { u8[0] = (char)(0xc0 | cp >> 6); u8[1] = (char)(0x80 | (cp & 63)); len = 2; }
                else if (cp >= 0xd800 && cp < 0xe000) u8[0] = '?';
                else { u8[0] = (char)(0xe0 | cp >> 12); u8[1] = (char)(0x80 | (cp >> 6 & 63)); u8[2] = (char)(0x80 | (cp & 63)); len = 3; }
            }
        }
        if (out && k + len < n) { memcpy(out + k, u8, len); k += len; }
        else if (out) n = k + 1;   /* full: drop the rest, never half a character */
    }
    if (out && n) out[k] = 0;
    return *p == '"' ? p + 1 : NULL;
}
static const char *jstr(const char *p, char *out, size_t n) { return jstr_nl(p, out, n, 0); }

static const char *jskip(const char *p) {
    p = ws(p);
    if (*p == '"') return jstr(p, NULL, 0);
    if (*p == '{' || *p == '[') {
        char close = *p == '{' ? '}' : ']';
        p = ws(p + 1);
        if (*p == close) return p + 1;
        for (;;) {
            if (close == '}') { p = jstr(ws(p), NULL, 0); if (!p) return NULL; p = ws(p); if (*p++ != ':') return NULL; }
            p = jskip(p);
            if (!p) return NULL;
            p = ws(p);
            if (*p == ',') { p++; continue; }
            return *p == close ? p + 1 : NULL;
        }
    }
    while (*p && !strchr(",}] \n\r\t", *p)) p++;
    return p;
}

/* Iterate an object: calls f(key, value_ptr, ud) for each member; f returns past the value (or NULL to skip it). */
typedef const char *(*member_fn)(const char *key, const char *v, void *ud);
static const char *jobject(const char *p, member_fn f, void *ud) {
    char key[64];
    p = ws(p);
    if (*p != '{') return NULL;
    p = ws(p + 1);
    if (*p == '}') return p + 1;
    for (;;) {
        p = jstr(ws(p), key, sizeof key);
        if (!p) return NULL;
        p = ws(p);
        if (*p++ != ':') return NULL;
        p = ws(p);
        const char *q = f(key, p, ud);
        p = q ? q : jskip(p);
        if (!p) return NULL;
        p = ws(p);
        if (*p == ',') { p++; continue; }
        return *p == '}' ? p + 1 : NULL;
    }
}

#define NOTES_MAX 4096   /* release notes kept per version while parsing (What's new is near the top) */
typedef struct { char version[24], url[512], sha[72], channel[16], cpu[8], cpu_pct[12], tested[48], date[12];
                 long long size; int yanked; char *notes, *about;
                 struct { char url[512], sha[72]; long long size; } as[2]; int has_assets; } ver_t;   /* as[0] armv7, as[1] aarch64 */
typedef struct { char s[48]; } str_t;
typedef struct { char verdict[8], pct[12]; } cpu_t;

static const char *cpu_member(const char *k, const char *v, void *ud) {
    cpu_t *c = ud;
    if (*v == '"' && !strcmp(k, "verdict")) return jstr(v, c->verdict, sizeof c->verdict);
    if (!strcmp(k, "p99_pct") && ((*v >= '0' && *v <= '9') || *v == '.')) snprintf(c->pct, sizeof c->pct, "%.1f%%", atof(v));
    return NULL;
}
static const char *about_member(const char *k, const char *v, void *ud) {
    return *v == '"' && !strcmp(k, "about") ? jstr_nl(v, (char *)ud, NOTES_MAX, 1) : NULL;
}
static const char *device_member(const char *k, const char *v, void *ud) {
    return *v == '"' && !strcmp(k, "device") ? jstr(v, ((str_t *)ud)->s, sizeof ((str_t *)ud)->s) : NULL;
}

/* the zip for this device: a Gen2 (aarch64) MPC takes the "aarch64" asset, anything else the "armv7" one */
static int want_asset(void) {
    struct utsname u;
    const char *a = getenv("PM_ARCH");   /* tests: pretend to be this machine */
    return a ? !strcmp(a, "aarch64") : uname(&u) == 0 && !strcmp(u.machine, "aarch64");
}
typedef struct { char url[512], sha[72]; long long size; } asset_t;
static const char *asset_member(const char *k, const char *v, void *ud) {
    asset_t *a = ud;
    if (*v == '"' && !strcmp(k, "url")) return jstr(v, a->url, sizeof a->url);
    if (*v == '"' && !strcmp(k, "sha256")) return jstr(v, a->sha, sizeof a->sha);
    if (!strcmp(k, "size") && *v >= '0' && *v <= '9') { a->size = atoll(v); return NULL; }
    return NULL;
}
static const char *assets_member(const char *k, const char *v, void *ud) {
    ver_t *x = ud;
    int i = !strcmp(k, "aarch64") ? 1 : !strcmp(k, "armv7") ? 0 : -1;
    if (i < 0 || *v != '{') return NULL;
    asset_t a = {{0}, {0}, 0};
    const char *q = jobject(v, asset_member, &a);
    snprintf(x->as[i].url, sizeof x->as[i].url, "%s", a.url);
    snprintf(x->as[i].sha, sizeof x->as[i].sha, "%s", a.sha);
    x->as[i].size = a.size;
    return q;
}

static const char *ver_member(const char *k, const char *v, void *ud) {
    ver_t *x = ud;
    if (!strcmp(k, "assets") && *v == '{') { x->has_assets = 1; return jobject(v, assets_member, x); }
    if (!strcmp(k, "channel") && *v == '"') return jstr(v, x->channel, sizeof x->channel);
    if (!strcmp(k, "size") && *v >= '0' && *v <= '9') { x->size = atoll(v); return NULL; }
    if (!strcmp(k, "cpu") && *v == '{') {   /* the bench verdict (PASS, WARN or FAIL) and p99 load */
        cpu_t t = {{0}, {0}};
        const char *q = jobject(v, cpu_member, &t);
        snprintf(x->cpu, sizeof x->cpu, "%s", t.verdict);
        snprintf(x->cpu_pct, sizeof x->cpu_pct, "%s", t.pct);
        return q;
    }
    if (!strcmp(k, "date") && *v == '"') return jstr(v, x->date, sizeof x->date);
    if (!strcmp(k, "notes") && *v == '"' && x->notes) return jstr_nl(v, x->notes, NOTES_MAX, 1);
    if (!strcmp(k, "manifest") && *v == '{' && x->about) return jobject(v, about_member, x->about);
    if (!strcmp(k, "tested") && *v == '[') {   /* devices it was tested on: the first one */
        const char *q = ws(v + 1);
        while (q && *q == '{') {
            str_t t = {{0}};
            q = jobject(q, device_member, &t);
            if (q && !x->tested[0]) snprintf(x->tested, sizeof x->tested, "%s", t.s);
            q = q ? ws(q) : NULL;
            if (q && *q == ',') q = ws(q + 1);
        }
        return q && *q == ']' ? q + 1 : NULL;
    }
    if (!strcmp(k, "version")) return jstr(v, x->version, sizeof x->version);
    if (!strcmp(k, "url")) return jstr(v, x->url, sizeof x->url);
    if (!strcmp(k, "sha256")) return jstr(v, x->sha, sizeof x->sha);
    if (!strcmp(k, "yanked")) { x->yanked = !strncmp(v, "true", 4); return NULL; }
    return NULL;
}

typedef struct { char uid[16]; } comp_t;
static const char *comp_member(const char *k, const char *v, void *ud) {
    return *v == '"' && !strcmp(k, "uid") ? jstr(v, ((comp_t *)ud)->uid, sizeof ((comp_t *)ud)->uid) : NULL;
}

/* "MdOn" (a 4-character VST id, as the catalog writes it) -> "4d644f6e" (as MPC.settings writes it) */
static void uid_hex(const char *u, char *out) {
    out[0] = 0;
    if (strlen(u) == 4) snprintf(out, 16, "%02x%02x%02x%02x", (unsigned char)u[0], (unsigned char)u[1], (unsigned char)u[2], (unsigned char)u[3]);
    else if (strlen(u) == 8) snprintf(out, 16, "%s", u);
}

static const char *pkg_member(const char *k, const char *v, void *ud) {
    pkg_t *x = ud;
    if (*v == '"') {
        if (!strcmp(k, "id")) return jstr(v, x->id, sizeof x->id);
        if (!strcmp(k, "name")) return jstr(v, x->name, sizeof x->name);
        if (!strcmp(k, "author")) return jstr(v, x->author, sizeof x->author);
        if (!strcmp(k, "kind")) return jstr(v, x->kind, sizeof x->kind);
        if (!strcmp(k, "distribution")) return jstr(v, x->dist, sizeof x->dist);
        if (!strcmp(k, "latest")) return x->url[0] ? NULL : jstr(v, x->latest, sizeof x->latest);   /* after "versions": the pick stands */
        if (!strcmp(k, "style")) return jstr(v, x->style, sizeof x->style);
        if (!strcmp(k, "summary")) return jstr(v, x->summary, sizeof x->summary);
    }
    if (!strcmp(k, "downloads") && *v >= '0' && *v <= '9') { x->downloads = atoi(v); return NULL; }
    if (!strcmp(k, "updated") && *v == '"') return jstr(v, x->updated, sizeof x->updated);
    if (!strcmp(k, "tags") && *v == '[') {   /* the first two, for the meta line */
        const char *q = ws(v + 1);
        for (int c = 0; q && *q == '"'; c++) {
            char t[32];
            q = jstr(q, t, sizeof t);
            if (q && c < 2) snprintf(x->tags + strlen(x->tags), sizeof x->tags - strlen(x->tags), "%s%s", c ? "  \xc2\xb7  " : "", t);
            if (q && strlen(x->alltags) + strlen(t) + 2 < sizeof x->alltags)
                snprintf(x->alltags + strlen(x->alltags), sizeof x->alltags - strlen(x->alltags), "%s%s|", x->alltags[0] ? "" : "|", t);
            q = q ? ws(q) : NULL;
            if (q && *q == ',') q = ws(q + 1);
        }
        return q && *q == ']' ? q + 1 : NULL;
    }
    if (!strcmp(k, "devices") && *v == '[') {   /* the devices it is for; none listed = every device */
        const char *q = ws(v + 1);
        while (q && *q == '"') {
            char t[40];
            q = jstr(q, t, sizeof t);
            if (q && strlen(x->devices) + strlen(t) + 2 < sizeof x->devices)
                snprintf(x->devices + strlen(x->devices), sizeof x->devices - strlen(x->devices), "%s%s|", x->devices[0] ? "" : "|", t);
            q = q ? ws(q) : NULL;
            if (q && *q == ',') q = ws(q + 1);
        }
        return q && *q == ']' ? q + 1 : NULL;
    }
    if (!strcmp(k, "components") && *v == '[') {   /* build-yourself: the VST uids it installs */
        const char *p = ws(v + 1);
        for (int c = 0; *p != ']'; c++) {
            comp_t cp = {{0}};
            p = jobject(p, comp_member, &cp);
            if (!p) return NULL;
            if (c < 4) uid_hex(cp.uid, x->uids[c]);
            p = ws(p);
            if (*p == ',') p = ws(p + 1);
        }
        return p + 1;
    }
    if (!strcmp(k, "versions") && *v == '[') {
        /* The version to offer: "latest" if it came first; else (catalog.json lists "versions" first, newest first) the
         * newest stable one that isn't yanked, or the newest beta for a plugin with no stable release. */
        const char *p = ws(v + 1);
        if (*p == ']') return p + 1;
        for (;;) {
            ver_t ver = {0};
            ver.notes = x->vnotes;
            ver.about = x->vabout;
            if (ver.notes) ver.notes[0] = 0;
            if (ver.about) ver.about[0] = 0;
            p = jobject(p, ver_member, &ver);
            if (!p) return NULL;
            if (ver.has_assets) {   /* the zip for this device; none for it: nothing to install here (the top-level url is the armv7 one) */
                int w = want_asset();
                snprintf(ver.url, sizeof ver.url, "%s", ver.as[w].url);
                snprintf(ver.sha, sizeof ver.sha, "%s", ver.as[w].sha);
                if (ver.as[w].url[0]) ver.size = ver.as[w].size;
            }
            if (!ver.yanked) x->nver++;
            if (!ver.yanked && ver.date[0] && (!x->first[0] || strcmp(ver.date, x->first) < 0)) snprintf(x->first, sizeof x->first, "%s", ver.date);
            int pinned = x->latest[0] && !x->url[0], beta = !strcmp(ver.channel, "beta");
            if (!ver.yanked && ver.url[0] && (pinned ? !strcmp(ver.version, x->latest)
                                                     : !x->url[0] || (!strcmp(x->channel, "beta") && !beta))) {
                snprintf(x->url, sizeof x->url, "%s", ver.url);
                snprintf(x->sha, sizeof x->sha, "%s", ver.sha);
                snprintf(x->channel, sizeof x->channel, "%s", ver.channel);
                snprintf(x->cpu, sizeof x->cpu, "%s", ver.cpu);
                snprintf(x->tested, sizeof x->tested, "%s", ver.tested);
                snprintf(x->date, sizeof x->date, "%s", ver.date);
                snprintf(x->cpu_pct, sizeof x->cpu_pct, "%s", ver.cpu_pct);
                x->size = ver.size;
                snprintf(x->latest, sizeof x->latest, "%s", ver.version);
                if (x->notes && ver.notes) snprintf(x->notes, NOTES_MAX, "%s", ver.notes);
                if (x->about && ver.about) snprintf(x->about, NOTES_MAX, "%s", ver.about);
            }
            p = ws(p);
            if (*p == ',') { p = ws(p + 1); continue; }
            return *p == ']' ? p + 1 : NULL;
        }
    }
    return NULL;
}

/* Markdown (len bytes of in) as one plain line: no emphasis, code marks, headings or images; links as their text;
 * list items as "* "; newlines as spaces */
static void md_line(const char *in, int len, char *out, int n) {
    int k = 0, bol = 1;
    for (int i = 0; i < len && k < n - 4; i++) {
        char c = in[i];
        if (c == '\r') continue;
        if (c == '\n') { bol = 1; if (k && out[k - 1] != ' ') out[k++] = ' '; continue; }
        if (bol) {
            while (i < len && (in[i] == ' ' || in[i] == '#' || in[i] == '>')) i++;
            if (i + 1 < len && (in[i] == '-' || in[i] == '*') && in[i + 1] == ' ') { out[k++] = '\xe2'; out[k++] = '\x80'; out[k++] = '\xa2'; out[k++] = ' '; i += 2; }
            bol = 0;
            if (i >= len) break;
            c = in[i];
        }
        if (c == '!' && i + 1 < len && in[i + 1] == '[') {   /* an image: drop it */
            const char *e = memchr(in + i, ')', len - i);
            if (e) { i = (int)(e - in); continue; }
        }
        if (c == '[') {   /* [text](url): the text */
            const char *e = memchr(in + i, ']', len - i);
            if (e && e + 1 < in + len && e[1] == '(') {
                const char *u = memchr(e, ')', in + len - e);
                for (const char *t = in + i + 1; t < e && k < n - 4; t++) out[k++] = *t;
                if (u) { i = (int)(u - in); continue; }
            }
        }
        if (c == '*' || c == '`' || (c == '_' && i + 1 < len && in[i + 1] == '_')) continue;
        if (c == ' ' && k && out[k - 1] == ' ') continue;
        out[k++] = c;
    }
    while (k && out[k - 1] == ' ') k--;
    out[k] = 0;
}

static int line_is(const char *l, const char *word) {   /* a heading line whose text starts with word (any case) */
    if (*l != '#') return 0;
    while (*l == '#' || *l == ' ') l++;
    return !strncasecmp(l, word, strlen(word));
}

/* The detail view's "What's new": the release notes' "What's new" / "Changes" section; else, past the first release,
 * their first paragraph unless it just describes the plugin (the summary or the manifest's "about"); else
 * "First release." or "No release notes." (agreed 2026-10-05) */
static void whats_new(pkg_t *x) {
    const char *nt = x->notes ? x->notes : "";
    x->wn_kind = 2;
    x->whatsnew[0] = 0;
    for (const char *l = nt; *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : l + strlen(l)) {
        if (line_is(l, "what") || line_is(l, "change")) {
            const char *b = strchr(l, '\n'), *e;
            if (!b) break;
            for (e = ++b; *e && !(*e == '#' && (e == b || e[-1] == '\n')); e++) {}
            md_line(b, (int)(e - b), x->whatsnew, sizeof x->whatsnew);
            if (x->whatsnew[0]) { x->wn_kind = 1; return; }
            break;
        }
    }
    if (x->nver > 1) {
        const char *b = nt, *e;
        for (;;) {   /* the first paragraph that isn't a heading or an image */
            while (*b == '\n' || *b == '\r' || *b == ' ') b++;
            if ((*b == '#' || *b == '!') && strchr(b, '\n')) { b = strchr(b, '\n'); continue; }
            break;
        }
        e = strstr(b, "\n\n");
        char para[480], about[480];
        md_line(b, e ? (int)(e - b) : (int)strlen(b), para, sizeof para);
        const char *ab = x->about ? x->about : "", *ae = strstr(ab, "\n\n");
        md_line(ab, ae ? (int)(ae - ab) : (int)strlen(ab), about, sizeof about);
        int cut = 60;
        if (para[0] && strncmp(para, x->summary, cut) && strncmp(para, about, cut)) {
            snprintf(x->whatsnew, sizeof x->whatsnew, "%s", para);
            x->wn_kind = 1;
            return;
        }
    }
    snprintf(x->whatsnew, sizeof x->whatsnew, "%s", x->nver > 1 ? "No release notes." : "First release.");
}

typedef struct { pkg_t *pkg; int n; } list_t;

static const char *top_member(const char *k, const char *v, void *ud) {
    list_t *l = ud;
    if (strcmp(k, "plugins") || *v != '[') return NULL;
    const char *p = ws(v + 1);
    if (*p == ']') return p + 1;
    char *notes = malloc(NOTES_MAX), *about = malloc(NOTES_MAX), *vnotes = malloc(NOTES_MAX), *vabout = malloc(NOTES_MAX);
    #define FREE_NOTES() (free(notes), free(about), free(vnotes), free(vabout))
    if (!notes || !about || !vnotes || !vabout) { FREE_NOTES(); return NULL; }
    for (;;) {
        pkg_t x;
        memset(&x, 0, sizeof x);
        x.notes = notes;
        x.about = about;
        x.vnotes = vnotes;
        x.vabout = vabout;
        notes[0] = about[0] = 0;
        p = jobject(p, pkg_member, &x);
        if (!p) { FREE_NOTES(); return NULL; }
        whats_new(&x);
        x.notes = x.about = x.vnotes = x.vabout = NULL;
        if (x.id[0] && l->n < MAXPKG) l->pkg[l->n++] = x;
        p = ws(p);
        if (*p == ',') { p = ws(p + 1); continue; }
        FREE_NOTES();
        #undef FREE_NOTES
        return *p == ']' ? p + 1 : NULL;
    }
}

/* ---- the device: everything model-specific is read from it, nothing is assumed ---- */

/* Models differ in where plugins live (MPC One: /media/az01-internal/Synths, /sdcard is a stub on the full root fs;
 * Force: /sdcard/Synths), in what MPC preloads into its children, and possibly in their tools. So: install
 * locations come from MPC.settings' SynthContentLocations, filtered to internal, writable, exec-allowed filesystems;
 * what is installed comes from the pluginList-arm entries (uid, file=) and the mpc-plugin.json next to each .so;
 * missing tools are reported on screen and in WORK/device.txt, which testers attach to a report. */

typedef struct { char name[64], uid[16], file[256]; } entry_t;
typedef struct { char id[48], version[24], folder[256]; int removable; } addin_t;   /* an installed addin folder */
struct device {
    char model_id[48];   /* "mpc-live-iii": /proc/device-tree/model without "Akai Professional", lower case, dashes; "" when unknown (nothing is hidden) */
    char settings[200], base[200], state[220], arch[32], target[200], problem[24], keys[160], via[48];
    char loc[8][200];
    char uname[8][32];     /* their names in the drive menu: "Internal" or the drive's label (/media/<label>) */
    char usable[8][200];   /* the usable locations (bad_target() says nothing against them), internal drive first; the first is the default */
    int nusable;
    char addins[64];   /* ADDINS_DIR when its parent is a writable folder, else "" (no addin can be installed) */
    int nloc, nent, naddin, systemd_run, cgroup2;
    entry_t ent[128];
    struct { char folder[300], name[64], uid[16], file[320], entry[1500]; int inst; } orphan[8];   /* folders with a plugin-meta.xml and no entry */
    int norphan;
    addin_t addin[32];
    int vst2;              /* MPC OS 3.10.1+: 0 no VST2 Support setting (older OS), 1 off, 2 on (Enhancements.VST2Support in MPC.settings) */
    char vst2dir[220];     /* "<drive of the target>/VST2 Plugins": where 3.10.1 looks for plugins (and where installs go while vst2 == 2) */
    char cand[8][200];     /* LOCATIONS: Synths folders on mounted drives MPC does not list (registered with REGISTER), cand[k] = "<mount>/Synths" */
    int ncand, hasloclist; /* hasloclist: MPC.settings has a SynthContentLocations element a Location can be added to */
    int nmove;             /* plugin folders in a usable Synths location with no copy in vst2dir yet */
    char vdrv[4][220], vname[4][32];   /* 3.10.1 with VST2 Support on: "<drive>/VST2 Plugins" on each writable drive (internal first), its name */
    int nvdrv;
};

static int on_path(const char *tool) {
    static const char *dirs[] = {"/usr/bin", "/bin", "/usr/sbin", "/sbin"};
    char p[64];
    for (int i = 0; i < 4; i++) {
        snprintf(p, sizeof p, "%s/%s", dirs[i], tool);
        if (access(p, X_OK) == 0) return 1;
    }
    return 0;
}

static char *slurp(const char *path, size_t max) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *b = malloc(max + 1);
    size_t n = b ? fread(b, 1, max, f) : 0;
    fclose(f);
    if (b) b[n] = 0;
    return b;
}

/* an attribute of one <PLUGIN .../> tag (tag..end, whitespace already flattened to spaces) */
static void attr(const char *tag, const char *end, const char *name, char *out, size_t n) {
    char pat[32];
    snprintf(pat, sizeof pat, " %s=\"", name);
    out[0] = 0;
    const char *p = strstr(tag, pat), *q;
    if (!p || p >= end) return;
    p += strlen(pat);
    if ((q = strchr(p, '"')) && q < end) snprintf(out, n, "%.*s", (int)(q - p), p);
}

/* /proc/mounts writes a space, tab, newline or backslash in a mount point as \040, \011, \012, \134 (a USB drive labelled "My HDD" is
 * /media/My\040HDD): decode it in place, or the drive never matches its own path (a user's MPC One, 2026-11-03: "/media is tmpfs"). */
static void mount_unescape(char *s) {
    char *o = s;
    for (const char *i = s; *i; ) {
        if (i[0] == '\\' && i[1] >= '0' && i[1] <= '3' && i[2] >= '0' && i[2] <= '7' && i[3] >= '0' && i[3] <= '7') {
            *o++ = (char)((i[1] - '0') * 64 + (i[2] - '0') * 8 + (i[3] - '0')); i += 4;
        } else *o++ = *i++;
    }
    *o = 0;
}

/* the filesystem a path is on, from /proc/mounts (longest matching mount point); why it can't hold a plugin, or NULL.
 * A plugin needs a mount that is read-write and not noexec, and that keeps its files: so there is no list of
 * allowed filesystems (Gen2's internal /nvme is exFAT, and a user's SD card may be too), only a few that lose or never
 * take files. Whether the drive can store symbolic links is a per-package question, asked at install time. */
static const char *bad_fs(const char *path, char *why, size_t n) {
#ifdef FS_ANY   /* tests: a fake /proc/mounts, since a container cannot mount a read-only or noexec drive */
    FILE *f = fopen(!access("/tmp/pm-mounts", R_OK) ? "/tmp/pm-mounts" : "/proc/mounts", "r");
#else
    FILE *f = fopen("/proc/mounts", "r");
#endif
    char dev[128], mnt[256], type[32], opts[512];
    size_t best = 0;
    int ok = 0;
    snprintf(why, n, "not on a mounted filesystem");
    while (f && fscanf(f, "%127s %255s %31s %511s %*d %*d", dev, mnt, type, opts) == 4) {
        mount_unescape(mnt);
        size_t l = strlen(mnt);
        if (strncmp(path, mnt, l) || (path[l] && path[l] != '/' && l > 1) || l < best) continue;
        best = l;
        char *o = opts;   /* "rw" as an option of its own, not the start of another word */
        int rw = !strncmp(o, "rw", 2) && (o[2] == ',' || !o[2]), exec = !strstr(opts, "noexec");
        int volatile_fs = !strcmp(type, "tmpfs") || !strcmp(type, "ramfs") || !strcmp(type, "devtmpfs") || !strcmp(type, "squashfs")
                          || !strcmp(type, "erofs") || !strcmp(type, "iso9660") || !strcmp(type, "cramfs");
        ok = rw && exec && !volatile_fs;
        snprintf(why, n, "%s is %s%s%s%s", mnt, type, rw ? "" : ", read-only", exec ? "" : ", noexec", volatile_fs ? ", not kept across restarts" : "");
    }
    if (f) fclose(f);
    return ok ? NULL : why;
}

/* Is the drive under `path` part of the device (eMMC, NVMe, a loop image of its storage) rather than something that can be pulled out (a USB stick,
 * the SD card: /dev/sd*, /dev/mmcblk1*)? The MPC One Gen2 has no /nvme: its internal Synths folder is /storage/Synths (loop0, exFAT; /synths is read-only), the SD card is /media/MPC. */
static int internal_device(const char *path) {
#ifdef FS_ANY
    FILE *f = fopen(!access("/tmp/pm-mounts", R_OK) ? "/tmp/pm-mounts" : "/proc/mounts", "r");
#else
    FILE *f = fopen("/proc/mounts", "r");
#endif
    char dev[128], mnt[256], type[32], opts[512], best_dev[128] = "";
    size_t best = 0;
    while (f && fscanf(f, "%127s %255s %31s %511s %*d %*d", dev, mnt, type, opts) == 4) {
        mount_unescape(mnt);
        size_t l = strlen(mnt);
        if (strncmp(path, mnt, l) || (path[l] && path[l] != '/' && l > 1) || l < best) continue;
        best = l;
        snprintf(best_dev, sizeof best_dev, "%s", dev);
    }
    if (f) fclose(f);
    return !strncmp(best_dev, "/dev/nvme", 9) || !strncmp(best_dev, "/dev/mmcblk0", 12) || !strncmp(best_dev, "/dev/loop", 9);
}

/* The banner and the status line for a `problem` (short: the banner is 590 px of 23 px text): what is wrong, in plain words. The reasons per
 * location are in device.txt. */
static void problem_words(const device_t *d, char *out, size_t n) {
    const char *p = d->problem;
    if (!strcmp(p, "No MPC.settings")) snprintf(out, n, "No MPC.settings found: nothing can be installed");
    else if (!strcmp(p, "No install location") && !d->nloc) snprintf(out, n, "MPC lists no Synths folder to install into");
    else if (!strcmp(p, "No install location")) snprintf(out, n, "None of MPC's %d Synths folders can take a plugin", d->nloc);
    else if (!strncmp(p, "No ", 3)) snprintf(out, n, "This MPC lacks %s, which the manager needs", p + 3);
    else snprintf(out, n, "%s", p);
}

/* why a Synths folder can't take a plugin (.so), or NULL if it can */
static const char *bad_target(const char *path, char *why, size_t n) {
    char loc[200], parent[200];
    struct statvfs sv;
    snprintf(loc, sizeof loc, "%s", path);
    for (size_t l = strlen(loc); l > 1 && loc[l - 1] == '/'; ) loc[--l] = 0;
    snprintf(parent, sizeof parent, "%s", loc);
    char *slash = strrchr(parent, '/');
    if (slash && slash != parent) *slash = 0;
    const char *leaf = strrchr(loc, '/');   /* plugin folders go in a Synths folder, never Akai's Expansions content */
    if (!leaf || strcmp(leaf, "/Synths")) return "not a Synths folder";
    if (!strncmp(loc, "/usr/", 5)) return "factory content";
    if (!safe(loc) || strchr(loc, '&') || strchr(loc, '|')) return "unsupported characters";
    const char *at = is_dir(loc) ? loc : parent;
    if (!is_dir(at)) return "missing";
    if (bad_fs(at, why, n)) return why;
    if (statvfs(at, &sv) || (unsigned long long)sv.f_bavail * sv.f_frsize < 64ull << 20) return "under 64 MB free";
    return NULL;
}

static int folder_id(const char *p, dev_t *dv, ino_t *in, int *made) {
    struct stat st;
    char par[200];
    *made = stat(p, &st) == 0;
    if (!*made) {
        snprintf(par, sizeof par, "%s", p);
        char *sl = strrchr(par, '/');
        if (!sl || sl == par || stat((*sl = 0, par), &st)) return 0;
    }
    *dv = st.st_dev; *in = st.st_ino;
    return 1;
}
static int same_folder(const char *a, const char *b) {
    dev_t da, db; ino_t ia, ib; int ma, mb;
    if (!folder_id(a, &da, &ia, &ma) || !folder_id(b, &db, &ib, &mb) || ma != mb || da != db || ia != ib) return 0;
    if (ma) return 1;
    const char *x = strrchr(a, '/'), *y = strrchr(b, '/');   /* both not made yet: same parent and same last name */
    return x && y && !strcmp(x, y);
}

static int good_target(const char *loc) {
    char why[320];
    return !bad_target(loc, why, sizeof why);
}


/* ---- LOCATIONS tab: what is known about each Synths folder MPC lists ---- */
static int loc_target_index(const mgr_t *m, const char *path);   /* below, with the other target helpers */
static int ntargets(const mgr_t *m);

static int mount_of(const char *path, char *fs, size_t nf, int *rw, int *exec) {
#ifdef FS_ANY
    FILE *f = fopen(!access("/tmp/pm-mounts", R_OK) ? "/tmp/pm-mounts" : "/proc/mounts", "r");
#else
    FILE *f = fopen("/proc/mounts", "r");
#endif
    char dev[128], mnt[256], type[32], opts[512];
    size_t best = 0;
    int found = 0;
    while (f && fscanf(f, "%127s %255s %31s %511s %*d %*d", dev, mnt, type, opts) == 4) {
        mount_unescape(mnt);
        size_t l = strlen(mnt);
        if (strncmp(path, mnt, l) || (path[l] && path[l] != '/' && l > 1) || l < best) continue;
        best = l; found = 1;
        snprintf(fs, nf, "%s", type);
        *rw = !strncmp(opts, "rw", 2) && (opts[2] == ',' || !opts[2]);
        *exec = !strstr(opts, "noexec");
    }
    if (f) fclose(f);
    return found;
}

static void loc_fill(const mgr_t *m, const char *loc, int cand, locrow_t *r) {   /* cand: a mounted drive MPC does not list */
    memset(r, 0, sizeof *r);
    r->cand = cand;
    snprintf(r->path, sizeof r->path, "%s", loc);
    char path[200], why[320], fs[32] = "?", parent[200];
    snprintf(path, sizeof path, "%s", loc);
    for (size_t l = strlen(path); l > 1 && path[l - 1] == '/'; ) path[--l] = 0;
    snprintf(parent, sizeof parent, "%s", path);
    char *sl = strrchr(parent, '/');
    if (sl && sl != parent) *sl = 0;
    const char *at = is_dir(path) ? path : parent;
    int internal = !cand && (!strncmp(path, "/media/az01-internal", 20) || !strncmp(path, "/sdcard/", 8) || !strncmp(path, "/nvme/", 6) || internal_device(path));
    const char *lab = !strncmp(path, "/media/", 7) && !internal ? path + 7 : NULL;
    snprintf(r->name, sizeof r->name, "%.*s  \xc2\xb7  %s", lab ? (int)strcspn(lab, "/") : 8, lab ? lab : "Internal", path);
    int rw = 0, ex = 0;
    mount_of(at, fs, sizeof fs, &rw, &ex);
    struct statvfs sv;
    char sz[64] = "size unknown";
    if (statvfs(at, &sv) == 0 && sv.f_blocks) {
        double tot = (double)sv.f_blocks * sv.f_frsize / 1e9, fr = (double)sv.f_bavail * sv.f_frsize / 1e9;
        snprintf(sz, sizeof sz, "%.1f of %.1f GB used", tot - fr, tot);
        r->bar = (int)((tot - fr) / tot * 10 + 0.5);
        if (r->bar < 0) r->bar = 0; else if (r->bar > 10) r->bar = 10;
    }
    snprintf(r->info, sizeof r->info, "%s  \xc2\xb7  %s  \xc2\xb7  %s  \xc2\xb7  %s", fs, rw ? "read-write" : "read-only", ex ? "exec" : "noexec", sz);
    int n = 0;
    DIR *dd = opendir(path);
    struct dirent *e;
    while (dd && (e = readdir(dd))) if (e->d_name[0] != '.' && strstr(e->d_name, " - VST - ")) n++;
    if (dd) closedir(dd);
    const char *bad = bad_target(path, why, sizeof why);
    r->usable = !bad;
    int ti = bad || cand ? -1 : loc_target_index(m, path);
    r->dflt = ti >= 0 && ti == m->tsel % ntargets(m);   /* where new installs go now: the remembered place, else the first usable */
    r->pick = ti >= 0 && !r->dflt;
    if (bad) snprintf(r->state, sizeof r->state, "Can't install here: %s", bad);
    else if (cand) snprintf(r->state, sizeof r->state, "MPC does not list this drive  \xc2\xb7  REGISTER adds its Synths folder");
    else snprintf(r->state, sizeof r->state, "%s  \xc2\xb7  %d plugin%s", r->dflt ? "Default install location" : "Ready", n, n == 1 ? "" : "s");
    r->vis = 1;
}

/* The LOCATIONS rows, in the order shown: listed folders that can take a plugin, then drives MPC does not list, then the folders that cannot.
 * Built by the worker on each refresh (each row reads the mounts and the folder), served from the copy while MPC polls the page. */
static void loc_build(mgr_t *m) {
    locrow_t *rows = calloc(16, sizeof *rows);
    if (!rows) return;
    const device_t *d = m->dev;
    int n = 0;
    for (int pass = 0; pass < 3; pass++) {
        for (int k = 0; k < (pass == 1 ? d->ncand : d->nloc) && n < 16; k++) {
            locrow_t r;
            loc_fill(m, pass == 1 ? d->cand[k] : d->loc[k], pass == 1, &r);
            if (pass != 1 && (pass == 0) != r.usable) continue;
            rows[n++] = r;
        }
    }
    pthread_mutex_lock(&m->mu);
    memcpy(m->locs, rows, (size_t)n * sizeof *rows);
    m->nlocs = n;
    if (m->locpage * 4 >= n) m->locpage = n ? (n - 1) / 4 : 0;
    int w = 0;   /* a queued REGISTER for a drive that is listed now (or gone) is dropped */
    for (int q = 0; q < m->nregq; q++) {
        int keep = 0;
        for (int k = 0; k < d->ncand; k++) if (!strcmp(d->cand[k], m->regq[q])) keep = 1;
        if (keep) { if (w != q) snprintf(m->regq[w], sizeof m->regq[w], "%s", m->regq[q]); w++; }
    }
    m->nregq = w;
    pthread_mutex_unlock(&m->mu);
    free(rows);
}

/* ---- PATCHES tab ---- */
static const char *pt_arr_join(const char *v, char *out, size_t n) {   /* ["a", "b"] -> "a, b" */
    const char *p = ws(v);
    size_t k = 0;
    out[0] = 0;
    if (*p != '[') return NULL;
    p = ws(p + 1);
    while (*p == '"') {
        char one[120];
        p = jstr(p, one, sizeof one);
        if (!p) return NULL;
        k += snprintf(out + k, k < n ? n - k : 0, "%s%s", k ? ", " : "", one);
        if (k >= n) k = n - 1;
        p = ws(p);
        if (*p == ',') p = ws(p + 1);
    }
    return *p == ']' ? p + 1 : NULL;
}
static const char *pt_script(const char *key, const char *v, void *ud) {
    patch_t *x = ud;
    if (!strcmp(key, "url") && *v == '"') return jstr(v, x->url, sizeof x->url);
    if (!strcmp(key, "sha256") && *v == '"') return jstr(v, x->sha, sizeof x->sha);
    return NULL;
}
static const char *pt_supports(const char *key, const char *v, void *ud) {
    patch_t *x = ud;
    if (!strcmp(key, "os") && *v == '"') return jstr(v, x->os, sizeof x->os);
    if (!strcmp(key, "arch") && *v == '"') return jstr(v, x->arch, sizeof x->arch);
    return NULL;
}
static const char *pt_member(const char *key, const char *v, void *ud) {
    patch_t *x = ud;
    if (!strcmp(key, "id") && *v == '"') return jstr(v, x->id, sizeof x->id);
    if (!strcmp(key, "title") && *v == '"') return jstr(v, x->title, sizeof x->title);
    if (!strcmp(key, "summary") && *v == '"') return jstr(v, x->summary, sizeof x->summary);
    if (!strcmp(key, "author") && *v == '"') return jstr(v, x->author, sizeof x->author);
    if (!strcmp(key, "backup") && *v == '"') return jstr(v, x->backup, sizeof x->backup);
    if (!strcmp(key, "restarts_mpc")) { x->restarts = *v == 't'; return jskip(v); }
    if (!strcmp(key, "reversible")) { x->reversible = *v == 't'; return jskip(v); }
    if (!strcmp(key, "modifies")) return pt_arr_join(v, x->modifies, sizeof x->modifies);
    if (!strcmp(key, "script")) return jobject(v, pt_script, x);
    if (!strcmp(key, "supports")) return jobject(v, pt_supports, x);
    return NULL;
}
typedef struct { patch_t *a; int n; } ptlist_t;
static const char *pt_top(const char *key, const char *v, void *ud) {
    ptlist_t *l = ud;
    if (strcmp(key, "patches")) return NULL;
    const char *p = ws(v);
    if (*p != '[') return NULL;
    p = ws(p + 1);
    while (*p == '{' && l->n < 8) {
        patch_t *x = &l->a[l->n];
        memset(x, 0, sizeof *x);
        p = jobject(p, pt_member, x);
        if (!p) return NULL;
        if (x->id[0] && safe(x->id) && x->title[0]) l->n++;
        p = ws(p);
        if (*p == ',') p = ws(p + 1);
    }
    return *p == ']' ? p + 1 : NULL;
}
static void patches_load(mgr_t *m) {   /* the worker, on each refresh; nothing changes on the device */
    patch_t *tmp = calloc(8, sizeof *tmp);
    if (!tmp) return;
    ptlist_t l = {tmp, 0};
    sink_t s = {0};
    int ok = fetch(m, PATCHES_URL, &s) == 0 && s.buf && jobject(s.buf, pt_top, &l);
    if (!ok && PATCHES_URL_FALLBACK[0]) {
        free(s.buf);
        memset(&s, 0, sizeof s);
        l.n = 0;
        ok = fetch(m, PATCHES_URL_FALLBACK, &s) == 0 && s.buf && jobject(s.buf, pt_top, &l);
    }
    free(s.buf);
    if (!ok) { free(tmp); return; }   /* offline: keep the list we had */
    pthread_mutex_lock(&m->mu);
    for (int i = 0; i < l.n; i++)   /* a check made earlier still stands if the script is the same */
        for (int k = 0; k < m->npt; k++)
            if (!strcmp(m->pt[k].id, tmp[i].id) && !strcmp(m->pt[k].sha, tmp[i].sha) && m->pt[k].checked) {
                tmp[i].checked = 1; tmp[i].supported = m->pt[k].supported;
                snprintf(tmp[i].state, sizeof tmp[i].state, "%s", m->pt[k].state);
                snprintf(tmp[i].reason, sizeof tmp[i].reason, "%s", m->pt[k].reason);
            }
    memcpy(m->pt, tmp, (size_t)l.n * sizeof *tmp);
    m->npt = l.n;
    if (m->ptpage * 3 >= l.n) m->ptpage = l.n ? (l.n - 1) / 3 : 0;
    m->rev++;
    pthread_mutex_unlock(&m->mu);
    free(tmp);
}
static int pt_arch_ok(const mgr_t *m, const patch_t *p) {
    char a[48];
    snprintf(a, sizeof a, "%s", m->dev->arch);
    return !p->arch[0] || strstr(p->arch, a) != NULL;
}

static long long mono_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000; }
/* what the page offers for a checked patch: 1 APPLY (the script says stock and supported), 2 UNDO (it says patched), 0 nothing. A patch that
 * restarts MPC is not run from this screen: the script stops MPC, and the manager lives inside it. */
static int pt_act_kind(const mgr_t *m, const patch_t *p) {
    if (!p || !pt_arch_ok(m, p) || !p->checked || p->restarts) return 0;
    if (p->supported && !strcmp(p->state, "stock")) return 1;
    if (!strcmp(p->state, "patched") && p->reversible) return 2;
    return 0;
}

/* A patch's script: fetched, written to `file` and compared with the sha256 in patches.json. 0 when it is there and matches; else why not. */
static const char *patch_script(mgr_t *m, const patch_t *p, const char *file) {
    sink_t s = {0};
    const char *why = NULL;
    if (strlen(p->sha) != 64 || !p->url[0]) return "no script in the list";
    if (fetch(m, p->url, &s) || !s.buf || !s.len) why = "the script could not be fetched";
    else {
        FILE *f = fopen(file, "w");
        if (f) { fwrite(s.buf, 1, s.len, f); fclose(f); }
        char cmd[400], hash[80] = "";
        snprintf(cmd, sizeof cmd, "sha256sum '%s' 2>/dev/null | cut -d' ' -f1", file);
        FILE *h = popen(cmd, "r");
        if (h) { if (fgets(hash, sizeof hash, h)) hash[strcspn(hash, "\r\n")] = 0; pclose(h); }
        if (strcmp(hash, p->sha)) { why = "the script does not match its checksum: not run"; unlink(file); }
    }
    free(s.buf);
    return why;
}

/* Run `sh file <a1> <a2>` with a clean environment (no LD_PRELOAD of MPC's addins), output to `outfile`, and a watchdog (busybox here has no
 * `timeout`). Returns the script's exit status, -1 when it did not finish or could not start. */
static int patch_run(const char *file, const char *a1, const char *a2, int secs, const char *outfile) {
    char cmd[700], rcf[300];
    snprintf(rcf, sizeof rcf, "%s.rc", outfile);
    unlink(rcf);
    snprintf(cmd, sizeof cmd, "env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME=/root sh -c "
             "'sh \"$1\" $3 $4 >\"$2\" 2>&1 </dev/null & p=$!; ( sleep $5; kill $p ) >/dev/null 2>&1 & w=$!; wait $p; echo $? >\"$2.rc\"; kill $w 2>/dev/null' "
             "sh '%s' '%s' '%s' '%s' %d", file, outfile, a1, a2, secs);
    sh(cmd);
    int rc = -1;
    FILE *f = fopen(rcf, "r");
    if (f) { if (fscanf(f, "%d", &rc) != 1) rc = -1; fclose(f); }
    unlink(rcf);
    return rc;
}

static void patch_state_from(const char *out, char *state, size_t ns, int *sup, char *reason, size_t nr) {   /* the script's last STATE line */
    char *st = strstr(out, "STATE state=");
    while (st && strstr(st + 1, "STATE state=")) st = strstr(st + 1, "STATE state=");
    if (!st) { snprintf(reason, nr, "the script gave no STATE line"); return; }
    char line[300];
    snprintf(line, sizeof line, "%.*s", (int)strcspn(st, "\r\n"), st);
    char sb[24] = "";
    sscanf(line, "STATE state=%23s", sb);
    snprintf(state, ns, "%s", sb);
    char *sp = strstr(line, "supported=");
    *sup = sp && sp[10] == '1';
    char *rs = strstr(line, "reason=");
    if (rs) snprintf(reason, nr, "%s", rs + 7);
}

static char *patch_slurp_out(void) {
    char *o = slurp(WORK "/patch-out.txt", 16384);
    unlink(WORK "/patch-out.txt");
    return o;
}

/* CHECK: fetch the patch's script, verify its sha256 against patches.json, and run `status` (the contract: it changes nothing). The script's
 * last "STATE state=<s> supported=<0|1> ... reason=<text>" line is what the page shows. */
static void patch_store(mgr_t *m, const char *id, const char *state, int sup, const char *reason) {
    pthread_mutex_lock(&m->mu);
    for (int i = 0; i < m->npt; i++)
        if (!strcmp(m->pt[i].id, id)) {
            m->pt[i].checked = 1;
            m->pt[i].supported = sup;
            snprintf(m->pt[i].state, sizeof m->pt[i].state, "%s", state);
            snprintf(m->pt[i].reason, sizeof m->pt[i].reason, "%s", reason);
        }
    m->rev++;
    pthread_mutex_unlock(&m->mu);
}

static int patch_status(mgr_t *m, const patch_t *p, const char *file, char *state, size_t ns, int *sup, char *reason, size_t nr) {
    snprintf(state, ns, "error");
    *sup = 0;
    reason[0] = 0;
    int rc = patch_run(file, "status", "", 60, WORK "/patch-out.txt");
    char *out = patch_slurp_out();
    if (rc < 0 && !out) snprintf(reason, nr, "the script did not finish");
    else patch_state_from(out ? out : "", state, ns, sup, reason, nr);
    free(out);
    (void)m; (void)p;
    return 0;
}

static void do_patch(mgr_t *m) {
    pthread_mutex_lock(&m->mu);
    int k = m->ptidx;
    patch_t p = k >= 0 && k < m->npt ? m->pt[k] : (patch_t){0};
    pthread_mutex_unlock(&m->mu);
    if (!p.id[0]) return;
    char state[24] = "error", reason[100] = "", file[300];
    int sup = 0;
    snprintf(file, sizeof file, WORK "/patch-%s.sh", p.id);
    const char *why = patch_script(m, &p, file);
    if (why) snprintf(reason, sizeof reason, "%s", why);
    else { patch_status(m, &p, file, state, sizeof state, &sup, reason, sizeof reason); unlink(file); }
    logf_("patch %s status: %s supported=%d %s", p.id, state, sup, reason);
    patch_store(m, p.id, state, sup, reason);
    pthread_mutex_lock(&m->mu);
    m->sticky = 0;
    m->rev++;
    pthread_mutex_unlock(&m->mu);
}

/* APPLY / UNDO a patch that does not restart MPC: the script's own `install --confirmed` / `uninstall --confirmed` (it keeps its own backup and
 * puts things back if it fails), then `status` again. The screen asked twice first. The output is kept in <work>/patch-<id>.log. */
static void do_papply(mgr_t *m) {
    pthread_mutex_lock(&m->mu);
    int k = m->ptidx, undo = m->ptact == 2;
    patch_t p = k >= 0 && k < m->npt ? m->pt[k] : (patch_t){0};
    pthread_mutex_unlock(&m->mu);
    if (!p.id[0] || p.restarts) return;   /* restarting patches are not run from here */
    char file[300], log[300], state[24] = "error", reason[100] = "";
    int sup = 0;
    snprintf(file, sizeof file, WORK "/patch-%s.sh", p.id);
    snprintf(log, sizeof log, WORK "/patch-%s.log", p.id);
    const char *why = patch_script(m, &p, file);
    if (why) {
        say(m, ERR, "%s: %s. Nothing was changed.", p.title, why);
        return;
    }
    say(m, OK, "%s \"%s\"\xe2\x80\xa6", undo ? "Removing" : "Applying", p.title);
    int rc = patch_run(file, undo ? "uninstall" : "install", "--confirmed", 180, WORK "/patch-out.txt");
    char *out = slurp(WORK "/patch-out.txt", 16384);
    if (out) { FILE *lf = fopen(log, "w"); if (lf) { fputs(out, lf); fclose(lf); } }
    unlink(WORK "/patch-out.txt");
    char last[160] = "";
    if (out) {   /* the last line that is not the STATE line, for the status message */
        char *e = out + strlen(out);
        while (e > out && (e[-1] == '\n' || e[-1] == '\r')) *--e = 0;
        char *ls = strrchr(out, '\n');
        snprintf(last, sizeof last, "%s", ls ? ls + 1 : out);
    }
    free(out);
    patch_status(m, &p, file, state, sizeof state, &sup, reason, sizeof reason);
    unlink(file);
    patch_store(m, p.id, state, sup, reason);
    logf_("patch %s %s: exit %d, now %s", p.id, undo ? "uninstall" : "install", rc, state);
    int good = rc == 0 && (undo ? strcmp(state, "patched") : !strcmp(state, "patched"));
    if (good) say(m, OK, "%s: %s. Now: %s.", p.title, undo ? "removed" : "applied", state);
    else say(m, ERR, "%s: %s did not finish (exit %d, now %s). %s", p.title, undo ? "removing" : "applying", rc, state, last);
}

/* The theme: the skin the device runs is the folder "Plugin Skins" in this plugin's own folder. Both skins ship there as "Plugin Skins.dark"
 * and "Plugin Skins.light"; an apply copies the wanted one over it while MPC is stopped (MPC keeps skins in memory, so a swap shows after a
 * restart). The wanted theme is kept in <base>/.openmpcpm-theme; the one in place is named by the file ".theme" inside each skin folder. */
static void own_folder(char *out, size_t n) {
    out[0] = 0;
#ifdef FS_ANY
    if (getenv("PM_OWN")) { snprintf(out, n, "%s", getenv("PM_OWN")); return; }   /* tests: the binary is not in a plugin folder */
#endif
    Dl_info di;
    if (!dladdr((void *)own_folder, &di) || !di.dli_fname || di.dli_fname[0] != '/') return;
    snprintf(out, n, "%s", di.dli_fname);
    char *sl = strrchr(out, '/');
    if (sl && sl != out) *sl = 0;
}

static int theme_read(const char *base, const char *name, int def) {   /* 1 for "light", 0 for "dark"; def when there is no such file */
    char f[300], b[16] = "";
    snprintf(f, sizeof f, "%s/%s", base, name);
    FILE *fp = fopen(f, "r");
    if (!fp) return def;
    if (!fgets(b, sizeof b, fp)) b[0] = 0;
    fclose(fp);
    return !strncmp(b, "light", 5);
}

static void theme_init(mgr_t *m) {   /* after the device probe: where the settings are is where the theme files go */
    char a[340], b[340];
    own_folder(m->own, sizeof m->own);
    m->theme_can = 0;
    if (m->own[0] && m->dev->base[0]) {
        snprintf(a, sizeof a, "%s/Plugin Skins.light", m->own);
        snprintf(b, sizeof b, "%s/Plugin Skins.dark", m->own);
        m->theme_can = is_dir(a) && is_dir(b);
        m->theme = theme_read(m->dev->base, ".openmpcpm-theme", 1);   /* light is the default */
        snprintf(a, sizeof a, "%s/Plugin Skins", m->own);
        m->theme_applied = theme_read(a, ".theme", 1);   /* inside the skin itself: a reinstall of this plugin brings the packaged (dark) skin and this file together */
    }
}

/* SETTINGS: "key=0|1" lines in <base>/.openmpcpm-settings beside the theme preference; a missing file or key keeps the default (both on). */
static int cfg_get(const char *base, const char *key, int def) {
    char f[320], l[64];
    snprintf(f, sizeof f, "%s/.openmpcpm-settings", base);
    FILE *fp = fopen(f, "r");
    size_t kl = strlen(key);
    while (fp && fgets(l, sizeof l, fp)) if (!strncmp(l, key, kl) && l[kl] == '=') { fclose(fp); return l[kl + 1] == '1'; }
    if (fp) fclose(fp);
    return def;
}
static void cfg_save(const mgr_t *m) {   /* tmp, then rename */
    char f[320], t[330];
    snprintf(f, sizeof f, "%s/.openmpcpm-settings", m->dev->base);
    snprintf(t, sizeof t, "%s.new", f);
    FILE *fp = fopen(t, "w");
    if (!fp) return;
    fprintf(fp, "beta=%d\nweb=%d\n", m->show_beta, m->web_pref);
    fclose(fp);
    rename(t, f);
}
/* where the settings live before the first device probe (the web page starts with the plugin): the same two folders probe_device() looks in */
static void cfg_base_guess(char *out, size_t n) {
    static const char *dirs[] = {"/media/az01-internal/Settings", "/data/Settings"};
    out[0] = 0;
    for (int i = 0; i < 2 && !out[0]; i++) {
        DIR *sd = opendir(dirs[i]);
        struct dirent *e;
        while (sd && (e = readdir(sd)) && !out[0]) {
            char p[300];
            snprintf(p, sizeof p, "%s/%s/MPC.settings", dirs[i], e->d_name);
            if (e->d_name[0] != '.' && is_file(p)) snprintf(out, n, "%.*s", (int)(strstr(dirs[i], "/Settings") - dirs[i]), dirs[i]);
        }
        if (sd) closedir(sd);
    }
}

static int theme_dirty(const mgr_t *m) { return m->theme_can && m->theme != m->theme_applied; }

static void theme_save(const mgr_t *m) {   /* tmp, then rename */
    char f[320], t[330];
    snprintf(f, sizeof f, "%s/.openmpcpm-theme", m->dev->base);
    snprintf(t, sizeof t, "%s.new", f);
    FILE *fp = fopen(t, "w");
    if (!fp) return;
    fprintf(fp, "%s\n", m->theme ? "light" : "dark");
    fclose(fp);
    rename(t, f);
}

/* where a new install goes: the location picked on the restart prompt, else the first usable one */
/* the places a new install can go: "<drive>/VST2 Plugins" on 3.10.1 with VST2 Support on (every writable drive), else the usable Synths folders */
static int ntargets(const mgr_t *m) { return m->dev->vst2 == 2 && m->dev->nvdrv ? m->dev->nvdrv : m->dev->nusable; }
static const char *target_at(const mgr_t *m, int k) { return m->dev->vst2 == 2 && m->dev->nvdrv ? m->dev->vdrv[k] : m->dev->usable[k]; }
static const char *target_name(const mgr_t *m, int k) { return m->dev->vst2 == 2 && m->dev->nvdrv ? m->dev->vname[k] : m->dev->uname[k]; }
static const char *dest(const mgr_t *m) {
    return ntargets(m) ? target_at(m, m->tsel % ntargets(m)) : m->dev->target;
}


/* which of the install targets (ntargets/target_at) is this listed Synths folder? -1: none. With VST2 Support on (3.10.1) a target is
 * "<drive>/VST2 Plugins", so the folder matches by its drive. */
static int loc_target_index(const mgr_t *m, const char *path) {
    char p[220], drive[220];
    snprintf(p, sizeof p, "%s", path);
    for (size_t l = strlen(p); l > 1 && p[l - 1] == '/'; ) p[--l] = 0;
    snprintf(drive, sizeof drive, "%s", p);
    char *sl = strrchr(drive, '/');
    if (sl && sl != drive) *sl = 0;
    for (int k = 0; k < ntargets(m); k++) {
        const char *t = target_at(m, k);
        if (m->dev->vst2 == 2 && m->dev->nvdrv) {
            char td[220];
            snprintf(td, sizeof td, "%s", t);
            char *ts = strrchr(td, '/');
            if (ts && ts != td) *ts = 0;
            if (!strcmp(td, drive)) return k;
        } else if (!strcmp(t, p) || same_folder(t, p)) return k;
    }
    return -1;
}

/* The install location confirmed last time (a restart prompt, usually after CHANGE) is the default for new installs from now on: saved in
 * .openmpcpm-target beside the theme preference. It applies when that folder is a usable location now (a pulled SSD or card falls back to the
 * normal default, and the file is kept for when it is back). An update always stays where the plugin already is. */
static void target_init(mgr_t *m) {
    char f[320], b[260] = "";
    snprintf(f, sizeof f, "%s/.openmpcpm-target", m->dev->base);
    FILE *fp = fopen(f, "r");
    m->tsel = 0;
    if (!fp) return;
    if (fgets(b, sizeof b, fp)) b[strcspn(b, "\r\n")] = 0;
    fclose(fp);
    for (int k = 0; b[0] && k < ntargets(m); k++)
        if (!strcmp(target_at(m, k), b)) m->tsel = k;
}

static void target_save(const mgr_t *m) {   /* tmp, then rename */
    char f[320], t[330];
    snprintf(f, sizeof f, "%s/.openmpcpm-target", m->dev->base);
    snprintf(t, sizeof t, "%s.new", f);
    FILE *fp = fopen(t, "w");
    if (!fp) return;
    fprintf(fp, "%s\n", dest(m));
    fclose(fp);
    rename(t, f);
}

/* Can this folder hold a symbolic link? FAT, exFAT and NTFS cannot (Gen2's internal /nvme is exFAT). Asked of a package that needs
 * links, not of a drive in general: most plugins ship none. */
static int can_link(const char *dir) {
#ifdef FS_ANY
    if (getenv("PM_NOLINKS")) return 0;   /* tests: a container's overlay stores links, an exFAT drive does not */
#endif
    char l[320];
    snprintf(l, sizeof l, "%s/.pm-link-test", dir);
    unlink(l);
    int ok = symlink("x", l) == 0;
    unlink(l);
    return ok;
}

/* does the unpacked package's MODES file (install.sh restores these after a copy that loses them) list a symbolic link? ("l", tab, ...) */
static int needs_links(const char *pkgdir) {
    char f[600], line[1024];
    int n = 0;
    snprintf(f, sizeof f, "%s/MODES", pkgdir);
    FILE *fp = fopen(f, "r");
    while (fp && fgets(line, sizeof line, fp)) if (line[0] == 'l' && line[1] == '\t') { n = 1; break; }
    if (fp) fclose(fp);
    return n;
}

/* the Synths folder this plugin runs from: proof that MPC loads plugins (and their skins) from there */
static void own_synths(char *out, size_t n) {
    Dl_info di;
    out[0] = 0;
    if (!dladdr((void *)own_synths, &di) || !di.dli_fname || di.dli_fname[0] != '/') return;
    snprintf(out, n, "%s", di.dli_fname);
    for (int i = 0; i < 2; i++) { char *sl = strrchr(out, '/'); if (sl && sl != out) *sl = 0; }
}

static void probe_device(device_t *d) {
    memset(d, 0, sizeof *d);
    struct utsname u;
    snprintf(d->arch, sizeof d->arch, "%s", uname(&u) == 0 ? u.machine : "?");
    {   /* which MPC this is, for the catalog's per-device entries */
        char mb[96] = "";
        FILE *mf = fopen("/proc/device-tree/model", "r");
#ifdef FS_ANY
        if (getenv("PM_MODEL")) snprintf(mb, sizeof mb, "%s", getenv("PM_MODEL")); else
#endif
        if (mf && fgets(mb, sizeof mb, mf)) {}
        if (mf) fclose(mf);
        char *p = mb;
        if (!strncmp(p, "Akai Professional ", 18)) p += 18;
        size_t k = 0;
        for (; *p && *p != '\n' && k + 1 < sizeof d->model_id; p++) d->model_id[k++] = *p == ' ' ? '-' : (*p >= 'A' && *p <= 'Z') ? *p + 32 : *p;
        d->model_id[k] = 0;
    }
    int gen2 = !strcmp(d->arch, "aarch64");
    /* Gen2 (aarch64: Live III, confirmed; One G2/Key(s) G2 assumed to match): Settings lives under /data, not
     * /media/az01-internal (that path doesn't exist at all on a verity rootfs with no /sdcard). Same two-glob
     * fallback as mpc-store.sh. */
    static const char *settings_dirs[] = {"/media/az01-internal/Settings", "/data/Settings"};
    struct dirent *e;
    for (int si = 0; si < 2 && !d->settings[0]; si++) {
        DIR *sd = opendir(settings_dirs[si]);
        while (sd && (e = readdir(sd)) && !d->settings[0]) {
            char p[300];
            snprintf(p, sizeof p, "%s/%s/MPC.settings", settings_dirs[si], e->d_name);
            if (e->d_name[0] != '.' && is_file(p)) snprintf(d->settings, sizeof d->settings, "%s", p);
        }
        if (sd) closedir(sd);
    }
    char *x = d->settings[0] ? slurp(d->settings, 4 << 20) : NULL;
    if (x) {
        char *a = strstr(x, "<SynthContentLocations>"), *b = a ? strstr(a, "</SynthContentLocations>") : NULL;
        d->hasloclist = a && b;
        for (char *p = a; b && d->nloc < 8 && (p = strstr(p, "<Location>")) && p < b; ) {
            p += 10;
            char *q = strstr(p, "</Location>");
            if (!q || q > b) break;
            snprintf(d->loc[d->nloc++], sizeof d->loc[0], "%.*s", (int)(q - p), p);
        }
        for (char *p = x; (p = strstr(p, "<VALUE name=\"pluginList")); p++) {   /* which plugin lists exist (Gen2 may differ) */
            char *k = p + 13, *q = strchr(k, '"');   /* past '<VALUE name="' */
            if (q && strlen(d->keys) + (size_t)(q - k) + 2 < sizeof d->keys) strncat(d->keys, k, q - k), strcat(d->keys, " ");
        }
        /* Gen2 (aarch64) keeps its own plugin list, pluginList-arm-64bit, separate from pluginList-arm */
        a = strstr(x, gen2 ? "<VALUE name=\"pluginList-arm-64bit\">" : "<VALUE name=\"pluginList-arm\">");
        b = a ? strstr(a, "</VALUE>") : NULL;
        for (char *p = a; b && d->nent < 128 && (p = strstr(p, "<PLUGIN ")) && p < b; p++) {
            char *q = strstr(p, "/>");
            if (!q) break;
            entry_t *en = &d->ent[d->nent++];
            for (char *c = p; c < q; c++) if (*c == '\n' || *c == '\r' || *c == '\t') *c = ' ';
            attr(p, q, "name", en->name, sizeof en->name);
            attr(p, q, "uid", en->uid, sizeof en->uid);
            attr(p, q, "file", en->file, sizeof en->file);
        }
        d->vst2 = strstr(x, "Enhancements.VST2Support\" val=\"1\"") ? 2 : strstr(x, "Enhancements.VST2Support") ? 1 : 0;
        free(x);
    }
    /* internal storage first (One: /media/az01-internal, Force: /sdcard = /media/az01-internal-sd, Gen2: /nvme, One Gen2: /storage on a loop
     * image, told by the mount's device): a
     * plugin on a USB stick or card disappears with it; any other good location only as a last resort */
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < d->nloc; i++) {
            int internal = !strncmp(d->loc[i], "/media/az01-internal", 20) || !strncmp(d->loc[i], "/sdcard/", 8)
                            || !strncmp(d->loc[i], "/nvme/", 6) || internal_device(d->loc[i]);
            if (!(pass || internal) || !good_target(d->loc[i])) continue;
            char u[200];
            snprintf(u, sizeof u, "%s", d->loc[i]);
            for (size_t l = strlen(u); l > 1 && u[l - 1] == '/'; ) u[--l] = 0;   /* a trailing "/" is the same folder */
            /* the same folder by another path (a Force's /sdcard and /media/az01-internal-sd, a bind mount) counts once: compared by
             * device and inode, of the folder or, when it is not made yet, of its parent */
            int dup = 0;
            for (int k = 0; k < d->nusable; k++) if (!strcmp(d->usable[k], u) || same_folder(d->usable[k], u)) dup = 1;
            if (!dup && d->nusable < 8) {
                const char *lab = !strncmp(u, "/media/", 7) && !internal ? u + 7 : NULL;
                snprintf(d->uname[d->nusable], sizeof d->uname[0], "%.*s", lab ? (int)strcspn(lab, "/") : 8, lab ? lab : "Internal");
                snprintf(d->usable[d->nusable++], sizeof d->usable[0], "%s", u);
            }
        }
    /* drives MPC does not list: a mounted drive under /media/<label> with no entry for its Synths folder (LOCATIONS offers REGISTER) */
    {
#ifdef FS_ANY
        FILE *mf = fopen(!access("/tmp/pm-mounts", R_OK) ? "/tmp/pm-mounts" : "/proc/mounts", "r");
#else
        FILE *mf = fopen("/proc/mounts", "r");
#endif
        char mdev[128], mmnt[256], mtype[32], mopts[512];
        while (mf && fscanf(mf, "%127s %255s %31s %511s %*d %*d", mdev, mmnt, mtype, mopts) == 4 && d->ncand < 8) {
            mount_unescape(mmnt);
            if (strncmp(mmnt, "/media/", 7) || strchr(mmnt + 7, '/') || !mmnt[7] || !strncmp(mmnt + 7, "az01-internal", 13)) continue;
            char c[200];
            snprintf(c, sizeof c, "%s/Synths", mmnt);
            if (strlen(mmnt) > 180 || !safe(c) || strpbrk(c, "&<>\"|")) continue;
            int listed = 0;
            for (int k = 0; k < d->nloc; k++) if (!strcmp(d->loc[k], c) || same_folder(d->loc[k], c)) listed = 1;
            for (int k = 0; k < d->ncand; k++) if (!strcmp(d->cand[k], c)) listed = 1;
            if (!listed) snprintf(d->cand[d->ncand++], sizeof d->cand[0], "%s", c);
        }
        if (mf) fclose(mf);
    }
    if (d->nusable) snprintf(d->target, sizeof d->target, "%s", d->usable[0]);
    if (d->target[0]) snprintf(d->via, sizeof d->via, "SynthContentLocations");
    /* No fallback: MPC draws a plugin's skin only from a listed content location (docs/NOTES.md in mpc-vst-plugins, 2026-10-27),
     * so a folder that is not on the list would install a plugin that loads with no skin. With nothing usable listed the
     * manager says so (problem "No install location", every location and its reason in device.txt) and installs nothing. */
    for (size_t l = strlen(d->target); l > 1 && d->target[l - 1] == '/'; ) d->target[--l] = 0;
    /* MPC OS 3.10.1 lists only plugins in "<drive>/VST2 Plugins/<Mfr> - VST - <Name>/" with a plugin.json (MPC.settings entries are no longer
     * shown, though saved projects still load them; device-tested on a Live III, 2026-10-31). The folder goes on the target's drive. */
    if (d->vst2 && d->target[0]) {
        char drive[200];
        snprintf(drive, sizeof drive, "%s", d->target);
        char *sl = strrchr(drive, '/');
        if (sl && sl != drive) *sl = 0;
        snprintf(d->vst2dir, sizeof d->vst2dir, "%s/VST2 Plugins", drive);
        for (int k = 0; k < d->nusable; k++) {
            DIR *sd = opendir(d->usable[k]);
            while (sd && (e = readdir(sd))) {
                char pj[520], pd[460];
                if (e->d_name[0] == '.' || !strstr(e->d_name, " - VST - ")) continue;
                snprintf(pd, sizeof pd, "%s/%s", d->usable[k], e->d_name);
                snprintf(pj, sizeof pj, "%s/%s/plugin.json", d->vst2dir, e->d_name);
                if (!is_dir(pd) || is_file(pj)) continue;
                DIR *pdd = opendir(pd);   /* a folder with a .so: a plugin, not just its user files */
                struct dirent *f;
                int so = 0;
                while (pdd && !so && (f = readdir(pdd))) { size_t l = strlen(f->d_name); so = l > 3 && !strcmp(f->d_name + l - 3, ".so"); }
                if (pdd) closedir(pdd);
                d->nmove += so;
            }
            if (sd) closedir(sd);
        }
    }
    /* 3.10.1 scans "VST2 Plugins" at the root of every drive and draws the skin from the plugin's own folder, so (unlike Synths) any writable
     * drive works: the internal one, then each /media/<label> mount (an SD card, a USB drive). Akai's own mounts are left out. A noexec drive
     * is offered: MPC remounts it with exec for VST2 plugins (firmware strings, 2026-10-31; not yet seen on a device). */
    if (d->vst2 == 2 && d->vst2dir[0]) {
        snprintf(d->vdrv[0], sizeof d->vdrv[0], "%s", d->vst2dir);
        snprintf(d->vname[0], sizeof d->vname[0], "Internal");
        d->nvdrv = 1;
#ifdef FS_ANY
        FILE *mf = fopen(!access("/tmp/pm-mounts", R_OK) ? "/tmp/pm-mounts" : "/proc/mounts", "r");
#else
        FILE *mf = fopen("/proc/mounts", "r");
#endif
        char dv[128], mnt[256], ty[32], op[512];
        while (mf && d->nvdrv < 4 && fscanf(mf, "%127s %255s %31s %511s %*d %*d", dv, mnt, ty, op) == 4) {
            mount_unescape(mnt);
            const char *lab = mnt + 7;
            if (strncmp(mnt, "/media/", 7) || !*lab || strchr(lab, '/') || !strncmp(lab, "az01-internal", 13) || !strncmp(lab, "acvs-", 5)) continue;
            if (strncmp(op, "rw", 2) || (op[2] && op[2] != ',') || !strcmp(ty, "tmpfs") || !strcmp(ty, "ramfs") || !strcmp(ty, "squashfs")) continue;
            if (!safe(mnt) || strstr(d->vst2dir, mnt) == d->vst2dir || !is_dir(mnt)) continue;   /* the internal drive is already first */
            snprintf(d->vdrv[d->nvdrv], sizeof d->vdrv[0], "%s/VST2 Plugins", mnt);
            snprintf(d->vname[d->nvdrv], sizeof d->vname[0], "%.20s", lab);
            d->nvdrv++;
        }
        if (mf) fclose(mf);
    }
    /* the manager's own record of versions it installed, next to the Settings folder */
    snprintf(d->base, sizeof d->base, "%s", d->settings);
    char *cut = strstr(d->base, "/Settings/");
    if (cut) *cut = 0; else snprintf(d->base, sizeof d->base, "/media/az01-internal");
    snprintf(d->state, sizeof d->state, "%s/.openmpcpm-installed", d->base);
    d->systemd_run = on_path("systemd-run");
    d->cgroup2 = is_file("/sys/fs/cgroup/cgroup.procs");
    /* addins: one folder per id under ADDINS_DIR, each with the addin.manifest the installer wrote (ADDIN_ID,
     * ADDIN_VERSION, plain shell assignments) and a copy of its uninstall.sh; a folder without one was made by hand */
    char parent[64];
    snprintf(parent, sizeof parent, "%s", ADDINS_DIR);
    char *ps = strrchr(parent, '/');
    if (ps && ps != parent) *ps = 0;
    if (is_dir(parent) && access(parent, W_OK) == 0) snprintf(d->addins, sizeof d->addins, "%s", ADDINS_DIR);
    DIR *ad = d->addins[0] ? opendir(d->addins) : NULL;
    while (ad && (e = readdir(ad)) && d->naddin < 32) {
        addin_t *a = &d->addin[d->naddin];
        char mp[300];
        if (e->d_name[0] == '.' || !safe(e->d_name)) continue;
        snprintf(a->folder, sizeof a->folder, "%s/%s", d->addins, e->d_name);
        snprintf(mp, sizeof mp, "%s/addin.manifest", a->folder);
        char *mf = slurp(mp, 64 << 10);
        if (!mf) continue;
        a->id[0] = a->version[0] = 0;
        for (char *l = mf; l; ) {
            char *nl = strchr(l, '\n'), *val = NULL, *out = NULL;
            size_t n = 0;
            if (!strncmp(l, "ADDIN_ID=", 9)) val = l + 9, out = a->id, n = sizeof a->id;
            else if (!strncmp(l, "ADDIN_VERSION=", 14)) val = l + 14, out = a->version, n = sizeof a->version;
            if (val) {
                size_t len = strcspn(val, "\r\n");
                if (len >= 2 && (*val == '"' || *val == '\'')) val++, len -= 2;   /* the installer quotes values */
                snprintf(out, n, "%.*s", (int)len, val);
            }
            l = nl ? nl + 1 : NULL;
        }
        snprintf(mp, sizeof mp, "%s/uninstall.sh", a->folder);
        a->removable = is_file(mp);
        free(mf);
        if (a->id[0] && safe(a->id)) d->naddin++;
    }
    if (ad) closedir(ad);
    const char *pb = !d->settings[0] ? "No MPC.settings"
                   : !d->target[0] ? "No install location"
                   : !on_path("systemctl") ? "No systemctl"
                   : !d->systemd_run && !d->cgroup2 ? "No systemd-run"
                   : !on_path("unzip") ? "No unzip"
                   : !on_path("sha256sum") ? "No sha256sum"
                   : !curl_load() ? "No libcurl" : "";
    snprintf(d->problem, sizeof d->problem, "%s", pb);
}

/* The device's glibc ("2.39"), asked of the running system; tests: PM_GLIBC. MPC OS 2.x ships 2.32, 3.x 2.39 (docs/NOTES.md), so
 * a glibc below 2.39 reads as OS 2.x: an inference, said as such in the message. */
static const char *dev_glibc(void) {
    const char *g = getenv("PM_GLIBC");
    return g ? g : gnu_get_libc_version();
}
static int glibc_less(const char *a, const char *b) {   /* a < b, as "major.minor" */
    int a1 = 0, a2 = 0, b1 = 0, b2 = 0;
    sscanf(a, "%d.%d", &a1, &a2);
    sscanf(b, "%d.%d", &b1, &b2);
    return a1 != b1 ? a1 < b1 : a2 < b2;
}
/* MPC caches every skin image it draws in /var/volatile/tmp/filmstrips, a RAM disk that only a reboot empties. Once it is full, new images fail to
 * draw (black patches: Live III, 2026-10-28, 3497 files, 4 GB, 100%). Returns the percent used, or -1 when there is no such volume. */
static int cache_used_pct(void) {
    struct statvfs sv;
    const char *dir = "/var/volatile";
#ifdef FS_ANY
    if (getenv("PM_CACHE_PCT")) return atoi(getenv("PM_CACHE_PCT"));   /* tests: a full RAM disk cannot be made in a container */
#endif
    if (statvfs(dir, &sv) || !sv.f_blocks) return -1;
    return (int)(100.0 * (1.0 - (double)sv.f_bavail / (double)sv.f_blocks) + 0.5);
}

static void write_report(const device_t *d) {
    FILE *f = fopen(WORK "/device.txt", "w");
    if (!f) return;
    struct utsname u;
    if (uname(&u) == 0) fprintf(f, "uname: %s %s %s\n", u.machine, u.release, u.version);
    char *os = slurp("/etc/os-release", 4096);
    if (os) { char *v = strstr(os, "VERSION="); if (v) fprintf(f, "os: %.*s\n", (int)strcspn(v, "\n"), v); free(os); }
    { int cp = cache_used_pct(); if (cp >= 0) fprintf(f, "image cache: /var/volatile %d%% used (85%% or more: skins draw black patches)\n", cp); }
    fprintf(f, "glibc: %s (reads as MPC OS %s)\n", dev_glibc(), glibc_less(dev_glibc(), "2.39") ? "2.x" : "3.x");
    fprintf(f, "settings: %s\nplugin lists: %s\nstate: %s\n", d->settings[0] ? d->settings : "(not found)", d->keys, d->state);
    char why[320], own[200];
    for (int i = 0; i < d->nloc; i++) {
        const char *bad = bad_target(d->loc[i], why, sizeof why);
        fprintf(f, "location: %s  (%s)\n", d->loc[i], bad ? bad : "usable");
    }
    for (int k = 0; k < d->nusable; k++) fprintf(f, "usable: %s\n", d->usable[k]);
    own_synths(own, sizeof own);
    fprintf(f, "manager folder: %s\ninstall target: %s%s%s%s\n", own[0] ? own : "(unknown)", d->target[0] ? d->target : "(none)",
            d->via[0] ? "  (from " : "", d->via, d->via[0] ? ")" : "");
    fprintf(f, "vst2: %s%s%s\n", d->vst2 == 2 ? "on" : d->vst2 == 1 ? "off" : "none (MPC OS before 3.10.1)", d->vst2dir[0] ? "  " : "", d->vst2dir);
    if (d->vst2) fprintf(f, "vst2 to move: %d\n", d->nmove);
    for (int i = 0; i < d->nvdrv; i++) fprintf(f, "vst2 drive: %s  (%s)\n", d->vdrv[i], d->vname[i]);
    fprintf(f, "addins: %s\n", d->addins[0] ? d->addins : "(" ADDINS_DIR " not usable)");
    for (int i = 0; i < d->naddin; i++)
        fprintf(f, "addin: %s %s %s%s\n", d->addin[i].id, d->addin[i].version, d->addin[i].folder, d->addin[i].removable ? "" : "  (no uninstall.sh)");
    fprintf(f, "tools: systemctl=%d systemd-run=%d cgroup2=%d unzip=%d sha256sum=%d libcurl=%d ca-bundle=%d\n",
            on_path("systemctl"), d->systemd_run, d->cgroup2, on_path("unzip"), on_path("sha256sum"), curl_load(),
            is_file("/etc/ssl/certs/ca-certificates.crt"));
    char *env = getenv("LD_PRELOAD");
    fprintf(f, "MPC LD_PRELOAD: %s\n", env ? env : "(none)");
    for (int i = 0; i < d->nent; i++)
        fprintf(f, "plugin: %s uid=%s file=%s%s\n", d->ent[i].name, d->ent[i].uid, d->ent[i].file, is_file(d->ent[i].file) ? "" : "  (MISSING)");
    fprintf(f, "problem: %s\n", d->problem[0] ? d->problem : "none");
    fclose(f);
}

typedef struct { char id[48], version[24]; } manifest_t;
static const char *manifest_member(const char *k, const char *v, void *ud) {
    manifest_t *x = ud;
    if (*v == '"' && !strcmp(k, "id")) return jstr(v, x->id, sizeof x->id);
    if (*v == '"' && !strcmp(k, "version")) return jstr(v, x->version, sizeof x->version);
    return NULL;
}

/* A folder in a content location with a plugin-meta.xml whose file= exists but that MPC's list has no entry for (by file or uid): copied by hand,
 * or dropped from the list while a card was out. The card offers REGISTER: the entry is added from plugin-meta.xml by the installer's own awk
 * (apply.sh, MPC stopped, settings backed up). %payload-path% in the entry stands for the content location. */
static const char *xml_attr(const char *s, const char *attr, char *out, size_t n) {
    char pat[40];
    snprintf(pat, sizeof pat, " %s=\"", attr);
    const char *p = strstr(s, pat);
    out[0] = 0;
    if (!p) return NULL;
    p += strlen(pat);
    const char *e = strchr(p, '"');
    if (!e || (size_t)(e - p) >= n) return NULL;
    memcpy(out, p, e - p);
    out[e - p] = 0;
    return out;
}

static void detect_orphans(device_t *d) {
    d->norphan = 0;
    for (int u = 0; u < d->nusable; u++) {
        DIR *dp = opendir(d->usable[u]);
        struct dirent *e;
        while (dp && (e = readdir(dp)) && d->norphan < 8) {
            char meta[640], el[1500], file[320], uid[16], *s, *a, *b;
            if (e->d_name[0] == '.' || !safe(e->d_name)) continue;
            snprintf(meta, sizeof meta, "%s/%s/plugin-meta.xml", d->usable[u], e->d_name);
            if (!(s = slurp(meta, 16 << 10))) continue;
            if (!(a = strstr(s, "<PLUGIN")) || !(b = strstr(a, "/>"))) { free(s); continue; }
            snprintf(el, sizeof el, "%.*s", (int)(b + 2 - a), a);
            free(s);
            for (char *q = el; *q; q++) if (*q == '\n' || *q == '\r' || *q == '\t') *q = ' ';
            char *pp = strstr(el, "%payload-path%");   /* the content location the folder is in */
            if (pp) {
                char rest[1500];
                snprintf(rest, sizeof rest, "%s", pp + strlen("%payload-path%"));
                snprintf(pp, sizeof el - (pp - el), "%s%s", d->usable[u], rest);
            }
            if (!xml_attr(el, "file", file, sizeof file) || !xml_attr(el, "uid", uid, sizeof uid) || !is_file(file) || !safe(file)) continue;
            int listed = 0;
            for (int k = 0; k < d->nent && !listed; k++) listed = !strcmp(d->ent[k].file, file) || !strcasecmp(d->ent[k].uid, uid);
            if (listed) continue;
            int o = d->norphan++;
            snprintf(d->orphan[o].folder, sizeof d->orphan[o].folder, "%s/%s", d->usable[u], e->d_name);
            snprintf(d->orphan[o].file, sizeof d->orphan[o].file, "%s", file);
            snprintf(d->orphan[o].uid, sizeof d->orphan[o].uid, "%s", uid);
            snprintf(d->orphan[o].entry, sizeof d->orphan[o].entry, "%s", el);
            char nm[64], ins[8];
            snprintf(d->orphan[o].name, sizeof d->orphan[o].name, "%s", xml_attr(el, "name", nm, sizeof nm) ? nm : e->d_name);
            d->orphan[o].inst = xml_attr(el, "isInstrument", ins, sizeof ins) && ins[0] == '1';
        }
        if (dp) closedir(dp);
    }
}

static int add_orphans(const device_t *d, pkg_t *pkg, int n) {
    for (int o = 0; o < d->norphan && n < MAXPKG; o++) {
        pkg_t *p = &pkg[n++];
        memset(p, 0, sizeof *p);
        const char *base = strrchr(d->orphan[o].folder, '/') + 1;
        snprintf(p->id, sizeof p->id, "orphan-%s", base);
        for (char *q = p->id; *q; q++) if (!isalnum((unsigned char)*q) && *q != '-' && *q != '.') *q = '-';
        snprintf(p->name, sizeof p->name, "%s", d->orphan[o].name);
        snprintf(p->author, sizeof p->author, "not in MPC's list");
        snprintf(p->kind, sizeof p->kind, "%s", d->orphan[o].inst ? "instrument" : "effect");
        snprintf(p->dist, sizeof p->dist, "release");
        snprintf(p->url, sizeof p->url, "orphan:");
        snprintf(p->channel, sizeof p->channel, "stable");
        snprintf(p->tags, sizeof p->tags, "orphan");
        snprintf(p->installed, sizeof p->installed, "?");
        snprintf(p->synths, sizeof p->synths, "%s", d->orphan[o].folder);
        snprintf(p->summary, sizeof p->summary, "This folder is in a content location but MPC's plugin list has no entry for it, so MPC does not show the plugin. REGISTER adds the entry from its plugin-meta.xml (MPC restarts; MPC.settings is backed up first).");
        snprintf(p->updated, sizeof p->updated, "0000-00-00");
        p->orphan = o + 1;
    }
    return n;
}

/* Zips the user put on a drive: <a content location's parent>/local-plugins/<name>.zip become cards ("local zip"), installed by the same steps as a
 * catalog package (zip inspection, arch/glibc check, rollback). Nothing vouches for them: the card shows the zip's own sha256, and the
 * catalog's checksum rule does not apply. Returns the new package count. */
static int add_local(const device_t *d, pkg_t *pkg, int n) {
    for (int u = 0; u < d->nusable; u++) {
        char dir[300], *sl;
        snprintf(dir, sizeof dir, "%s", d->usable[u]);
        if (!(sl = strrchr(dir, '/'))) continue;
        snprintf(sl, sizeof dir - (sl - dir), "/local-plugins");
        DIR *dp = opendir(dir);
        struct dirent *e;
        while (dp && (e = readdir(dp)) && n < MAXPKG) {
            size_t l = strlen(e->d_name);
            char path[600], cmd[900];
            if (l < 5 || strcasecmp(e->d_name + l - 4, ".zip") || e->d_name[0] == '.') continue;
            snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
            if (!safe(path) || !is_file(path)) continue;
            snprintf(cmd, sizeof cmd, "unzip -p '%s' '*/mpc-plugin.json' > '" WORK "/local.json' 2>/dev/null; "
                                      "sha256sum '%s' | cut -c1-64 > '" WORK "/local.sha'", path, path);
            if (sh(cmd)) continue;
            manifest_t mf = {{0}, {0}};
            char *js = slurp(WORK "/local.json", 64 << 10), *sha = slurp(WORK "/local.sha", 80);
            if (js) { jobject(js, manifest_member, &mf); free(js); }
            if (!sha || strlen(sha) < 64) { free(sha); continue; }
            sha[64] = 0;
            pkg_t *p = &pkg[n];
            memset(p, 0, sizeof *p);
            char stem[48];
            snprintf(stem, sizeof stem, "%.*s", (int)(l - 4 > 40 ? 40 : l - 4), e->d_name);
            for (char *q = stem; *q; q++) if (!isalnum((unsigned char)*q) && *q != '-' && *q != '.') *q = '-';
            snprintf(p->id, sizeof p->id, "local-%s", mf.id[0] ? mf.id : stem);
            int dup = 0;
            for (int i = 0; i < n; i++) dup |= !strcmp(pkg[i].id, p->id);
            if (dup || !safe(p->id)) { free(sha); continue; }
            snprintf(p->name, sizeof p->name, "%.*s", (int)(l - 4 > 44 ? 44 : l - 4), e->d_name);
            snprintf(p->author, sizeof p->author, "local zip");
            snprintf(p->kind, sizeof p->kind, "instrument");
            snprintf(p->dist, sizeof p->dist, "release");
            snprintf(p->latest, sizeof p->latest, "%s", mf.version[0] ? mf.version : "local");
            snprintf(p->url, sizeof p->url, "file://%s", path);
            snprintf(p->sha, sizeof p->sha, "%s", sha);
            snprintf(p->channel, sizeof p->channel, "stable");
            snprintf(p->tags, sizeof p->tags, "local");
            snprintf(p->summary, sizeof p->summary, "A zip you put in %s. Nothing checked it against the catalog: its own sha256 is %.16s...", dir, sha);
            struct stat st;
            if (!stat(path, &st)) p->size = st.st_size;
            snprintf(p->updated, sizeof p->updated, "9999-99-99");   /* newest first in the default sort */
            free(sha);
            n++;
        }
        if (dp) closedir(dp);
    }
    return n;
}

/* Match each catalog plugin to a plugin-list entry: by the mpc-plugin.json id in its folder, else a component uid,
 * else the name. The Synths folder is the entry's file= minus "<vendor> - VST - <name>/<so>"; an entry whose .so is
 * not in such a folder is an old-layout install (synths left empty, shown as "old"). An addin is matched by id to
 * the folders under ADDINS_DIR; one without uninstall.sh is shown as "old" too (it can be updated, not removed). */
/* plugin-history.json (schema 1): what was installed or removed, and when, kept beside the state file so it outlives a lost state file.
 * One entry per line, written atomically (temp file, fsync, rename); a file that is not ours is left alone. */
typedef struct { char id[48], ver[24]; int inst; long t; } hist_t;
#define HIST_MAX 512
static void hist_path(const device_t *d, char *out, size_t n) { snprintf(out, n, "%s/.openmpcpm-history.json", d->base); }

static int hist_load(const device_t *d, hist_t *h, int *bad) {
    char p[300], *s;
    int n = 0;
    *bad = 0;
    hist_path(d, p, sizeof p);
    if (!(s = slurp(p, 256 << 10))) return 0;
    if (strncmp(s, "{\"schema\": 1,", 13)) { *bad = 1; free(s); return 0; }
    char *sv = NULL;   /* strtok_r: MPC runs more than one instance (each with a worker); strtok's hidden state is shared */
    for (char *l = strtok_r(s, "\n", &sv); l && n < HIST_MAX; l = strtok_r(NULL, "\n", &sv)) {
        hist_t x = {{0}, {0}, 0, 0};
        char inst[8] = "";
        if (sscanf(l, "{\"id\": \"%47[^\"]\", \"version\": \"%23[^\"]\", \"installed\": %7[a-z], \"time\": %ld", x.id, x.ver, inst, &x.t) < 4) continue;
        x.inst = !strcmp(inst, "true");
        h[n++] = x;
    }
    free(s);
    return n;
}

static void hist_record(const device_t *d, const char *id, const char *act, const char *ver) {
    hist_t *h = calloc(HIST_MAX + 1, sizeof *h);
    int bad, n;
    if (!h) return;
    n = hist_load(d, h, &bad);
    if (bad) { logf_("history: not our file, left alone"); free(h); return; }
    int k = 0;
    while (k < n && strcmp(h[k].id, id)) k++;
    if (k == n && n < HIST_MAX) n++;
    if (k < n) {
        snprintf(h[k].id, sizeof h[k].id, "%s", id);
        if (!strcmp(act, "install") || !strcmp(act, "register")) { snprintf(h[k].ver, sizeof h[k].ver, "%s", ver); h[k].inst = 1; }
        else h[k].inst = 0;
        h[k].t = (long)time(NULL);
    }
    char p[300], t[310];
    hist_path(d, p, sizeof p);
    snprintf(t, sizeof t, "%s.new", p);
    FILE *f = fopen(t, "w");
    if (f) {
        fprintf(f, "{\"schema\": 1, \"plugins\": [\n");
        for (int i = 0; i < n; i++)
            fprintf(f, "{\"id\": \"%s\", \"version\": \"%s\", \"installed\": %s, \"time\": %ld}%s\n", h[i].id, h[i].ver,
                    h[i].inst ? "true" : "false", h[i].t, i + 1 < n ? "," : "");
        fprintf(f, "]}\n");
        fflush(f);
        fsync(fileno(f));
        fclose(f);
        rename(t, p);
    }
    free(h);
}

static void scan_installed(const device_t *d, pkg_t *pkg, int n) {
    for (int i = 0; i < n; i++) pkg[i].installed[0] = pkg[i].synths[0] = pkg[i].folder[0] = 0, pkg[i].legacy = 0, pkg[i].ufiles = 0;
    for (int k = 0; k < d->nent; k++) {
        const entry_t *en = &d->ent[k];
        char folder[256], mpath[300];
        snprintf(folder, sizeof folder, "%s", en->file);
        char *sl = strrchr(folder, '/');
        if (!sl) continue;
        *sl = 0;
        manifest_t mf = {{0}, {0}};
        snprintf(mpath, sizeof mpath, "%s/mpc-plugin.json", folder);
        char *js = slurp(mpath, 64 << 10);
        int uf = 0;
        if (js) {
            const char *u = strstr(js, "\"user_files\"");
            if (u && (u = strchr(u, '['))) { u++; while (isspace((unsigned char)*u)) u++; uf = *u == '{'; }
            jobject(js, manifest_member, &mf);
            free(js);
        }
        for (int i = 0; i < n; i++) {
            pkg_t *p = &pkg[i];
            if (is_addin(p)) continue;
            /* a local zip's card is "local-<id>": it is the same plugin as the installed <id> (else it would offer Install forever, never Remove) */
            const char *pid = !strncmp(p->id, "local-", 6) ? p->id + 6 : p->id;
            int hit = mf.id[0] ? !strcmp(mf.id, pid) : !strcmp(en->name, p->name);
            for (int c = 0; c < 4 && !hit; c++) hit = p->uids[c][0] && !strcasecmp(p->uids[c], en->uid);
            if (!hit || p->installed[0]) continue;
            snprintf(p->installed, sizeof p->installed, "%s", mf.version[0] ? mf.version : "?");
            snprintf(p->folder, sizeof p->folder, "%s", folder);
            p->ufiles = uf;
            const char *base = strrchr(folder, '/');
            if (base && strstr(base, " - VST - ")) snprintf(p->synths, sizeof p->synths, "%.*s", (int)(base - folder), folder);
            else p->legacy = 1;
        }
    }
    for (int i = 0; i < n; i++) {
        pkg_t *p = &pkg[i];
        if (!is_addin(p)) continue;
        for (int k = 0; k < d->naddin; k++)
            if (!strcmp(d->addin[k].id, p->id) && !p->installed[0]) {
                snprintf(p->installed, sizeof p->installed, "%s", d->addin[k].version[0] ? d->addin[k].version : "?");
                snprintf(p->synths, sizeof p->synths, "%s", d->addin[k].folder);
                p->legacy = !d->addin[k].removable;
            }
    }
    FILE *f = fopen(d->state, "r");
    char id[64], ver[32];
    while (f && fscanf(f, "%63s %31s", id, ver) == 2)
        for (int i = 0; i < n; i++)
            if (!strcmp(pkg[i].id, id) && !strcmp(pkg[i].installed, "?")) snprintf(pkg[i].installed, sizeof pkg[i].installed, "%s", ver);
    if (f) fclose(f);
    hist_t *h = calloc(HIST_MAX + 1, sizeof *h);   /* a version the state file lacks: the history's */
    int bad, hn = h ? hist_load(d, h, &bad) : 0;
    for (int i = 0; i < n; i++)
        for (int k = 0; k < hn; k++)
            if (h[k].inst && h[k].ver[0] && !strcmp(pkg[i].id, h[k].id) && !strcmp(pkg[i].installed, "?"))
                snprintf(pkg[i].installed, sizeof pkg[i].installed, "%s", h[k].ver);
    free(h);
}

/* ---- jobs ---- */

/* MPC.settings.bak-* piles up (every installer makes one): keep the newest BAK_KEEP, delete the rest. Only regular files with that exact
 * name prefix, only in the settings folder, only after a successful apply with MPC running (never while it is being stopped). */
#define BAK_KEEP 10
static int prune_backups(const device_t *d) {
    if (!d->settings[0]) return 0;
    char dir[300];
    snprintf(dir, sizeof dir, "%s", d->settings);
    char *sl = strrchr(dir, '/');
    if (!sl) return 0;
    *sl = 0;
    const char *pre = strrchr(d->settings, '/') + 1;
    char want[80];
    snprintf(want, sizeof want, "%s.bak-", pre);
    struct { char name[260]; time_t t; } *b = calloc(512, sizeof *b);
    int n = 0, gone = 0;
    DIR *dp = b ? opendir(dir) : NULL;
    struct dirent *e;
    while (dp && (e = readdir(dp)) && n < 512) {
        char f[600];
        struct stat st;
        snprintf(f, sizeof f, "%s/%s", dir, e->d_name);
        if (strncmp(e->d_name, want, strlen(want)) || stat(f, &st) || !S_ISREG(st.st_mode)) continue;
        snprintf(b[n].name, sizeof b[n].name, "%s", e->d_name);
        b[n++].t = st.st_mtime;
    }
    if (dp) closedir(dp);
    for (int i = 1; i < n; i++)   /* newest first */
        for (int j = i; j > 0 && b[j].t > b[j - 1].t; j--) { typeof(*b) x = b[j]; b[j] = b[j - 1]; b[j - 1] = x; }
    for (int i = BAK_KEEP; i < n; i++) {
        char f[600];
        snprintf(f, sizeof f, "%s/%s", dir, b[i].name);
        if (!unlink(f)) gone++;
    }
    free(b);
    if (gone) logf_("pruned %d old settings backups (kept the newest %d)", gone, BAK_KEEP);
    return gone;
}

/* The result of the last apply (apply.sh's per-item lines "id|ok|" or "id|fail|reason"), shown once after MPC is back. */
static void show_result(mgr_t *m, const pkg_t *pkg, int n) {
    /* MPC creates the plugin more than once (it opens it twice), and each instance refreshes: claim the file by renaming it, so exactly
     * one instance reads and reports it (two parsing it at once with strtok showed "2 failed" and garbage on a Live III, 2026-10-31) */
    char mine[96];
    snprintf(mine, sizeof mine, WORK "/result.%ld.%p", (long)getpid(), (void *)m);
    if (rename(WORK "/result", mine)) return;
    char *s = slurp(mine, 8192);
    if (!s) { unlink(mine); return; }
    int ok = 0, bad = 0;
    char first[300] = "";
    char *sv = NULL;
    for (char *l = strtok_r(s, "\n", &sv); l; l = strtok_r(NULL, "\n", &sv)) {
        char *a = strchr(l, '|'), *b = a ? strchr(a + 1, '|') : NULL;
        if (!b) continue;
        *a = *b = 0;
        if (!strcmp(a + 1, "ok")) {
            ok++;
            char act[12] = "", ver[24] = "";
            if (sscanf(b + 1, "%11s %23s", act, ver) >= 1 && strncmp(l, "loc-", 4)) hist_record(m->dev, l, act, ver);
        }
        else {
            if (!bad++) {
                const char *name = l;
                for (int i = 0; i < n; i++) if (!strcmp(pkg[i].id, l)) name = pkg[i].name;
                snprintf(first, sizeof first, "%s: %s", name, b[1] ? b + 1 : "failed");
            }
        }
    }
    free(s);
    rename(mine, WORK "/result.seen");
    if (!ok && !bad) return;
    if (ok) prune_backups(m->dev);
    if (bad) say_locked(m, ERR, "Last apply: %d done, %d failed and put back as they were. %s", ok, bad, first);
    else say_locked(m, OK, "Last apply: %d done.", ok);
}

static void do_refresh(mgr_t *m) {
    say(m, OK, "Loading the plugin catalog\xe2\x80\xa6");
    sink_t s = {0};
    int got = fetch(m, CATALOG_URL, &s) == 0;
    pkg_t *tmp = calloc(MAXPKG, sizeof *tmp);
    list_t l = {tmp, 0};
    int parsed = tmp && got && jobject(s.buf, top_member, &l);
    if (tmp && !parsed && CATALOG_URL_FALLBACK[0]) {
        logf_("catalog: %s %s, trying %s", CATALOG_URL, got ? "unreadable" : "unreachable", CATALOG_URL_FALLBACK);
        free(s.buf);
        memset(&s, 0, sizeof s);
        l.n = 0;
        memset(tmp, 0, MAXPKG * sizeof *tmp);
        got = fetch(m, CATALOG_URL_FALLBACK, &s) == 0;
        parsed = got && jobject(s.buf, top_member, &l);
    }
    int offline = 0;   /* 1: the catalog could not be reached, 2: it could not be read */
    if (!tmp) {
        free(s.buf);
        pthread_mutex_lock(&m->mu);
        m->online = 0;
        say_locked(m, ERR, "Out of memory");
        pthread_mutex_unlock(&m->mu);
        return;
    }
    if (!parsed) {
        /* No catalog: carry on with what is on the MPC (zips in local-plugins, installed plugins, folders to register), so a zip can be
         * installed with no network. Nothing from a half-read catalog is kept. */
        offline = got ? 2 : 1;
        l.n = 0;
        memset(tmp, 0, MAXPKG * sizeof *tmp);
    }
    free(s.buf);
    probe_device(m->dev);
    l.n = add_local(m->dev, tmp, l.n);
    theme_init(m);
    m->show_beta = cfg_get(m->dev->base, "beta", 1);
    m->web_pref = cfg_get(m->dev->base, "web", 1);
    if (!m->confirm) target_init(m);   /* a pick still open on the prompt is kept */
    write_report(m->dev);
    loc_build(m);
    patches_load(m);
    scan_installed(m->dev, tmp, l.n);
    detect_orphans(m->dev);
    l.n = add_orphans(m->dev, tmp, l.n);
    struct statvfs sv;   /* the header's storage readout: the drive plugins go to */
    char disk[220];
    snprintf(disk, sizeof disk, "%s", dest(m)[0] ? dest(m) : "/media/az01-internal");
    if (!is_dir(disk) && strrchr(disk, '/') && strrchr(disk, '/') != disk) *strrchr(disk, '/') = 0;   /* "VST2 Plugins" not made yet: its drive */
    float used = 0;
    char freebuf[32] = "";
    if (statvfs(disk, &sv) == 0 && sv.f_blocks) {
        double fr = (double)sv.f_bavail * sv.f_frsize;
        used = 1.0f - (float)sv.f_bavail / (float)sv.f_blocks;
        if (fr >= 1e9) snprintf(freebuf, sizeof freebuf, "%.1f GB free", fr / 1e9);
        else snprintf(freebuf, sizeof freebuf, "%.0f MB free", fr / 1e6);
    }
    pthread_mutex_lock(&m->mu);
    char sel[48] = "";
    if (m->sel >= 0 && m->sel < m->npkg) snprintf(sel, sizeof sel, "%s", m->pkg[m->sel].id);
    for (int i = 0; i < l.n; i++)   /* keep what was queued and selected */
        for (int j = 0; j < m->npkg; j++)
            if (!strcmp(tmp[i].id, m->pkg[j].id)) tmp[i].queued = m->pkg[j].queued;
    memcpy(m->pkg, tmp, sizeof(pkg_t) * l.n);
    m->npkg = l.n;
    m->sel = m->menu = -1;
    for (int i = 0; i < l.n; i++) if (!strcmp(tmp[i].id, sel)) m->sel = i;
    m->online = !offline;
    m->loaded = 1;
    m->disk_used = used;
    snprintf(m->disk_free, sizeof m->disk_free, "%s", freebuf);
    if (m->dev->problem[0]) {
        char w[160];
        problem_words(m->dev, w, sizeof w);
        say_locked(m, ERR, "Installing is turned off: %s (reasons in " WORK "/device.txt)", w);
    }
    else if (offline) {
        int local = 0;
        for (int i = 0; i < m->npkg; i++) local += !strncmp(m->pkg[i].id, "local-", 6);
        say_locked(m, WARN, "%s: showing the zips in local-plugins (%d) and what is installed. Check the MPC's network to browse the catalog.",
                   offline == 2 ? "The plugin catalog could not be read" : "Can't reach the plugin catalog", local);
    }
    else { m->sticky = 0; m->rev++; }
    if (m->dev->vst2 == 2 && m->dev->nmove && !m->migrate_seen) m->migrate = m->migrate_seen = 1;
    if (!(m->dev->vst2 == 2 && m->dev->nmove)) m->migrate = 0;
    if (!m->dev->problem[0] && m->dev->vst2 == 1)
        say_locked(m, WARN, "MPC OS 3.10.1: turn on VST2 Support (Preferences > Enhancements) and restart MPC; until then it lists no plugins");
    else if (!m->dev->problem[0] && m->migrate)
        say_locked(m, WARN, "MPC OS 3.10.1 lists only plugins in VST2 Plugins: APPLY copies your %d installed plugin%s there (MPC restarts)",
                   m->dev->nmove, m->dev->nmove == 1 ? "" : "s");
    show_result(m, m->pkg, m->npkg);
    int cp = cache_used_pct();
    if (cp >= 85 && !m->dev->problem[0] && m->status_kind != ERR)
        say_locked(m, WARN, "MPC's image cache is %d%% full: restart the MPC (or reboot) before parts of this screen draw black", cp);
    pthread_mutex_unlock(&m->mu);
    free(tmp);
}

/* a string member of the package's mpc-plugin.json ("arch", "folder"): the first quoted value after the key; "" if absent */
static void manifest_str(const char *dir, const char *key, char *out, size_t n) {
    char path[640], pat[48];
    out[0] = 0;
    snprintf(path, sizeof path, "%s/mpc-plugin.json", dir);
    snprintf(pat, sizeof pat, "\"%s\"", key);
    char *s = slurp(path, 64 << 10);
    if (!s) return;
    char *k = strstr(s, pat);
    if (k && (k = strchr(k + strlen(pat), ':')) && (k = strchr(k, '"'))) {
        char *e = strchr(++k, '"');
        if (e && (size_t)(e - k) < n) { memcpy(out, k, e - k); out[e - k] = 0; }
    }
    free(s);
}

/* does the manifest's array member `key` list `needle` ("3.x")? 0 also when there is no such member */
static int manifest_lists(const char *dir, const char *key, const char *needle, int *present) {
    char path[640], pat[48];
    snprintf(path, sizeof path, "%s/mpc-plugin.json", dir);
    snprintf(pat, sizeof pat, "\"%s\"", key);
    *present = 0;
    char *s = slurp(path, 64 << 10);
    if (!s) return 0;
    int hit = 0;
    char *k = strstr(s, pat);
    if (k && (k = strchr(k + strlen(pat), '[')))   {
        char *e = strchr(k, ']');
        if (e) { *e = 0; *present = 1; hit = strstr(k, needle) != NULL; }
    }
    free(s);
    return hit;
}

/* The zip before anything is unpacked: no absolute or ".." names, no backslashes, and a sane unpacked size (WORK is RAM). */
/* "unzip -l" (BusyBox and Info-ZIP agree on it; BusyBox has no -Z, so the old -Z1/-Zt checks passed every zip unread on a device):
 * entries sit between the first and second dashed line, "<length> <date> <time> <name>". BusyBox strips a leading "/" or "../" from
 * names (and says "removing leading" on stderr), so that message counts as an unsafe name too. Fails closed: no listing, no install. */
static const char *inspect_zip(const char *zip) {
    char cmd[900];
    snprintf(cmd, sizeof cmd, "unzip -l '%s' > " WORK "/zl.txt 2>&1", zip);
    if (sh(cmd)) return "could not be read as a zip";
    if (sh("! grep -q 'removing leading' " WORK "/zl.txt && awk '/^ *-+/ {s++; next} s == 1 && NF >= 4 {n = $0; "
           "sub(/^ *[0-9]+ +[^ ]+ +[^ ]+ +/, \"\", n); if (n ~ /^\\/|(^|\\/)\\.\\.(\\/|$)|\\\\/) bad = 1} END {exit bad}' " WORK "/zl.txt"))
        return "has unsafe file names in its zip";
    if (sh("awk '/^ *-+/ {s++; next} s == 1 && NF >= 4 {t += $1} END {exit t > 600000000}' " WORK "/zl.txt"))
        return "is more than 600 MB unpacked";
    return NULL;
}

/* Download, check and unpack every queued package, then write apply.sh. Nothing on the device changes yet. */
static void prepare(mgr_t *m, pkg_t *q) {
    int nq = 0;
    pthread_mutex_lock(&m->mu);
    for (int i = 0; i < m->npkg; i++) if (m->pkg[i].queued) q[nq++] = m->pkg[i];
    m->failed = 0;
    m->downloading = 1;
    m->progress = 0;
    pthread_mutex_unlock(&m->mu);
    char path[512], cmd[1400];
    const char *err = NULL;
    char why[300];
    int i;
    for (i = 0; i < nq && !err; i++) {
        pkg_t *p = &q[i];
        if (p->queued == Q_REGISTER) continue;   /* an entry is added from the folder's plugin-meta.xml: nothing to fetch */
        if (is_addin(p) && p->queued == Q_REMOVE) continue;   /* removed by the uninstall.sh in its folder: nothing to fetch */
        if (!p->url[0] || strlen(p->sha) != 64 || !safe(p->id) || !safe(p->name)) { err = "has no download in the catalog"; break; }
        snprintf(path, sizeof path, WORK "/%s.zip", p->id);
        if (download(m, p, path)) {
            snprintf(why, sizeof why, "download failed after 3 retries (%s)", m->fetch_err[0] ? m->fetch_err : "no connection error: the file was incomplete or its checksum did not match");
            err = why;
            break;
        }
        if ((err = inspect_zip(path))) break;
        snprintf(cmd, sizeof cmd, "rm -rf '" WORK "/%s' && unzip -q -o '%s' -d '" WORK "/%s'", p->id, path, p->id);
        if (sh(cmd)) { err = "could not be unpacked"; break; }
        p->dir[0] = 0;   /* the zip holds one top folder with install.sh / uninstall.sh */
        snprintf(path, sizeof path, WORK "/%s", p->id);
        DIR *d = opendir(path);
        struct dirent *e;
        while (d && (e = readdir(d))) {
            char f[600];
            snprintf(f, sizeof f, "%s/%s/install.sh", path, e->d_name);
            if (e->d_name[0] != '.' && is_file(f) && safe(e->d_name)) snprintf(p->dir, sizeof p->dir, "%s/%s", path, e->d_name);
        }
        if (d) closedir(d);
        if (!p->dir[0]) err = "is not a valid package";
        else if (manifest_str(p->dir, "arch", why, sizeof why), (!strcmp(why, "armv7") || !strcmp(why, "aarch64")) && strcmp(why, want_asset() ? "aarch64" : "armv7")) {   /* other values ("unknown", "") say nothing */
            char have[24];
            snprintf(have, sizeof have, "%s", why);
            snprintf(why, sizeof why, "is built for %s, this MPC is %s", have, want_asset() ? "aarch64" : "armv7");
            err = why;
            break;
        }
        else if (p->queued != Q_REMOVE && (manifest_str(p->dir, "max_glibc", why, sizeof why), why[0] && glibc_less(dev_glibc(), why))) {
            char need[24];
            snprintf(need, sizeof need, "%s", why);
            snprintf(why, sizeof why, "needs glibc %s or newer, this MPC has %s (MPC OS %s: inferred from it)", need, dev_glibc(),
                     glibc_less(dev_glibc(), "2.39") ? "2.x" : "3.x");
            err = why;
            break;
        }
        else if (p->queued != Q_REMOVE) {
            int has, ok = manifest_lists(p->dir, "os_compat", glibc_less(dev_glibc(), "2.39") ? "2.x" : "3.x", &has);
            if (has && !ok) {
                snprintf(why, sizeof why, "is for a different MPC OS, and this one reads as %s (from its glibc %s)",
                         glibc_less(dev_glibc(), "2.39") ? "2.x" : "3.x", dev_glibc());
                err = why;
                break;
            }
        }
        if (err) break;
        if (!is_addin(p) && p->queued == Q_INSTALL && needs_links(p->dir)) {
            const char *t = p->synths[0] && good_target(p->synths) ? p->synths : dest(m);
            if (t[0] && !can_link(t)) {
                snprintf(why, sizeof why, "needs a drive that stores symbolic links, and %s does not", t);
                err = why;
                break;
            }
        }
    }
    FILE *f = err ? NULL : fopen(WORK "/apply.sh", "w");
    if (!err && !f) err = "apply.sh could not be written";
    if (f) {
        fprintf(f, "#!/bin/sh\n# written by the OpenMPC Plugin Manager VST; runs as a transient systemd unit, outside MPC\n"
                   "exec >>" WORK "/apply.log 2>&1\necho \"== apply $(date)\"\nSTATE='%s'\n"
                   "quiesce() { systemctl stop acvs; i=0; while pidof MPC >/dev/null && [ $i -lt 30 ]; do sleep 1; i=$((i + 1)); done; }\n"
                   "quiesce\n"
                   "setstate() { grep -v \"^$1 \" \"$STATE\" 2>/dev/null > \"$STATE.new\"; [ -n \"$2\" ] && echo \"$1 $2\" >> \"$STATE.new\"; mv \"$STATE.new\" \"$STATE\"; }\n"
                   "batch() { grep -q -- '-n)' \"$1\" && printf '%%s' -n; }   # older installers lack -n and restart MPC themselves\n"
                   "SETTINGS='%s'; SNAP='" WORK "/snap'; RES='" WORK "/result'\nrm -rf \"$SNAP\"; mkdir -p \"$SNAP\"; : > \"$RES\"\n"
                   /* item ID FOLDER cmd...: snapshot the plugin folder and MPC.settings, run cmd; on failure put both back and say why (DESIGN 7) */
                   "item() {\n  id=$1; dir=$2; act=$3; ver=$4; shift 4; snap=''\n"
                   /* a small folder is copied to RAM (WORK); a big one to a hidden sibling on its own drive, after a free-space check */
                   "  if [ -n \"$dir\" ] && [ -d \"$dir\" ]; then\n"
                   "    sz=$(du -sk \"$dir\" 2>/dev/null | cut -f1); sz=${sz:-0}\n"
                   "    if [ \"$sz\" -le \"${PM_SNAP_KB:-49152}\" ]; then snap=\"$SNAP/$id.dir\"; else snap=\"$(dirname \"$dir\")/.pm-snap-$id\"; fi\n"
                   "    if [ -e \"$snap\" ]; then echo \"$id|fail|an earlier update left a safety copy at $snap\" >> \"$RES\"; return 1; fi\n"
                   "    free=$(df -Pk \"$(dirname \"$snap\")\" 2>/dev/null | awk 'NR==2 {print $4}')\n"
                   "    if [ \"${free:-0}\" -lt $((sz + 65536)) ]; then echo \"$id|fail|not enough free space to keep a safe copy\" >> \"$RES\"; return 1; fi\n"
                   "    cp -a \"$dir\" \"$snap\" || { rm -rf \"$snap\"; echo \"$id|fail|could not keep a safe copy\" >> \"$RES\"; return 1; }\n"
                   "  fi\n"
                   "  cp -p \"$SETTINGS\" \"$SNAP/$id.settings\" 2>/dev/null\n"
                   "  out=$(\"$@\" 2>&1); rc=$?\n  pidof MPC >/dev/null && quiesce   # an installer without -n started MPC again: MPC starts once, at the end\n"
                   "  if [ $rc -eq 0 ]; then echo \"$out\"; echo \"$id|ok|$act $ver\" >> \"$RES\"; [ -n \"$snap\" ] && rm -rf \"$snap\"; rm -f \"$SNAP/$id.settings\"; return 0; fi\n"
                   "  echo \"$out\"\n"
                   "  if [ -n \"$dir\" ]; then rm -rf \"$dir\"; [ -n \"$snap\" ] && [ -d \"$snap\" ] && cp -a \"$snap\" \"$dir\" && rm -rf \"$snap\"; fi\n"
                   "  [ -f \"$SNAP/$id.settings\" ] && cp -p \"$SNAP/$id.settings\" \"$SETTINGS\"\n"
                   "  echo \"$id|fail|$(echo \"$out\" | tail -n 1 | tr '|' ' ')\" >> \"$RES\"; echo \"-- $id failed: rolled back\"; return 1\n}\n",
                m->dev->state, m->dev->settings);
        if (m->dev->vst2) {   /* MPC OS 3.10.1: the folder copier / plugin.json writer (tools/vst2_migrate.sh), run while MPC is stopped */
            FILE *v = fopen(WORK "/vst2.sh", "w");
            if (v) { fputs(VST2_MIGRATE_SH, v); fclose(v); }
            else err = "vst2.sh could not be written";
        }
        if (!err && m->migrate)   /* first, so an update below finds (and upgrades) the VST2 copy with the user's files in it */
            fprintf(f, "echo '-- move to VST2 Plugins'\nif sh '" WORK "/vst2.sh' --apply; then echo 'move-to-vst2|ok|move' >> \"$RES\"; "
                       "else echo 'move-to-vst2|fail|not every plugin could be copied to VST2 Plugins (" WORK "/apply.log)' >> \"$RES\"; fi\n");
        if (!err && m->nregq) {   /* LOCATIONS: add each drive's Synths folder to MPC.settings (CRLF kept); item() snapshots the settings for a rollback */
            FILE *a = fopen(WORK "/loc_add.awk", "w");
            if (a) {
                fputs("/<\\/SynthContentLocations>/ && !done { cr = ($0 ~ /\\r$/) ? \"\\r\" : \"\"; print \"      <Location>\" loc \"</Location>\" cr; done = 1 }\n"
                      "{ print }\nEND { exit !done }\n", a);
                fclose(a);
            } else err = "could not write the registration files";
            for (int r = 0; r < m->nregq && !err; r++) {
                if (!safe(m->regq[r]) || strpbrk(m->regq[r], "&<>\"|")) { err = "has an unsupported character in its name"; break; }
                fprintf(f, "echo '-- register location %s'\nitem 'loc-%d' '' register '' sh -c 'mkdir -p \"$1\" && awk -v loc=\"$1\" -f \"$2\" \"$3\" > \"$3.new\" && mv \"$3.new\" \"$3\"' sh '%s' '" WORK "/loc_add.awk' '%s'\n",
                        m->regq[r], r + 1, m->regq[r], m->dev->settings);
            }
        }
        for (i = 0; i < nq && !err; i++) {
            pkg_t *p = &q[i];
            /* a remove goes where it is; an update stays where it is if that folder can hold a plugin, else it (and a new
             * install) goes to the first good SynthContentLocations entry. The installer moves the entry by uid.
             * An addin's folder is ADDINS_DIR/<id>: its installer is told that folder, and its removal runs the
             * uninstall.sh kept there (-n: MPC is already stopped; no STATE record, the folder's manifest has it). */
            if (p->queued == Q_REGISTER) {
                const char *key = !strcmp(m->dev->arch, "aarch64") ? "pluginList-arm-64bit" : "pluginList-arm";
                char ef[300];
                snprintf(ef, sizeof ef, WORK "/%s.entry", p->id);
                FILE *x = fopen(ef, "w");
                if (x) { fputs(m->dev->orphan[p->orphan - 1].entry, x); fclose(x); }
                FILE *a = fopen(WORK "/plugin_list.awk", "w");
                if (a) { fputs(PLUGIN_LIST_AWK, a); fclose(a); }
                if (!x || !a) { err = "could not write the registration files"; break; }
                fprintf(f, "echo '-- register %s'\nitem '%s' '' register '' sh -c 'awk -v mode=add -v file=\"$1\" -v uid=\"$2\" -v entryfile=\"$3\" "
                           "-v listkey=%s -f \"$4\" \"$5\" > \"$5.new\" && mv \"$5.new\" \"$5\"' sh '%s' '%s' '%s' '" WORK "/plugin_list.awk' '%s' && rm -f '%s'\n",
                        p->id, p->id, key, m->dev->orphan[p->orphan - 1].file, m->dev->orphan[p->orphan - 1].uid, ef, m->dev->settings, ef);
                continue;
            }
            char target[280];
            if (is_addin(p) && p->queued == Q_REMOVE) snprintf(target, sizeof target, "%s", p->synths);
            else if (is_addin(p)) snprintf(target, sizeof target, "%s/%s", m->dev->addins, p->id);
            else snprintf(target, sizeof target, "%s", p->queued == Q_REMOVE ? p->synths
                          : m->dev->vst2 == 2 ? dest(m)   /* 3.10.1: installs and updates go to VST2 Plugins (install.sh keeps user files on an update) */
                          : p->synths[0] && good_target(p->synths) ? p->synths : dest(m));
            if (m->dev->vst2 != 2 && !is_addin(p) && p->queued == Q_INSTALL && !p->synths[0]) {   /* (3.10.1: the move to VST2 Plugins runs first) */
                /* installed before by something that left no mpc-plugin.json (older installers): the catalog name and the entry's name differ,
                 * so it did not read as installed. An entry already pointing into a folder of this package's name is that install: update it in
                 * place instead of making a second copy on the picked drive (Force, 2026-10-31: AW Ambience in /sdcard and on the SSD) */
                char f0[200] = "";
                manifest_str(p->dir, "folder", f0, sizeof f0);
                const char *bn = strrchr(f0, '/') ? strrchr(f0, '/') + 1 : f0;
                for (int k = 0; bn[0] && k < m->dev->nent; k++) {
                    char fd[320], *sl;
                    snprintf(fd, sizeof fd, "%s", m->dev->ent[k].file);
                    if (!(sl = strrchr(fd, '/'))) continue;
                    *sl = 0;   /* the plugin folder */
                    char *b2 = strrchr(fd, '/');
                    if (!b2 || strcmp(b2 + 1, bn) || !is_dir(fd)) continue;
                    *b2 = 0;   /* its Synths folder */
                    if (good_target(fd)) { snprintf(target, sizeof target, "%s", fd); break; }
                }
            }
            if (m->dev->vst2 == 2 && !is_addin(p) && p->queued == Q_INSTALL) {   /* an update stays on the drive it is on (no second copy) */
                char f0[200] = "", fd[480];
                if (manifest_str(p->dir, "folder", f0, sizeof f0), !f0[0]) manifest_str(p->dir, "skin", f0, sizeof f0);
                const char *bn = strrchr(f0, '/') ? strrchr(f0, '/') + 1 : f0;
                for (int k = 0; bn[0] && k < m->dev->nvdrv; k++) {
                    snprintf(fd, sizeof fd, "%s/%s", m->dev->vdrv[k], bn);
                    if (is_dir(fd)) { snprintf(target, sizeof target, "%s", m->dev->vdrv[k]); break; }
                }
            }
            const char *synths = target;
            char fo[200] = "", dirp[560] = "";   /* the folder the installer writes (the package's "folder"), snapshotted for a rollback */
            if (is_addin(p)) snprintf(dirp, sizeof dirp, "%s", target);
            else if (manifest_str(p->dir, "folder", fo, sizeof fo), (fo[0] || (manifest_str(p->dir, "skin", fo, sizeof fo), fo[0])) && safe(fo))
                snprintf(dirp, sizeof dirp, "%s/%s", target, strrchr(fo, '/') ? strrchr(fo, '/') + 1 : fo);
            if (!synths[0] || !safe(synths) || (is_addin(p) && !m->dev->addins[0])) { err = "has no install location"; break; }
            if (is_addin(p) && p->queued == Q_INSTALL)
                fprintf(f, "echo '-- install %s %s'\nitem '%s' '%s' install '%s' sh '%s/install.sh' -y -n -t '%s' && rm -rf '" WORK "/%s' '" WORK "/%s.zip'\n",
                        p->id, p->latest, p->id, dirp, p->latest, p->dir, synths, p->id, p->id);
            else if (is_addin(p))
                fprintf(f, "echo '-- remove %s'\nitem '%s' '%s' remove '' sh '%s/uninstall.sh' -y -n -t '%s'\n", p->id, p->id, dirp, synths, synths);
            else if (p->queued == Q_INSTALL) {
                fprintf(f, "echo '-- install %s %s'\nitem '%s' '%s' install '%s' sh '%s/install.sh' -y $(batch '%s/install.sh') -t '%s' && setstate '%s' '%s' && rm -rf '" WORK "/%s' '" WORK "/%s.zip'\n",
                        p->id, p->latest, p->id, dirp, p->latest, p->dir, p->dir, synths, p->id, p->latest, p->id, p->id);
                if (m->dev->vst2 == 2 && dirp[0])   /* the installer writes plugin-meta.xml; MPC 3.10.1 needs plugin.json beside it */
                    fprintf(f, "[ -d '%s' ] && sh '" WORK "/vst2.sh' --json '%s'\n", dirp, dirp);
            } else {
                fprintf(f, "echo '-- remove %s'\nitem '%s' '%s' remove '' sh '%s/uninstall.sh' -y $(batch '%s/uninstall.sh') -t '%s' && setstate '%s' '' && rm -rf '" WORK "/%s' '" WORK "/%s.zip'\n",
                        p->id, p->id, dirp, p->dir, p->dir, synths, p->id, p->id, p->id);
                const char *fb = strrchr(dirp, '/');   /* removed from Synths: its copy in VST2 Plugins goes too, or MPC 3.10.1 keeps listing it */
                if (m->dev->vst2 && m->dev->vst2dir[0] && fb && strcmp(synths, m->dev->vst2dir) && safe(m->dev->vst2dir))
                    fprintf(f, "rm -rf '%s%s'\n", m->dev->vst2dir, fb);
                for (int k = 1; fb && k < m->dev->nvdrv; k++)   /* and on any other drive */
                    if (strcmp(synths, m->dev->vdrv[k]) && safe(m->dev->vdrv[k])) fprintf(f, "rm -rf '%s%s'\n", m->dev->vdrv[k], fb);
            }
        }
        if (theme_dirty(m)) {   /* MPC is stopped: copy the wanted skin over "Plugin Skins" (staged, then swapped; the old one goes last) */
            const char *w = m->theme ? "light" : "dark";
            fprintf(f, "echo '-- theme %s'\nT='%s'\nrm -rf \"$T/.skin.new\" \"$T/.skin.old\" && cp -a \"$T/Plugin Skins.%s\" \"$T/.skin.new\" && "
                       "mv \"$T/Plugin Skins\" \"$T/.skin.old\" && mv \"$T/.skin.new\" \"$T/Plugin Skins\" && rm -rf \"$T/.skin.old\" && "
                       "true\n", w, m->own, w);
        }
        /* MPC's skin image cache (a RAM disk only a reboot empties) grows ~400 MB per MPC start and, full, makes skins draw black (Live III, 2026-10-28).
         * MPC is stopped here, so nothing has the files open: empty it before MPC starts again, so the manager's own restarts do not fill it. */
        fprintf(f, "if ! pidof MPC >/dev/null; then rm -f /var/volatile/tmp/filmstrips/temp_*.img 2>/dev/null; echo '-- image cache emptied'; fi\n"
                   "pidof MPC >/dev/null || systemctl start acvs\nrm -f \"$0\"\necho \"== done $(date)\"\n");
        fclose(f);
    }
    pthread_mutex_lock(&m->mu);
    m->downloading = 0;
    m->reprompt = 0;
    if (err) {
        m->failed = 1;
        say_locked(m, ERR, "%s: %s. Nothing on the MPC was changed.", i < nq ? q[i].name : "APPLY", err);
    } else {
        m->ready = 1;
        say_locked(m, WARN, "Ready. Save your project: APPLY restarts MPC.");
    }
    pthread_mutex_unlock(&m->mu);
}

static void do_prepare(mgr_t *m) {
    pkg_t *q = calloc(MAXPKG, sizeof *q);   /* ~256 KB: kept off MPC's small thread stacks */
    if (q) prepare(m, q);
    free(q);
}

static void do_launch(mgr_t *m) {
    say(m, OK, "Restarting MPC\xe2\x80\xa6 it will be back in a few seconds");
    logf_("launching apply.sh");
#if defined(__arm__) || defined(__aarch64__)
    if (m->dev->systemd_run) {
        char unit[64];
        snprintf(unit, sizeof unit, "--unit=openmpcpm-apply-%ld", (long)time(NULL));
        char *argv[] = {"systemd-run", unit, "--collect", "/bin/sh", WORK "/apply.sh", NULL};
        if (run(argv)) say(m, ERR, "MPC could not be restarted (systemd-run failed). Nothing was changed.");
    } else {
        /* untested fallback: leave acvs.service's cgroup by hand (cgroup v2 root) so stopping MPC doesn't kill us */
        if (sh("echo $$ > /sys/fs/cgroup/cgroup.procs && (setsid /bin/sh " WORK "/apply.sh </dev/null >/dev/null 2>&1 &)"))
            say(m, ERR, "MPC could not be restarted. Nothing was changed.");
    }
#else
    say(m, OK, "Dry run: apply.sh written");   /* host tests: never touch the build machine */
#endif
}

static void *worker(void *ud) {
    mgr_t *m = ud;
    mkdir(WORK, 0755);
    pthread_mutex_lock(&m->mu);
    m->job = J_REFRESH;
    for (;;) {
        while (!m->quit && m->job == J_NONE) pthread_cond_wait(&m->cv, &m->mu);
        if (m->quit) break;
        int job = m->job;
        m->job = J_NONE;
        m->busy = job;
        m->rev++;
        pthread_mutex_unlock(&m->mu);
        if (job == J_REFRESH) do_refresh(m);
        else if (job == J_PREPARE) do_prepare(m);
        else if (job == J_LAUNCH) do_launch(m);
        else if (job == J_PATCH) do_patch(m);
        else if (job == J_PAPPLY) do_papply(m);
        pthread_mutex_lock(&m->mu);
        m->busy = 0;
        m->rev++;
    }
    pthread_mutex_unlock(&m->mu);
    return NULL;
}

/* ---- what the screen shows: everything is computed from the state on each read ---- */

static int installed(const pkg_t *p) { return p->installed[0] != 0; }
/* a < b as dotted numbers ("0.10.0" > "0.9.0"); equal numbers: a "-beta" style suffix is older than none, otherwise by text */
static int ver_older(const char *a, const char *b) {
    for (;;) {
        char *ea, *eb;
        long x = strtol(a, &ea, 10), y = strtol(b, &eb, 10);
        if (ea == a || eb == b) break;   /* no number left on one side */
        if (x != y) return x < y;
        a = ea; b = eb;
        if (!*a && *b == '.') a = ".0";   /* 1.2 is 1.2.0 */
        if (!*b && *a == '.') b = ".0";
        if (*a == '.' && *b == '.') { a++; b++; continue; }
        break;
    }
    return strcmp(a, b) && (!*b ? *a != 0 : *a && strcmp(a, b) < 0);
}
static int has_update(const pkg_t *p) {   /* a known older version, or an old-layout install; one newer than the catalog's is not an update */
    return p->url[0] && installed(p) && (p->legacy || (strcmp(p->installed, "?") && ver_older(p->installed, p->latest)));
}

/* the cards of the current tab and filter; build-yourself plugins (no download) are left out: not plug and play */
static int nth_tag(const pkg_t *p, int k, char *b, int n) {   /* tag k (0-based) of p into b; 0 when it has fewer */
    const char *t = p->alltags;
    for (int i = 0; *t == '|' && t[1]; i++) {
        const char *e = strchr(t + 1, '|');
        if (!e) break;
        if (i == k) { snprintf(b, n, "%.*s", (int)(e - t - 1), t + 1); return 1; }
        t = e;
    }
    return 0;
}

static int has_tag(const pkg_t *p, const char *tag) {
    char want[40];
    snprintf(want, sizeof want, "|%s|", tag);
    return strstr(p->alltags, want) != NULL;
}

static int sorts_before(const mgr_t *m, int a, int b) {   /* in the current sort; ties by name */
    const pkg_t *p = &m->pkg[a], *q = &m->pkg[b];
    int c = m->sort == 0 ? strcmp(q->updated, p->updated) : m->sort == 1 ? strcmp(q->first, p->first)
          : m->sort == 2 ? (q->downloads > p->downloads) - (q->downloads < p->downloads) : 0;
    return c ? c < 0 : strcasecmp(p->name, q->name) < 0;
}

static int has_text(const char *hay, const char *needle) {   /* case-insensitive substring */
    size_t n = strlen(needle);
    for (; *hay; hay++)
        if (!strncasecmp(hay, needle, n)) return 1;
    return 0;
}

static int matches(const pkg_t *p, const char *q) {   /* the search: name, author or a tag */
    return has_text(p->name, q) || has_text(p->author, q) || has_text(p->alltags, q);
}

/* not offered on this MPC: the catalog's "devices" does not list this model (an unknown model hides nothing), or it is a beta and SETTINGS hides
 * betas. Something installed always shows. The screen and the web page both use this. */
static int hidden_here(const mgr_t *m, const pkg_t *p) {
    if (installed(p)) return 0;
    if (p->devices[0] && m->dev->model_id[0]) {
        char tag[64];
        snprintf(tag, sizeof tag, "|%s|", m->dev->model_id);
        if (!strstr(p->devices, tag)) return 1;
    }
    return !m->show_beta && !strcmp(p->channel, "beta");
}

static int visible_now(const mgr_t *m, int *idx) {
    int n = 0;
    for (int i = 0; i < m->npkg; i++) {
        const pkg_t *p = &m->pkg[i];
        if (!p->url[0]) continue;
        if (m->kindf == 1 && strcmp(p->kind, "instrument")) continue;
        if (m->kindf == 2 && strcmp(p->kind, "effect")) continue;
        if (m->kindf == 3 && !is_addin(p)) continue;
        if (hidden_here(m, p)) continue;   /* made for other MPCs, or a beta with beta hidden */
        if (m->tab == 0 && installed(p)) continue;   /* DISCOVER: what isn't on this MPC yet */
        if (m->tag[0] && !has_tag(p, m->tag)) continue;
        if (m->q[0] && !matches(p, m->q)) continue;
        if (m->tab == 1 && !installed(p)) continue;
        if (m->tab == 2 && !has_update(p)) continue;
        idx[n++] = i;
    }
    for (int a = 1; a < n; a++)   /* insertion sort, stable; n is small */
        for (int b = a; b > 0 && sorts_before(m, idx[b], idx[b - 1]); b--) { int t = idx[b]; idx[b] = idx[b - 1]; idx[b - 1] = t; }
    return n;
}

/* The cards' plugins, in order. The screen asks for dozens of values every refresh, each needing this list, so it is
 * worked out once per change: anything that changes it (a tap, the worker) bumps m->rev. Called with mu held. */
static int visible(const mgr_t *m, int *idx) {
    mgr_t *w = (mgr_t *)m;
    if (w->vis_rev != m->rev + 1) { w->vis_n = visible_now(m, w->vis_idx); w->vis_rev = m->rev + 1; }
    memcpy(idx, m->vis_idx, sizeof(int) * m->vis_n);
    return m->vis_n;
}

enum { S_NONE, S_INSTALL, S_UPDATE, S_INSTALLED, S_Q_INSTALL, S_Q_UPDATE, S_Q_REMOVE, S_DISABLED, S_MENU, S_REGISTER, S_Q_REGISTER };

static int pkg_state(const mgr_t *m, int i) {   /* card_state() without the card's menu */
    const pkg_t *p = &m->pkg[i];
    if (p->orphan) return p->queued ? S_Q_REGISTER : S_REGISTER;
    if (p->queued == Q_REMOVE) return S_Q_REMOVE;
    if (p->queued == Q_INSTALL) return installed(p) ? S_Q_UPDATE : S_Q_INSTALL;
    if (m->dev->problem[0] && !installed(p)) return S_DISABLED;
    if (!installed(p)) return S_INSTALL;
    return has_update(p) ? S_UPDATE : S_INSTALLED;
}

static int card_state(const mgr_t *m, int i) { return m->menu == i ? S_MENU : pkg_state(m, i); }

static int detail_of(const mgr_t *m) {   /* the detail view's pkg index, -1 when the list shows */
    for (int i = 0; m->detail[0] && i < m->npkg; i++)
        if (!strcmp(m->pkg[i].id, m->detail)) return i;
    return -1;
}

static int count_updates(const mgr_t *m) {
    int n = 0;
    for (int i = 0; i < m->npkg; i++) n += has_update(&m->pkg[i]);
    return n;
}

static int installs_queued(const mgr_t *m) {   /* a plugin or addin will be written to a drive */
    for (int i = 0; i < m->npkg; i++) if (m->pkg[i].queued == Q_INSTALL) return 1;
    return 0;
}

static int count_queued(const mgr_t *m) {
    int n = theme_dirty(m) + m->migrate + m->nregq + m->clearcache;   /* a theme change (or the move to VST2 Plugins) is applied like an install: stop MPC, ..., start MPC */
    for (int i = 0; i < m->npkg; i++) n += m->pkg[i].queued != 0;
    return n;
}

static void changes_text(const mgr_t *m, char *b, int n) {   /* the restart prompt's list: +new, old > new, -removed */
    int k = 0;
    b[0] = 0;
    for (int i = 0; i < m->npkg && k < n; i++) {
        const pkg_t *p = &m->pkg[i];
        const char *sep = k ? "\xc2\xa0\xc2\xb7  " : "";   /* no-break space: a wrapped line never starts with the dot */
        if (p->queued == Q_REMOVE) k += snprintf(b + k, n - k, "%s\xe2\x88\x92 %s", sep, p->name);
        else if (p->queued && installed(p) && strcmp(p->installed, "?"))
            k += snprintf(b + k, n - k, "%s%s %s > %s", sep, p->name, p->installed, p->latest);
        else if (p->queued) k += snprintf(b + k, n - k, "%s+ %s %s", sep, p->name, p->latest);
    }
    if (theme_dirty(m) && k < n) k += snprintf(b + k, n - k, "%sTheme: %s", k ? "\xc2\xa0\xc2\xb7  " : "", m->theme ? "light" : "dark");
    if (m->clearcache && k < n) k += snprintf(b + k, n - k, "%sClear image cache", k ? "\xc2\xa0\xc2\xb7  " : "");
    for (int r = 0; r < m->nregq && k < n; r++) k += snprintf(b + k, n - k, "%sRegister %s", k ? "\xc2\xa0\xc2\xb7  " : "", m->regq[r]);
    if (m->migrate && k < n) snprintf(b + k, n - k, "%sMove %d plugin%s to VST2 Plugins", k ? "\xc2\xa0\xc2\xb7  " : "", m->dev->nmove, m->dev->nmove == 1 ? "" : "s");
}

/* the web page's address while one instance serves it (web.c), shared by every instance: the one on screen may not be the one
 * holding the port */
static pthread_mutex_t web_mu = PTHREAD_MUTEX_INITIALIZER;
static char web_url[64];

static void queue_text(const mgr_t *m, char *b, int n) {
    int q = count_queued(m), k;
    if (!q) {
        char u[64];
        pthread_mutex_lock(&web_mu);
        snprintf(u, sizeof u, "%s", web_url);
        pthread_mutex_unlock(&web_mu);
        if (u[0]) snprintf(b, n, "On your phone or computer: %s", u);
        else snprintf(b, n, "Nothing queued. Pick INSTALL or UPDATE on a plugin.");
        return;
    }
    k = snprintf(b, n, "%d change%s queued:", q, q > 1 ? "s" : "");
    for (int i = 0; i < m->npkg && k < n; i++)
        if (m->pkg[i].queued)
            k += snprintf(b + k, n - k, "  %s%s", m->pkg[i].queued == Q_REMOVE ? "\xe2\x88\x92" : "+", m->pkg[i].name);
    if (theme_dirty(m) && k < n) k += snprintf(b + k, n - k, "  Theme: %s", m->theme ? "light" : "dark");
    if (m->clearcache && k < n) k += snprintf(b + k, n - k, "  Clear image cache");
    for (int r = 0; r < m->nregq && k < n; r++) k += snprintf(b + k, n - k, "  Register %s", m->regq[r]);
    if (m->migrate && k < n) snprintf(b + k, n - k, "  Move %d to VST2 Plugins", m->dev->nmove);
}

static void initials(const char *name, char *b) {   /* first letters of the first two words that start with a letter */
    int k = 0, word = 1;
    for (const char *c = name; *c && k < 2; c++) {
        int al = (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z'), an = al || (*c >= '0' && *c <= '9');
        if (al && word) b[k++] = (*c >= 'a' && *c <= 'z') ? *c - 32 : *c;
        word = !an;
    }
    if (k == 1 && name[1]) b[k++] = (name[1] >= 'a' && name[1] <= 'z') ? name[1] - 32 : name[1];
    b[k] = 0;
}

static int shot(const char *id) {   /* its r<N>_shot option: 1 + its index in SHOTS, 0: none */
    for (int i = 0; i < (int)(sizeof SHOTS / sizeof *SHOTS); i++)
        if (!strcmp(SHOTS[i], id)) return i + 1;
    return 0;
}

/* line `want` (0-based) of s word-wrapped at `width` characters; the last of `lines` ends in "..." when text is left */
static void wrap_line(const char *s, int want, int lines, int width, char *b, int n) {
    for (int l = 0;; l++) {
        while (*s == ' ') s++;
        const char *e = s, *sp = NULL;
        int c = 0;
        for (; *e && c < width; e++) {
            if ((*e & 0xc0) != 0x80) c++;
            if (*e == ' ') sp = e;
        }
        while ((*e & 0xc0) == 0x80) e++;
        int more = *e != 0;
        if (more && sp && *e != ' ') e = sp;
        if (l == want) {
            int len = (int)(e - s);
            if (l == lines - 1 && more) {
                while (len > 0 && s[len - 1] == ' ') len--;
                snprintf(b, n, "%.*s\xe2\x80\xa6", len, s);
            } else snprintf(b, n, "%.*s", len, s);
            if (!b[0]) snprintf(b, n, " ");
            return;
        }
        s = e;
    }
}

static void short_date(const char *iso, char *b, int n) {   /* "2026-10-02" -> "2 Oct", with the year when not this one */
    static const char *mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int y, mo, d;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    if (sscanf(iso, "%d-%d-%d", &y, &mo, &d) != 3 || mo < 1 || mo > 12) { snprintf(b, n, "\xe2\x80\x94"); return; }
    if (y == tm.tm_year + 1900) snprintf(b, n, "%d %s", d, mon[mo - 1]);
    else snprintf(b, n, "%d %s %d", d, mon[mo - 1], y);
}

static void spaced(const char *in, char *out, int n) {   /* catalog slugs ("fm-synth") as words */
    int k = 0;
    for (; *in && k < n - 1; in++) out[k++] = *in == '-' ? ' ' : *in;
    out[k] = 0;
}

/* a card's field (key after "rN_"), for the plugin at pkg index i (or -1: an empty card) */
static void card_field(const mgr_t *m, int i, const char *f, char *b, int n) {
    const pkg_t *p = i >= 0 ? &m->pkg[i] : NULL;
    b[0] = 0;
    if (!strcmp(f, "vis")) snprintf(b, n, "%d", p != NULL && m->loaded);
    else if (!p) snprintf(b, n, !strcmp(f, "state") || !strcmp(f, "inst") || !strcmp(f, "chan") || !strcmp(f, "cpu") ||
                                 !strcmp(f, "old") || !strcmp(f, "tested") || !strcmp(f, "shot") ? "0" : " ");
    else if (!strcmp(f, "state")) snprintf(b, n, "%d", card_state(m, i));
    else if (!strcmp(f, "inst")) snprintf(b, n, "%d", installed(p) && !p->orphan);
    else if (!strcmp(f, "init")) initials(p->name, b);
    else if (!strcmp(f, "shot")) snprintf(b, n, "%d", shot(p->id));
    else if (!strcmp(f, "kindtxt")) snprintf(b, n, "%s", is_addin(p) ? "ADDIN" : !strcmp(p->kind, "effect") ? "FX" : "INST");
    else if (!strcmp(f, "meta")) {
        char st[32], tg[96];
        spaced(p->style, st, sizeof st);
        spaced(p->tags, tg, sizeof tg);
        snprintf(b, n, "by %s%s%s%s%s", p->author, st[0] ? "  \xc2\xb7  " : "", st, tg[0] ? "  \xc2\xb7  " : "", tg);
    }
    else if (!strcmp(f, "chan")) snprintf(b, n, "%d", !strcmp(p->channel, "beta") ? 2 : 1);   /* 0: no card */
    else if (!strcmp(f, "cpu")) snprintf(b, n, "%d", !strcmp(p->cpu, "WARN") || !strcmp(p->cpu, "FAIL"));
    else if (!strcmp(f, "old")) snprintf(b, n, "%d", p->legacy);
    else if (!strcmp(f, "tested")) snprintf(b, n, "%d", p->tested[0] != 0);
    else if (!strcmp(f, "tested_txt")) snprintf(b, n, "Tested: %s", !strncmp(p->tested, "Akai ", 5) ? p->tested + 5 : p->tested);
    else if (!strcmp(f, "ver")) snprintf(b, n, "v%s", p->latest);
    else if (!strcmp(f, "from")) snprintf(b, n, "%s%s", has_update(p) && !p->legacy ? "from " : " ",
                                          has_update(p) && !p->legacy ? p->installed : "");
    else if (!strcmp(f, "size")) {
        if (p->size >= 1000000) snprintf(b, n, "%.1f MB", p->size / 1e6);
        else snprintf(b, n, "%lld KB", p->size / 1000);
    } else if (!strcmp(f, "info")) {   /* what the list is sorted by */
        char d[24];
        if (m->sort == 2) snprintf(b, n, "%d download%s", p->downloads, p->downloads == 1 ? "" : "s");
        else { short_date(m->sort == 1 ? p->first : p->updated, d, sizeof d); snprintf(b, n, "%s %s", m->sort == 1 ? "Released" : "Updated", d); }
    }
    else snprintf(b, n, "0");   /* the triggers */
}

static void *web_thread(void *ud);   /* web.c, included below the actions it calls */

/* ---- engine interface ---- */

static void *mgr_create(const char *data_dir) {
    (void)data_dir;
    mgr_t *m = calloc(1, sizeof *m);
    if (!m) return NULL;
    pthread_mutex_init(&m->mu, NULL);
    pthread_cond_init(&m->cv, NULL);
    m->sel = m->menu = -1;
    m->online = 1;
    snprintf(m->status, sizeof m->status, "Starting\xe2\x80\xa6");
    m->sticky = 1;
    m->dev = calloc(1, sizeof *m->dev);
    if (!m->dev) { free(m); return NULL; }
    if (pthread_create(&m->th, NULL, worker, m)) { free(m->dev); free(m); return NULL; }
    m->show_beta = m->web_pref = 1;
    {
        char b0[200];
        cfg_base_guess(b0, sizeof b0);
        if (b0[0]) { m->show_beta = cfg_get(b0, "beta", 1); m->web_pref = cfg_get(b0, "web", 1); }
    }
    m->web_on = m->web_pref && !getenv("PM_NO_WEB") && !pthread_create(&m->web, NULL, web_thread, m);
    return m;
}

static void mgr_destroy(void *inst) {
    mgr_t *m = inst;
    pthread_mutex_lock(&m->mu);
    m->quit = 1;
    pthread_cond_signal(&m->cv);
    pthread_mutex_unlock(&m->mu);
    pthread_join(m->th, NULL);
    if (m->web_on) pthread_join(m->web, NULL);
    free(m->dev);
    free(m);
}

static void post(mgr_t *m, int job) {   /* called with mu held */
    if (m->busy || m->job) return;
    m->job = job;
    pthread_cond_signal(&m->cv);
}

static void requeue(mgr_t *m, pkg_t *p, int q) {   /* any change to the queue drops a prepared or failed APPLY */
    p->queued = q;
    m->ready = m->failed = 0;
    m->sticky = 0;
    m->menu = -1;
}

static void act(mgr_t *m, int i, const char *f) {   /* act, reinstall or remove on pkg i (mu held, not busy) */
    pkg_t *p = &m->pkg[i];
    int st = pkg_state(m, i);
    m->sel = i;
    if (!strcmp(f, "act")) {
        if (st == S_REGISTER) requeue(m, p, Q_REGISTER);
        else if (st == S_Q_REGISTER) requeue(m, p, 0);
        else if (st == S_INSTALL || st == S_UPDATE) requeue(m, p, Q_INSTALL);
        else if (st == S_Q_INSTALL || st == S_Q_UPDATE || st == S_Q_REMOVE) requeue(m, p, 0);
        else if (st == S_DISABLED) say_locked(m, ERR, "Installing is turned off on this MPC (%s)", m->dev->problem);
    } else if (!installed(p) || p->orphan) {
        /* the menu (Reinstall, Remove) is only on installed plugins (not on a folder MPC does not list) */
    } else if (!strcmp(f, "more")) m->menu = m->menu == i ? -1 : i;
    else if (!strcmp(f, "reinstall")) requeue(m, p, Q_INSTALL);
    else if (!strcmp(f, "remove")) {
        if (p->legacy && is_addin(p)) { m->menu = -1; say_locked(m, ERR, "%s was installed by hand: update it first, then remove it", p->name); }
        else if (p->legacy) { m->menu = -1; say_locked(m, ERR, "%s is an old-style install: update it first, then remove it", p->name); }
        else requeue(m, p, Q_REMOVE);
    }
}

/* MPC sent one tap twice on a Live III (2026-10-28 trace of THEME, and the page arrows): a second press within `ms` of the last
 * accepted one is the same tap. Returns 1 for that repeat; otherwise records the time and returns 0. */
static int repeated(long long *last, int ms) {
#ifdef FS_ANY
    if (getenv("PM_TAPMS")) ms = ms ? atoi(getenv("PM_TAPMS")) : 0;   /* tests: a short window, so they need not wait 250 ms between taps */
#endif
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long long now = ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
    if (now - *last < ms) return 1;
    *last = now;
    return 0;
}

#include "web.c"

/* the trigger params (a tap), not the option params the host steps on purpose (kind, sort, ...), the keyboard keys or THEME (its own window) */
static int is_button(const char *key) {
    static const char *const end[] = {"_act", "_open", "_more", "_reinstall", "_remove", "_btn", "_pick1", "_pick2", "_pick3", "_pick4",
                                      "_close", "_back", "_cancel", "_go", "_clear", "_next", "_prev", "_reg", "_pick", "_rescan", "_chk"};
    size_t kl = strlen(key);
    if (!strcmp(key, "theme_btn")) return 0;
    if (!strcmp(key, "update_all") || !strcmp(key, "apply") || !strcmp(key, "refresh") || !strncmp(key, "d_tag", 5)) return 1;
    for (size_t i = 0; i < sizeof end / sizeof *end; i++) {
        size_t el = strlen(end[i]);
        if (kl > el && !strcmp(key + kl - el, end[i])) return 1;
    }
    return 0;
}

static void mgr_set_param(void *inst, const char *key, const char *val) {
    mgr_t *m = inst;
    double v = atof(val);
    if (!access(WORK "/trace", F_OK) && strcmp(key, "display_rev"))   /* touch /tmp/pluginmgr/trace: log every input */
        logf_("set %s=%s", key, val);
    pthread_mutex_lock(&m->mu);
    int idx[MAXPKG], nv = visible(m, idx), pages = nv ? (nv + ROWS - 1) / ROWS : 1;
    int busy = m->busy == J_PREPARE || m->busy == J_LAUNCH;
    if (v >= 0.5 && is_button(key)) {   /* the same button twice within 250 ms is one tap */
        if (!strcmp(key, m->tap_key) && repeated(&m->tap_t, 250)) { pthread_mutex_unlock(&m->mu); return; }
        if (strcmp(key, m->tap_key)) { snprintf(m->tap_key, sizeof m->tap_key, "%s", key); repeated(&m->tap_t, 0); }
    }
    if (v >= 0.5 && m->kb && !strncmp(key, "k_", 2)) {
        /* MPC sends a tap on a drawn key twice, with no release: Live III trace 2026-10-30, "set k_A=1" twice per tap, 30 to 140 ms apart
         * (the finger going down and lifting), so a letter was typed twice. The same key again within 250 ms of the accepted press is the same
         * tap, as for the buttons; only the same key counts, so typing different keys quickly is not affected. */
        if (!strcmp(key + 2, m->kb_key) && repeated(&m->kb_t, 250)) { pthread_mutex_unlock(&m->mu); return; }
        if (strcmp(key + 2, m->kb_key)) { snprintf(m->kb_key, sizeof m->kb_key, "%s", key + 2); repeated(&m->kb_t, 0); }
    }
    if (m->confirm && !m->reprompt && (!m->ready || busy)) m->confirm = 0;   /* nothing left to restart for */
    if (v >= 0.5 && !strcmp(key, "theme_btn") && m->theme_can && !m->confirm && !busy) {   /* the next apply carries it */
        if (repeated(&m->theme_t, 700)) { pthread_mutex_unlock(&m->mu); return; }
        m->theme = !m->theme;
        theme_save(m);
        m->ready = 0;   /* a prepared apply.sh does not have the swap (or still has it): prepare again */
        pthread_mutex_unlock(&m->mu);
        return;
    }
    if (m->confirm) {   /* the restart prompt takes every tap until it is answered */
        if (v >= 0.5 && !strcmp(key, "target_next") && ntargets(m) > 1 && m->ready && !busy) {
            m->tsel = (m->tsel + 1) % ntargets(m);   /* the apply script names the target: prepare again (the downloads are cached) */
            m->ready = 0;
            m->reprompt = 1;
            post(m, J_PREPARE);
        } else if (v >= 0.5 && !strcmp(key, "confirm_cancel")) m->confirm = 0;   /* the prepared queue stays ready */
        else if (v >= 0.5 && !strcmp(key, "confirm_go")) {
            m->confirm = 0;
            if (m->dev->nusable > 1) target_save(m);   /* remember the place the user confirmed */
            if (m->ready && !busy) { m->ready = 0; post(m, J_LAUNCH); }
        }
    } else if (v >= 0.5 && !strcmp(key, "search_btn")) {   /* the header's search button or search chip: type */
        m->kb = 1;
        m->menu_open = 0;
        m->detail[0] = 0;
        m->tag[0] = 0;
        m->menu = -1;
    } else if (v >= 0.5 && !strcmp(key, "q_clear")) { m->q[0] = 0; m->kb = 0; m->page = 0; }
    else if (v >= 0.5 && m->kb && !strncmp(key, "k_", 2)) {   /* a key */
        const char *k = key + 2;
        size_t l = strlen(m->q);
        if (!strcmp(k, "done") || !strcmp(k, "hide")) m->kb = 0;
        else if (!strcmp(k, "clear")) m->q[0] = 0;
        else if (!strcmp(k, "back")) { if (l) m->q[l - 1] = 0; }
        else if (l + 1 < sizeof m->q && (!strcmp(k, "space") ? l && m->q[l - 1] != ' ' : k[0] && !k[1])) {
            m->q[l] = !strcmp(k, "space") ? ' ' : k[0];
            m->q[l + 1] = 0;
        }
        m->page = 0;
        m->menu = -1;
    } else if (v >= 0.5 && m->kb && !strncmp(key, "sr", 2) && (key[2] == '1' || key[2] == '2') && key[3] == '_') {   /* a match row */
        int k = key[2] - '1';
        if (k < nv && !strcmp(key + 4, "open")) {
            m->kb = 0;
            m->sel = idx[k];
            snprintf(m->detail, sizeof m->detail, "%s", m->pkg[idx[k]].id);
        } else if (k < nv && !strcmp(key + 4, "act") && !busy) act(m, idx[k], "act");
    } else if (!strcmp(key, "tab") || !strcmp(key, "kind") || !strcmp(key, "sort")) {   /* option params: the index as a number
                                                                                       * (and how the host restores them) */
        int o = (int)(v + 0.5);
        if (key[0] == 't') m->tab = o; else if (key[0] == 'k') m->kindf = o; else m->sort = o < 0 || o > 3 ? 0 : o;
        m->page = 0;
        m->menu = -1;
        m->menu_open = 0;
        if (key[0] != 's') m->detail[0] = 0;   /* the sort, restored with the project, leaves things be */
    } else if (!strncmp(key, "card", 4)) {   /* a tap anywhere on a card selects it and opens its details (both toggle
                                                 * directions); not before the catalog loaded, so a restored value can't */
        int r = key[4] - '1', k = m->page * ROWS + r;
        if (m->loaded && !m->detail[0] && k >= 0 && k < nv) {
            m->sel = idx[k];
            m->menu = -1;
            snprintf(m->detail, sizeof m->detail, "%s", m->pkg[idx[k]].id);
        }
    } else if (v < 0.5) {
        /* the other params are triggers: act on press only */
    } else if (!strcmp(key, "menu_close")) m->menu_open = 0;   /* a tap outside an open dropdown */
    else if (!strcmp(key, "kind_btn") || !strcmp(key, "sort_btn")) {   /* opens only: a quick second tap (or a bounce)
                                                                         * before the menu is drawn must not close it */
        m->menu_open = key[0] == 'k' ? 1 : 2;
    } else if ((!strncmp(key, "kind_pick", 9) || !strncmp(key, "sort_pick", 9)) && key[9] >= '1' && key[9] <= '4' &&
               m->menu_open == (key[0] == 'k' ? 1 : 2)) {   /* a row of the open menu */
        if (key[0] == 'k') m->kindf = key[9] - '1'; else m->sort = key[9] - '1';
        m->menu_open = 0;
        m->page = 0;
        m->menu = -1;
        m->detail[0] = 0;
    } else if (!strcmp(key, "disk_btn") && ntargets(m) > 1 && !busy) m->menu_open = 3;   /* the drive menu: where new installs go */
    else if (!strncmp(key, "drv_pick", 8) && key[8] >= '1' && key[8] <= '4' && m->menu_open == 3) {
        int k = key[8] - '1';
        m->menu_open = 0;
        if (k < ntargets(m) && k != m->tsel % ntargets(m)) {
            m->tsel = k;
            target_save(m);   /* remembered, as a pick on the restart prompt is */
            m->ready = 0;     /* a prepared apply.sh names the old drive */
            post(m, J_REFRESH);   /* the readout shows the new drive's free space */
        }
    } else if (!strncmp(key, "loc", 3) && key[3] >= '1' && key[3] <= '4' && !strcmp(key + 4, "_pick") && !busy) {   /* LOCATIONS: MAKE DEFAULT */
        int ri = m->locpage * 4 + key[3] - '1';
        int k = ri < m->nlocs && m->locs[ri].pick ? loc_target_index(m, m->locs[ri].path) : -1;
        if (k >= 0 && ntargets(m) > 1) {
            m->tsel = k;
            target_save(m);   /* remembered, as a pick in the drive menu is */
            m->ready = 0;
            post(m, J_REFRESH);
        }
    } else if (!strncmp(key, "loc", 3) && key[3] >= '1' && key[3] <= '4' && !strcmp(key + 4, "_reg") && !busy) {   /* LOCATIONS: REGISTER / cancel */
        int ri = m->locpage * 4 + key[3] - '1';
        if (ri < m->nlocs && m->locs[ri].cand && m->locs[ri].usable && m->dev->hasloclist) {
            int at = -1;
            for (int q = 0; q < m->nregq; q++) if (!strcmp(m->regq[q], m->locs[ri].path)) at = q;
            if (at >= 0) {
                for (int q = at; q + 1 < m->nregq; q++) snprintf(m->regq[q], sizeof m->regq[q], "%s", m->regq[q + 1]);
                m->nregq--;
            } else if (m->nregq < 4) snprintf(m->regq[m->nregq++], sizeof m->regq[0], "%s", m->locs[ri].path);
            m->ready = 0;   /* a prepared apply.sh does not have it */
        }
    } else if (!strcmp(key, "set_beta_btn")) {   /* SETTINGS: beta plugins in Discover */
        m->show_beta = !m->show_beta;
        cfg_save(m);
        m->page = 0;
    } else if (!strcmp(key, "set_cache_btn")) {   /* SETTINGS: queue / cancel emptying the image cache (done by APPLY, MPC stopped) */
        m->clearcache = !m->clearcache;
        m->ready = 0;
    } else if (!strcmp(key, "set_web_btn")) {   /* SETTINGS: the web page, from the next MPC start */
        m->web_pref = !m->web_pref;
        cfg_save(m);
    } else if (!strncmp(key, "pat", 3) && key[3] >= '1' && key[3] <= '3' && !strcmp(key + 4, "_chk") && !m->busy && !m->job) {   /* PATCHES: CHECK */
        int pi = m->ptpage * 3 + key[3] - '1';
        if (pi < m->npt && pt_arch_ok(m, &m->pt[pi])) {
            m->ptidx = pi;
            m->ptconf = 0;
            say_locked(m, OK, "Checking \"%s\": running its status (nothing is changed)\xe2\x80\xa6", m->pt[pi].title);
            post(m, J_PATCH);
        }
    } else if (!strncmp(key, "pat", 3) && key[3] >= '1' && key[3] <= '3' && !strcmp(key + 4, "_act") && !m->busy && !m->job) {   /* PATCHES: APPLY / UNDO, twice */
        int pi = m->ptpage * 3 + key[3] - '1';
        int kind = pi < m->npt ? pt_act_kind(m, &m->pt[pi]) : 0;
        if (kind) {
            long long now = mono_ms();
            int want = 1 + pi * 2 + (kind == 2);
            if (m->ptconf == want && now - m->ptconf_t < 6000) {   /* the second tap, within 6 s */
                m->ptconf = 0;
                m->ptidx = pi;
                m->ptact = kind;
                say_locked(m, OK, "%s \"%s\"\xe2\x80\xa6", kind == 2 ? "Removing" : "Applying", m->pt[pi].title);
                post(m, J_PAPPLY);
            } else { m->ptconf = want; m->ptconf_t = now; }   /* the first tap: the button asks again */
        }
    } else if (!strcmp(key, "pat_prev") && m->ptpage > 0) { m->ptpage--; m->ptconf = 0; }
    else if (!strcmp(key, "pat_next") && (m->ptpage + 1) * 3 < m->npt) { m->ptpage++; m->ptconf = 0; }
    else if (!strcmp(key, "loc_prev") && m->locpage > 0) m->locpage--;
    else if (!strcmp(key, "loc_next") && (m->locpage + 1) * 4 < m->nlocs) m->locpage++;
    else if (!strcmp(key, "loc_rescan") && !busy) post(m, J_REFRESH);
    else if (!strcmp(key, "d_back")) { m->detail[0] = 0; m->menu = -1; }
    else if (!strcmp(key, "tag_clear")) { m->tag[0] = 0; m->page = 0; }
    else if (!strncmp(key, "d_tag", 5) && key[5] >= '1' && key[5] <= '8') {   /* a tag button: list the plugins with that tag (in the current tab) */
        int i = detail_of(m);
        if (i >= 0 && nth_tag(&m->pkg[i], key[5] - '1', m->tag, sizeof m->tag)) { m->detail[0] = 0; m->menu = -1; m->page = 0; m->q[0] = 0; }
    } else if (key[0] == 'd' && key[1] == '_') {   /* d_act, d_more, d_reinstall, d_remove */
        int i = detail_of(m);
        if (i >= 0 && !busy) act(m, i, key + 2);
    } else if (!strcmp(key, "page_prev") && !m->detail[0] && m->page > 0) { m->page--; m->menu = -1; }   /* hidden by the details */
    else if (!strcmp(key, "page_next") && !m->detail[0] && m->page + 1 < pages) { m->page++; m->menu = -1; }
    else if (!strcmp(key, "refresh")) post(m, J_REFRESH);
    else if (!strcmp(key, "update_all") && !busy) {
        for (int i = 0; i < m->npkg; i++) if (has_update(&m->pkg[i]) && !m->pkg[i].queued) requeue(m, &m->pkg[i], Q_INSTALL);
    } else if (!strcmp(key, "apply") && !busy) {
        if (m->ready) m->confirm = 1;   /* restart only from the prompt */
        else if (count_queued(m) && !m->dev->problem[0]) post(m, J_PREPARE);
    } else if (key[0] == 'r' && key[1] >= '1' && key[1] <= '0' + ROWS && key[2] == '_' && !m->detail[0]) {
        int k = m->page * ROWS + key[1] - '1';
        const char *f = key + 3;
        if (k < nv && !strcmp(f, "open")) {   /* the thumbnail */
            m->sel = idx[k];
            m->menu = -1;
            snprintf(m->detail, sizeof m->detail, "%s", m->pkg[idx[k]].id);
        } else if (k < nv && !busy) act(m, idx[k], f);
    }
    m->rev++;
    pthread_mutex_unlock(&m->mu);
}

static int mgr_get_param(void *inst, const char *key, char *b, int n) {
    mgr_t *m = inst;
    size_t kl = strlen(key);
    /* The wrapper asks every text param for "<key>_on" (a list tile's selection) every 10 ms until it is told there is
     * none: only the cards' titles have one. Answering the others kept ~60 lookups running every 10 ms. */
    if (kl > 3 && !strcmp(key + kl - 3, "_on") && !(!strncmp(key, "card", 4) && key[4] >= '1' && key[4] <= '0' + ROWS)) return 0;
    pthread_mutex_lock(&m->mu);
    int idx[MAXPKG], nv = visible(m, idx), pages = nv ? (nv + ROWS - 1) / ROWS : 1, ok = 1;
    if (m->page >= pages) m->page = pages - 1;
    int upd = count_updates(m), q = count_queued(m), busy = m->busy == J_PREPARE || m->busy == J_LAUNCH;
    int d = detail_of(m);
    if (d < 0) m->detail[0] = 0;   /* gone from the catalog on a refresh */
    if (!strcmp(key, "display_rev")) snprintf(b, n, "%u", m->rev);
    else if (!strcmp(key, "tab")) snprintf(b, n, "%d", m->tab);
    else if (!strcmp(key, "kind")) snprintf(b, n, "%d", m->kindf);
    else if (!strcmp(key, "sort")) snprintf(b, n, "%d", m->sort);
    else if (!strcmp(key, "kind_open")) snprintf(b, n, "%d", m->menu_open == 1);
    else if (!strcmp(key, "sort_open")) snprintf(b, n, "%d", m->menu_open == 2);
    else if (!strcmp(key, "kind_hl")) snprintf(b, n, "%d", m->menu_open == 1 ? 1 + (m->kindf & 3) : 0);   /* the open menu's current row */
    else if (!strcmp(key, "sort_hl")) snprintf(b, n, "%d", m->menu_open == 2 ? 1 + (m->sort & 3) : 0);
    else if (!strcmp(key, "kind_txt")) snprintf(b, n, "%s", (const char *[]){"ALL", "INSTRUMENTS", "EFFECTS", "ADDINS"}[m->kindf & 3]);
    else if (!strcmp(key, "sort_txt")) snprintf(b, n, "%s", (const char *[]){"UPDATED", "NEWEST", "DOWNLOADS", "NAME"}[m->sort & 3]);
    else if (!strcmp(key, "dd")) snprintf(b, n, "%d", m->tag[0] || m->q[0] || !m->dev->problem[0]);   /* the dropdowns: not over the problem banner */
    else if (!strcmp(key, "net")) snprintf(b, n, "%d", !m->online);
    else if (!strcmp(key, "disk")) snprintf(b, n, "%d", (int)(m->disk_used * 10 + 0.5));   /* bar steps 0..10 */
    else if (!strcmp(key, "disk_txt")) {
        if (ntargets(m) > 1 && m->disk_free[0])   /* more than one place to install to: name it, and the ▾ says it opens a menu */
            snprintf(b, n, "%s \xc2\xb7 %.*s \xe2\x96\xbe", target_name(m, m->tsel % ntargets(m)),
                     (int)(strstr(m->disk_free, " free") ? strstr(m->disk_free, " free") - m->disk_free : (long)strlen(m->disk_free)), m->disk_free);
        else snprintf(b, n, "%s", m->disk_free[0] ? m->disk_free : " ");
    }
    else if (!strcmp(key, "drv_open")) snprintf(b, n, "%d", m->menu_open == 3);
    else if (!strcmp(key, "drv_hl")) snprintf(b, n, "%d", m->menu_open == 3 && ntargets(m) ? 1 + m->tsel % ntargets(m) : 0);
    else if (!strncmp(key, "drv", 3) && key[3] >= '1' && key[3] <= '4' && !strcmp(key + 4, "_txt")) {
        int k = key[3] - '1';
        if (k < ntargets(m)) {
            struct statvfs dv;
            char r[220];
            snprintf(r, sizeof r, "%s", target_at(m, k));
            char *sl = strrchr(r, '/');
            if (!is_dir(r) && sl && sl != r) *sl = 0;   /* "VST2 Plugins" not made yet: its drive */
            if (statvfs(r, &dv) == 0) snprintf(b, n, "%s  \xc2\xb7  %.1f GB free", target_name(m, k), (double)dv.f_bavail * dv.f_frsize / 1e9);
            else snprintf(b, n, "%s", target_name(m, k));
        } else snprintf(b, n, " ");
    }
    else if (!strncmp(key, "loc", 3) && key[3] >= '1' && key[3] <= '4' && key[4] == '_') {   /* LOCATIONS tab, rows 1..4 of the page */
        int ri = m->locpage * 4 + key[3] - '1';
        const locrow_t *r = ri < m->nlocs ? &m->locs[ri] : NULL;
        const char *f = key + 5;
        int queued = 0;
        for (int q = 0; r && q < m->nregq; q++) queued |= !strcmp(m->regq[q], r->path);
        if (!strcmp(f, "vis")) snprintf(b, n, "%d", r != NULL);
        else if (!strcmp(f, "def")) snprintf(b, n, "%d", r && r->dflt);
        else if (!strcmp(f, "canpick")) snprintf(b, n, "%d", r && r->pick);
        else if (!strcmp(f, "canreg")) snprintf(b, n, "%d", r && r->cand && r->usable && m->dev->hasloclist ? 1 + queued : 0);   /* off, register, queued */
        else if (!strcmp(f, "bar")) snprintf(b, n, "%d", r ? r->bar : 0);
        else if (!strcmp(f, "ok")) snprintf(b, n, "%d", !r ? 0 : r->cand && r->usable ? 3 : r->usable ? 1 : 2);   /* none, yes, no, not listed */
        else if (!strcmp(f, "name")) snprintf(b, n, "%s", r ? r->name : " ");
        else if (!strcmp(f, "info")) snprintf(b, n, "%s", r ? r->info : " ");
        else if (!strcmp(f, "state")) snprintf(b, n, "%s", r ? (queued ? "Queued: press APPLY on the PLUGINS tab (MPC restarts)" : r->state) : " ");
        else ok = 0;
    }
    else if (!strncmp(key, "pat", 3) && key[3] >= '1' && key[3] <= '3' && key[4] == '_') {   /* PATCHES tab, rows 1..3 of the page */
        int pi = m->ptpage * 3 + key[3] - '1';
        const patch_t *p = pi < m->npt ? &m->pt[pi] : NULL;
        const char *f = key + 5;
        if (!strcmp(f, "vis")) snprintf(b, n, "%d", p != NULL);
        else if (!strcmp(f, "title")) snprintf(b, n, "%s", p ? p->title : " ");
        else if (!strcmp(f, "s1") || !strcmp(f, "s2")) { if (p) wrap_line(p->summary, f[1] - '1', 2, 104, b, n); else snprintf(b, n, " "); }
        else if (!strcmp(f, "meta")) {
            if (p) snprintf(b, n, "Changes %s  \xc2\xb7  %s  \xc2\xb7  %s  \xc2\xb7  by %s", p->modifies[0] ? p->modifies : "?",
                            p->restarts ? "restarts MPC" : "no restart", p->reversible ? "can be undone" : "not reversible", p->author);
            else snprintf(b, n, " ");
        }
        else if (!strcmp(f, "for")) {
            if (!p) snprintf(b, n, " ");
            else if (!pt_arch_ok(m, p)) snprintf(b, n, "Not for this device: built for %s, this MPC is %s", p->arch, m->dev->arch);
            else if (p->checked) snprintf(b, n, "Checked on this MPC: %s%s%s%s", p->state, p->reason[0] ? "  \xc2\xb7  " : "", p->reason,
                                          p->restarts && (p->supported || !strcmp(p->state, "patched")) ? "  \xc2\xb7  restarts MPC: use the installer app" : "");
            else snprintf(b, n, "For: %.108s", p->os);
        }
        else if (!strcmp(f, "pill"))   /* none, not checked, stock (can apply), applied, not supported, other device, check failed */
            snprintf(b, n, "%d", !p ? 0 : !pt_arch_ok(m, p) ? 5 : !p->checked ? 1 : !strcmp(p->state, "error") ? 6
                                  : p->supported && !strcmp(p->state, "stock") ? 2 : !strcmp(p->state, "patched") ? 3 : 4);
        else if (!strcmp(f, "canact")) {   /* off, apply, undo, confirm apply, confirm undo */
            int kind = pt_act_kind(m, p);
            int want = 1 + pi * 2 + (kind == 2);
            snprintf(b, n, "%d", !kind ? 0 : m->ptconf == want && mono_ms() - m->ptconf_t < 6000 ? 2 + kind : kind);
        }
        else if (!strcmp(f, "canchk")) snprintf(b, n, "%d", p && pt_arch_ok(m, p) ? 1 + p->checked : 0);   /* off, check, check again */
        else ok = 0;
    }
    else if (!strcmp(key, "pat_pages")) snprintf(b, n, "%d", m->npt > 3);
    else if (!strcmp(key, "pat_page")) snprintf(b, n, "%d / %d", m->ptpage + 1, m->npt ? (m->npt + 2) / 3 : 1);
    else if (!strcmp(key, "pat_foot")) snprintf(b, n, m->npt ? "%d device patches. CHECK runs a patch's status on this MPC and changes nothing." : "No patch list yet (needs the network).", m->npt);
    else if (!strcmp(key, "set_theme_txt")) snprintf(b, n, "%s%s", m->theme ? "LIGHT" : "DARK", theme_dirty(m) ? "  \xc2\xb7  restart" : "");
    else if (!strcmp(key, "set_beta_txt")) snprintf(b, n, "%s", m->show_beta ? "SHOWN" : "HIDDEN");
    else if (!strcmp(key, "set_cache_txt")) {
        int cp = cache_used_pct();
        char u[24] = "unknown";
        if (cp >= 0) snprintf(u, sizeof u, "%d%% used", cp);
        snprintf(b, n, "%s%s", u, m->clearcache ? "  \xc2\xb7  clears on APPLY" : cp >= 85 ? "  \xc2\xb7  FULL" : "");
    }
    else if (!strcmp(key, "set_web_txt")) snprintf(b, n, "%s%s", m->web_pref ? "ON" : "OFF", m->web_pref != m->web_on ? "  \xc2\xb7  restart" : "");
    else if (!strcmp(key, "loc_pages")) snprintf(b, n, "%d", m->nlocs > 4);
    else if (!strcmp(key, "loc_page")) snprintf(b, n, "%d / %d", m->locpage + 1, m->nlocs ? (m->nlocs + 3) / 4 : 1);
    else if (!strcmp(key, "loc_foot")) {
        int unl = 0;
        for (int i = 0; i < m->nlocs; i++) unl += m->locs[i].cand;
        if (!m->dev->nloc && !unl) snprintf(b, n, "MPC lists no Synths folder");
        else if (unl) snprintf(b, n, "%d listed by MPC  \xc2\xb7  %d drive%s not listed", m->dev->nloc, unl, unl == 1 ? "" : "s");
        else snprintf(b, n, "%d Synths folder%s listed in MPC.settings", m->dev->nloc, m->dev->nloc == 1 ? "" : "s");
    }
    else if (!strcmp(key, "upd_badge")) snprintf(b, n, "%d", upd > 0);
    else if (!strcmp(key, "upd_count")) snprintf(b, n, "%d", upd);
    else if (!strcmp(key, "problem")) snprintf(b, n, "%d", m->dev->problem[0] != 0);
    else if (!strcmp(key, "problem_txt"))
        problem_words(m->dev, b, n);
    else if (!strcmp(key, "empty")) snprintf(b, n, "%d", d >= 0 ? 0 : !m->loaded ? (m->online || m->busy == J_REFRESH ? 0 : 1) : nv == 0 ? 2 : 0);
    else if (!strcmp(key, "empty_txt")) {
        int addins = 0;
        for (int i = 0; i < m->npkg; i++) addins += is_addin(&m->pkg[i]) && m->pkg[i].url[0];
        snprintf(b, n, "%s", !m->online ? "Offline, and no zips in local-plugins yet"
                             : m->tab == 2 ? "Everything is up to date" : m->tab == 1 ? "No catalog plugins installed yet"
                             : m->kindf == 3 && !addins ? "No addins in the catalog yet" : "Everything here is already installed");
    }
    else if (!strcmp(key, "nav_prev")) snprintf(b, n, "%d", d < 0 && m->page > 0);
    else if (!strcmp(key, "nav_next")) snprintf(b, n, "%d", d < 0 && m->page + 1 < pages);
    else if (!strcmp(key, "page_txt")) snprintf(b, n, "%d / %d", m->page + 1, pages);
    else if (!strcmp(key, "summary") && m->q[0])
        snprintf(b, n, "%d match%s for \xe2\x80\x9c%s\xe2\x80\x9d  \xc2\xb7  %d update%s available", nv, nv == 1 ? "" : "es", m->q, upd, upd == 1 ? "" : "s");
    else if (!strcmp(key, "summary") && m->tag[0])
        snprintf(b, n, "%d %s%s tagged \xe2\x80\x9c%s\xe2\x80\x9d  \xc2\xb7  %d update%s available", nv, m->kindf == 3 ? "addin" : "plugin",
                 nv == 1 ? "" : "s", m->tag, upd, upd == 1 ? "" : "s");
    else if (!strcmp(key, "summary"))
        snprintf(b, n, "%d %s%s %s  \xc2\xb7  %d update%s available", nv, m->kindf == 3 ? "addin" : "plugin", nv == 1 ? "" : "s",
                 m->tab == 1 ? "installed" : m->tab == 2 ? "to update" : "available", upd, upd == 1 ? "" : "s");
    else if (!strcmp(key, "status")) {
        if (m->sticky) snprintf(b, n, "%s", m->status);
        else if (m->tag[0] && !q) snprintf(b, n, "Tap the tag at the top to see everything again.");
        else if (m->q[0] && !q) snprintf(b, n, "Tap \xc3\x97 on the search at the top to see everything again.");
        else queue_text(m, b, n);
    }
    else if (!strcmp(key, "status_kind")) snprintf(b, n, "%d", m->sticky ? m->status_kind : OK);
    else if (!strcmp(key, "busy")) snprintf(b, n, "%d", m->downloading);
    else if (!strcmp(key, "progress"))   /* bar steps 1..20 while downloading, 0 (no bar) otherwise */
        snprintf(b, n, "%d", m->downloading ? 1 + (int)(m->progress * 19 + 0.5) : 0);
    else if (!strcmp(key, "upd_all")) snprintf(b, n, "%d", upd > 0 && !busy);
    else if (!strcmp(key, "apply_state"))   /* 0 off, 1 apply, 2 preparing, 3 ready (restart), 4 retry */
        snprintf(b, n, "%d", busy ? 2 : m->ready ? 3 : m->failed && q ? 4 : q && !m->dev->problem[0] ? 1 : 0);
    else if (!strncmp(key, "card", 4) && key[4] >= '1' && key[4] <= '0' + ROWS) {   /* card<r>_1: the name, and its selection */
        int k = m->page * ROWS + key[4] - '1', i = k < nv && d < 0 ? idx[k] : -1;
        if (strstr(key, "_on")) snprintf(b, n, "%d", i >= 0 && i == m->sel);
        else snprintf(b, n, "%s", i >= 0 ? m->pkg[i].name : " ");
    } else if (key[0] == 'r' && key[1] >= '1' && key[1] <= '0' + ROWS && key[2] == '_') {
        int k = m->page * ROWS + key[1] - '1';
        card_field(m, k < nv && d < 0 ? idx[k] : -1, key + 3, b, n);
    } else if (!strcmp(key, "view")) snprintf(b, n, "%d", d >= 0);
    else if (!strcmp(key, "confirm")) snprintf(b, n, "%d", m->confirm && m->ready && !busy);
    else if (!strcmp(key, "theme_can")) snprintf(b, n, "%d", m->theme_can);
    else if (!strcmp(key, "theme")) snprintf(b, n, "%d", m->theme);
    else if (!strcmp(key, "theme_txt")) snprintf(b, n, "%s%s", m->theme ? "LIGHT" : "DARK", theme_dirty(m) ? "  after restart" : "");
    else if (!strcmp(key, "confirm_pick")) snprintf(b, n, "%d", m->confirm && m->ready && !busy && ntargets(m) > 1 && installs_queued(m));
    else if (!strcmp(key, "confirm_dest")) {
        if (!installs_queued(m)) snprintf(b, n, " ");   /* only a register, a move or a theme: nothing is installed */
        else if (ntargets(m) > 1) snprintf(b, n, "Install to: %s  (%d of %d)", dest(m), m->tsel % ntargets(m) + 1, ntargets(m));
        else snprintf(b, n, "Install to: %s", dest(m));
    }
    else if (!strcmp(key, "confirm_txt1") || !strcmp(key, "confirm_txt2")) {
        char c[1024];
        changes_text(m, c, sizeof c);
        wrap_line(c, key[11] - '1', 2, 58, b, n);
    }
    else if (!strcmp(key, "hdr")) snprintf(b, n, "%d", m->tag[0] ? 2 : m->q[0] ? 3 : m->dev->problem[0] ? 1 : 0);   /* search, problem, tag, query */
    else if (!strcmp(key, "kb")) snprintf(b, n, "%d", m->kb);
    else if (!strcmp(key, "q_txt")) snprintf(b, n, "%s|", m->q);
    else if (!strcmp(key, "q_chip")) snprintf(b, n, "%s", m->q[0] ? m->q : " ");
    else if (!strcmp(key, "kb_count")) {
        if (!m->kb) snprintf(b, n, " ");
        else if (!m->q[0]) snprintf(b, n, "Type part of a name, author or tag");
        else snprintf(b, n, "%d match%s in name, author and tags%s", nv, nv == 1 ? "" : "es", nv > 2 ? "  \xc2\xb7  DONE shows them all" : "");
    }
    else if (!strncmp(key, "sr", 2) && (key[2] == '1' || key[2] == '2') && key[3] == '_') {   /* the keyboard's match rows */
        int k = key[2] - '1', i = m->kb && m->q[0] && k < nv ? idx[k] : -1;
        const char *f = key + 4;
        if (!strcmp(f, "vis")) snprintf(b, n, "%d", i >= 0);
        else if (!strcmp(f, "state")) snprintf(b, n, "%d", i >= 0 ? pkg_state(m, i) : 0);
        else if (i < 0) snprintf(b, n, !strcmp(f, "act") || !strcmp(f, "open") ? "0" : " ");
        else if (!strcmp(f, "init")) initials(m->pkg[i].name, b);
        else if (!strcmp(f, "name")) snprintf(b, n, "%s", m->pkg[i].name);
        else if (!strcmp(f, "meta")) {
            const pkg_t *p = &m->pkg[i];
            char st[32], tg[96];
            spaced(p->style, st, sizeof st);
            spaced(p->tags, tg, sizeof tg);
            snprintf(b, n, "%s%s%s%s%s", p->author, st[0] ? "  \xc2\xb7  " : "", st, tg[0] ? "  \xc2\xb7  " : "", tg);
        } else snprintf(b, n, "0");
    }
    else if (!strcmp(key, "tag_txt")) snprintf(b, n, "#%s", m->tag);
    else if (!strcmp(key, "d_shot")) snprintf(b, n, "%d", d >= 0 ? 1 + shot(m->pkg[d].id) : 0);   /* 0 hidden, 1 none */
    else if (key[0] == 'd' && key[1] == '_' && d < 0)   /* no detail view: option params at 0 (hidden), text blank */
        snprintf(b, n, !strcmp(key, "d_state") || !strcmp(key, "d_inst") || !strcmp(key, "d_cpuk") || !strcmp(key, "d_testk") ||
                       !strcmp(key, "d_wnk") || (!strncmp(key, "d_tag", 5) && key[5] != 't') || !strcmp(key, "d_more") ? "0" : " ");
    else if (!strcmp(key, "d_name")) snprintf(b, n, "%s", m->pkg[d].name);
    else if (!strcmp(key, "d_init") || !strcmp(key, "d_inst")) card_field(m, d, key + 2, b, n);
    else if (!strcmp(key, "d_state")) snprintf(b, n, "%d", card_state(m, d));
    else if (!strcmp(key, "d_by")) {
        const pkg_t *p = &m->pkg[d];
        char st[32];
        spaced(p->style, st, sizeof st);
        snprintf(b, n, "by %s  \xc2\xb7  %s%s%s", p->author, is_addin(p) ? "Addin" : !strcmp(p->kind, "effect") ? "Effect" : "Instrument",
                 st[0] ? "  \xc2\xb7  " : "", st);
    }
    else if (!strcmp(key, "d_f_version")) snprintf(b, n, "%s", m->pkg[d].latest);
    else if (!strcmp(key, "d_f_channel")) snprintf(b, n, "%s", !strcmp(m->pkg[d].channel, "beta") ? "Beta" : "Stable");
    else if (!strcmp(key, "d_f_size")) {
        const pkg_t *p = &m->pkg[d];
        if (p->size >= 1000000) snprintf(b, n, "%.1f MB", p->size / 1e6);
        else snprintf(b, n, "%lld KB", p->size / 1000);
    } else if (!strcmp(key, "d_f_released")) {   /* "2026-10-04" -> "4 Oct 2026" */
        static const char *mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        int y, mo, dd;
        if (sscanf(m->pkg[d].date, "%d-%d-%d", &y, &mo, &dd) == 3 && mo >= 1 && mo <= 12) snprintf(b, n, "%d %s %d", dd, mon[mo - 1], y);
        else snprintf(b, n, "\xe2\x80\x94");
    } else if (!strcmp(key, "d_f_downloads")) snprintf(b, n, "%d", m->pkg[d].downloads);
    else if (!strcmp(key, "d_cpuk")) {   /* 0 hidden, 1 pass, 2 warn, 3 fail, 4 no bench */
        const char *c = m->pkg[d].cpu;
        snprintf(b, n, "%d", !strcmp(c, "PASS") ? 1 : !strcmp(c, "WARN") ? 2 : !strcmp(c, "FAIL") ? 3 : 4);
    } else if (!strcmp(key, "d_f_cpu")) {
        const pkg_t *p = &m->pkg[d];
        if (p->cpu[0]) snprintf(b, n, "%s%s%s", p->cpu_pct, p->cpu_pct[0] ? "  \xc2\xb7  " : "", p->cpu);
        else snprintf(b, n, "no bench");
    } else if (!strcmp(key, "d_testk")) snprintf(b, n, "%d", m->pkg[d].tested[0] ? 1 : 2);   /* 0 hidden, 1 yes, 2 not yet */
    else if (!strcmp(key, "d_f_tested")) {
        const char *t = m->pkg[d].tested;
        snprintf(b, n, "%s", !t[0] ? "not yet" : !strncmp(t, "Akai ", 5) ? t + 5 : t);
    } else if (!strncmp(key, "d_sum", 5) && key[5] >= '1' && key[5] <= '4')
        wrap_line(m->pkg[d].summary, key[5] - '1', 4, 100, b, n);
    else if (!strcmp(key, "d_wnk")) snprintf(b, n, "%d", m->pkg[d].wn_kind);   /* 0 hidden, 1 notes, 2 fallback */
    else if (!strcmp(key, "d_wn_head")) snprintf(b, n, "WHAT'S NEW IN %s", m->pkg[d].latest);
    else if (!strcmp(key, "d_tags_head")) snprintf(b, n, "%s", m->pkg[d].alltags[0] ? "TAGS  \xc2\xb7  TAP ONE TO FIND SIMILAR PLUGINS" : " ");
    else if (!strncmp(key, "d_wn", 4) && key[4] >= '1' && key[4] <= '3')
        wrap_line(m->pkg[d].whatsnew, key[4] - '1', 3, 108, b, n);
    else if (!strncmp(key, "d_tag", 5)) {   /* d_tag<k>: the button, d_tag<k>v: shown, d_tagt<k>: its text */
        char t[40];
        int k = key[5] == 't' ? key[6] - '1' : key[5] - '1', has = k >= 0 && k < 8 && nth_tag(&m->pkg[d], k, t, sizeof t);
        if (key[5] == 't') snprintf(b, n, "#%s", has ? t : "");
        else snprintf(b, n, "%d", key[6] == 'v' && has);
    }
    else if (!strcmp(key, "refresh") || !strcmp(key, "page_prev") || !strcmp(key, "page_next") ||
               !strcmp(key, "update_all") || !strcmp(key, "apply") || !strcmp(key, "noop") || !strcmp(key, "d_back") ||
               !strcmp(key, "d_act") || !strcmp(key, "d_more") || !strcmp(key, "d_reinstall") || !strcmp(key, "d_remove") ||
               !strcmp(key, "tag_clear") || !strcmp(key, "confirm_cancel") || !strcmp(key, "confirm_go") || !strcmp(key, "target_next") || !strcmp(key, "theme_btn") ||
               !strcmp(key, "menu_close") || !strcmp(key, "kind_btn") || !strcmp(key, "sort_btn") ||
               !strcmp(key, "search_btn") || !strcmp(key, "q_clear") || !strncmp(key, "k_", 2) ||
               ((!strncmp(key, "kind_pick", 9) || !strncmp(key, "sort_pick", 9)) && key[9])) snprintf(b, n, "0");
    else ok = 0;
    pthread_mutex_unlock(&m->mu);
    return ok;
}

static void mgr_midi(void *inst, const uint8_t *msg, int len) { (void)inst, (void)msg, (void)len; }
static void mgr_render(void *inst, int16_t *out, int frames) { (void)inst; memset(out, 0, sizeof(int16_t) * 2 * frames); }

static const mpc_engine_t ENGINE = {mgr_create, mgr_destroy, mgr_midi, mgr_set_param, mgr_get_param, mgr_render, NULL};
__attribute__((visibility("default"))) const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

/* Test engine for the wrapper's state chunk size (STATE_MAX, wrapper/vst2_wrap.c; checked by tools/host_test.c CHUNK_PROBE):
 * its "state" is whatever text was last set (any length), so the test can push a chunk of any size through effSetChunk and
 * read it back through effGetChunk. get_param("state") copies at most buf_len - 1 bytes plus a NUL, as engine.h asks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../wrapper/engine.h"

typedef struct { char level[32]; char *state; } ct_t;

static void *create(const char *d) { (void)d; ct_t *c = calloc(1, sizeof *c); strcpy(c->level, "0.0"); return c; }
static void destroy(void *i) { ct_t *c = i; free(c->state); free(c); }
static void midi(void *i, const uint8_t *m, int n) { (void)i; (void)m; (void)n; }
static void set_param(void *i, const char *k, const char *v) {
    ct_t *c = i;
    if (!strcmp(k, "level")) snprintf(c->level, sizeof c->level, "%s", v);
    else if (!strcmp(k, "state")) { free(c->state); c->state = strdup(v); }
}
static int get_param(void *i, const char *k, char *b, int n) {
    ct_t *c = i;
    if (!strcmp(k, "level")) return snprintf(b, n, "%s", c->level);
    if (!strcmp(k, "state") && c->state) { snprintf(b, n, "%s", c->state); return (int)strlen(b) + 1; }
    return 0;
}
static void render(void *i, int16_t *o, int f) { (void)i; memset(o, 0, (size_t)f * 4); }

static const mpc_engine_t ENGINE = { create, destroy, midi, set_param, get_param, render, NULL };
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }

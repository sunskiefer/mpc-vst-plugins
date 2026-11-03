/* Drives a released Plugin Manager G2 (its src/manager.c from a git tag, tools/compat/run.sh) against a catalog and prints
 * what a user would see: the catalog summary, any banner, then every card on every page of the Discover, Installed and
 * Updates tabs (name, version, kind, state, size). Same output for the same catalog, so two catalogs can be compared.
 * Only reads: never presses APPLY or an install button. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
typedef struct { void *(*create)(const char *); void (*destroy)(void *); void (*midi)(void *, const uint8_t *, int);
  void (*set_param)(void *, const char *, const char *); int (*get_param)(void *, const char *, char *, int);
  void (*render)(void *, int16_t *, int); void *process; } E;
const E *mpc_engine(void);
static const E *e;
static void *m;
static const char *get(const char *k) { static char b[32][160]; static int i; i = (i + 1) % 32; b[i][0] = 0; e->get_param(m, k, b[i], 160); return b[i]; }
static void set(const char *k, const char *v) { e->set_param(m, k, v); }
static void page_next(void) { usleep(300 * 1000); set("page_next", "1"); set("page_next", "0"); }

static void tab(const char *t, const char *name) {
  set("tab", t); usleep(200 * 1000);
  printf("== %s: %s\n", name, get("summary"));
  char seen[32] = "";
  for (int p = 0; p < 200; p++) {
    const char *pt = get("page_txt");
    if (!strcmp(pt, seen)) break;   /* the last page: page_next did nothing */
    snprintf(seen, sizeof seen, "%s", pt);
    for (int r = 1; r <= 5; r++) {
      char k[32];
      #define F(f) (snprintf(k, sizeof k, "r%d_" f, r), get(k))
      if (!strcmp(F("vis"), "0")) continue;
      snprintf(k, sizeof k, "card%d_1", r);
      const char *name = get(k);
      printf("%s | %s | %s | state=%s | %s | %s\n", name, F("ver"), F("kindtxt"), F("state"), F("size"), F("chan"));
      /* its details page: author, the first summary line, tags */
      snprintf(k, sizeof k, "r%d_open", r); usleep(60 * 1000); set(k, "1"); set(k, "0");
      printf("    by %s | %s | tags:", get("d_by"), get("d_sum1"));
      for (int t = 1; t <= 8; t++) { char a[12], b[12]; snprintf(a, sizeof a, "d_tag%dv", t); snprintf(b, sizeof b, "d_tagt%d", t); if (!strcmp(get(a), "1")) printf(" %s", get(b)); }
      printf("\n");
      usleep(60 * 1000); set("d_back", "1"); set("d_back", "0");
    }
    page_next();
  }
}

int main(void) {
  e = mpc_engine(); m = e->create(NULL);
  int i;
  for (i = 0; i < 90 && !strstr(get("summary"), "available"); i++) sleep(1);
  if (i == 90) { printf("FAIL: the catalog never loaded: %s | %s\n", get("summary"), get("status")); return 1; }
  sleep(1);
  printf("net=%s problem=%s (%s)\n", get("net"), get("problem"), get("problem_txt"));
  set("kind", "0"); set("sort", "0");
  tab("0", "DISCOVER"); tab("1", "INSTALLED"); tab("2", "UPDATES");
  e->destroy(m);
  return 0;
}

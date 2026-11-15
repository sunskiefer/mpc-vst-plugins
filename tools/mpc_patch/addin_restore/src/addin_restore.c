/*
 * addin_restore.c: the native half of the Addin Restore device script (addin_restore.lua). MPC runs device scripts under
 * "<content root>/MPC Documents/Devices/" at every start; the script package.loadlib()s this library and calls
 * addin_restore_check(). On MPC OS 3.10 the boot work that has to come back after a reboot lives in /run (tmpfs), which a
 * reboot empties, and the root is read-only on Gen2, so this is the one hook that survives a reboot without a stick or SSH.
 *
 * It brings back, once per boot (guard: /run/addin-restore-done):
 *  1. Addins: every /data/mpc-addins/<name>/<first *.so> that MPC's LD_PRELOAD lacks goes into
 *     /run/systemd/system/acvs.service.d/90-mpc-addins.conf (MPC's current LD_PRELOAD first, then the missing ones, ':'
 *     between). Same behaviour as the 1.1.0 library, whose source was not published; rewritten from what it does.
 *  2. Since 1.2.0, the 16-pad drum layout patch on Gen2: that patch runs a patched copy of MPC (/data/mpc-drumpad/MPC)
 *     through a /run drop-in, 94-mpc-drumpad.conf. When the copy is installed and the drop-in is gone, the patch's own
 *     saved script runs `reapply`, which writes the drop-in only if the copy was made from the MPC this system runs (after
 *     a firmware update it leaves MPC stock). That script's checksum of the 130 MB MPC takes a moment, so it runs in the
 *     detached child, not on MPC's Lua thread.
 * If either wrote a drop-in, MPC restarts once (8 s after the check), so the user sees one restart after a reboot.
 *
 * Build: tools/mpc_patch/addin_restore/build.sh (aarch64 and armv7, glibc 2.31 floor). Host tests: build.sh test
 * (AR_TEST: the paths sit under $AR_ROOT, MPC's environment is read from $AR_ROOT/environ, no sleeping).
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_ADDINS 16
#define RESTART_AFTER 8   /* seconds after the check, as in 1.1.0 */

static char root[256];   /* "" on the device; $AR_ROOT in tests */
static void path(char *out, size_t n, const char *p) { snprintf(out, n, "%s%s", root, p); }

#define LOG_FILE   "/tmp/addin-restore.log"
#define GUARD      "/run/addin-restore-done"
#define ADDINS     "/data/mpc-addins"
#define DROPIN_DIR "/run/systemd/system/acvs.service.d"
#define ADDIN_CONF DROPIN_DIR "/90-mpc-addins.conf"
#define DP_DIR     "/data/mpc-drumpad"
#define DP_SCRIPT  DP_DIR "/mpc-drum-pad-patch.sh"
#define DP_CONF    DROPIN_DIR "/94-mpc-drumpad.conf"

static void logmsg(const char *fmt, const char *a) {
    char p[300];
    path(p, sizeof p, LOG_FILE);
    FILE *f = fopen(p, "a");
    if (!f) return;
    fputs("[addin-restore] ", f);
    fprintf(f, fmt, a ? a : "");
    fputc('\n', f);
    fclose(f);
}

static int exists(const char *p) { char q[300]; path(q, sizeof q, p); return access(q, F_OK) == 0; }

static void put_file(const char *p, const char *body) {
    char q[300];
    path(q, sizeof q, p);
    FILE *f = fopen(q, "w");
    if (f) { fputs(body, f); fclose(f); }
}

/* MPC's LD_PRELOAD (this library runs inside MPC). "" when it has none. */
static void ld_preload(char *out, size_t n) {
    char p[300];
    out[0] = 0;
#ifdef AR_TEST
    path(p, sizeof p, "/environ");
#else
    snprintf(p, sizeof p, "/proc/self/environ");
#endif
    FILE *f = fopen(p, "r");
    if (!f) return;
    char var[8192];
    size_t k = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c && k + 1 < sizeof var) { var[k++] = (char)c; continue; }
        var[k] = 0;
        if (!strncmp(var, "LD_PRELOAD=", 11)) { snprintf(out, n, "%s", var + 11); break; }
        k = 0;
    }
    if (c == EOF && k) { var[k] = 0; if (!strncmp(var, "LD_PRELOAD=", 11)) snprintf(out, n, "%s", var + 11); }
    fclose(f);
}

/* /data/mpc-addins/<name>/<first *.so>, for up to MAX_ADDINS names that do not start with a dot. Paths are device paths. */
static int find_addins(char list[][640]) {
    char d[300];
    path(d, sizeof d, ADDINS);
    DIR *top = opendir(d);
    if (!top) return 0;
    int n = 0;
    struct dirent *e;
    while (n < MAX_ADDINS && (e = readdir(top))) {
        if (e->d_name[0] == '.') continue;
        char sub[600];
        snprintf(sub, sizeof sub, "%s/%s", d, e->d_name);
        DIR *s = opendir(sub);
        if (!s) continue;
        struct dirent *f;
        while ((f = readdir(s))) {
            size_t l = strlen(f->d_name);
            if (l > 3 && !strcmp(f->d_name + l - 3, ".so")) {
                int w = snprintf(list[n], 640, ADDINS "/%s/%s", e->d_name, f->d_name);
                if (w > 0 && w < 640) n++;   /* a path that does not fit would be a wrong path in LD_PRELOAD: skipped */
                break;
            }
        }
        closedir(s);
    }
    closedir(top);
    return n;
}

/* Writes 90-mpc-addins.conf when an addin is missing from LD_PRELOAD. 1 when it wrote it. */
static int restore_addins(void) {
    static char list[MAX_ADDINS][640];
    int n = find_addins(list);
    if (!n) { logmsg("no addins found in " ADDINS, NULL); return 0; }
    char cur[8192], num[16];
    ld_preload(cur, sizeof cur);
    int missing = 0;
    for (int i = 0; i < n; i++)
        if (!strstr(cur, list[i])) { missing++; logmsg("NOT loaded: %s", list[i]); }
    snprintf(num, sizeof num, "%d", n);
    logmsg("found %s addin(s)", num);
    if (!missing) { logmsg("all addins loaded, nothing to do", NULL); return 0; }
    char dir[300], conf[300];
    path(dir, sizeof dir, "/run/systemd"); mkdir(dir, 0755);
    path(dir, sizeof dir, "/run/systemd/system"); mkdir(dir, 0755);
    path(dir, sizeof dir, DROPIN_DIR); mkdir(dir, 0755);
    path(conf, sizeof conf, ADDIN_CONF);
    FILE *f = fopen(conf, "w");
    if (!f) { logmsg("cannot write drop-in", NULL); return 0; }
    fputs("[Service]\nEnvironment=LD_PRELOAD=", f);
    fputs(cur, f);
    int first = !cur[0];
    for (int i = 0; i < n; i++) {
        if (strstr(cur, list[i])) continue;
        if (!first) fputc(':', f);
        fputs(list[i], f);
        first = 0;
    }
    fputc('\n', f);
    fclose(f);
    logmsg("addins drop-in written", NULL);
    return 1;
}

/* The 16-pad patch is installed on Gen2 (its copy and saved script are there) and its drop-in is gone. */
static int drumpad_wanted(void) {
    return exists(DP_SCRIPT) && exists(DP_DIR "/MPC") && exists(DP_DIR "/STOCK") && !exists(DP_CONF);
}

/* In the child: the patch's own `reapply` (it checks that the copy fits this MPC). 1 when its drop-in is now there. */
static int drumpad_reapply(void) {
    char s[300], log[300], cmd[1024];
    path(s, sizeof s, DP_SCRIPT);
    path(log, sizeof log, LOG_FILE);
#ifdef AR_TEST
    snprintf(cmd, sizeof cmd, "AR_ROOT='%s' sh '%s' reapply >>'%s' 2>&1 </dev/null", root, s, log);
#else
    snprintf(cmd, sizeof cmd, "env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME=/root sh '%s' reapply >>'%s' 2>&1 </dev/null", s, log);
#endif
    logmsg("16-pad drum layout patch installed and not active: running its reapply", NULL);
    int rc = system(cmd);
    int ok = exists(DP_CONF);
    char num[16];
    snprintf(num, sizeof num, "%d", WIFEXITED(rc) ? WEXITSTATUS(rc) : -1);
    logmsg(ok ? "16-pad drop-in written (reapply exit %s)" : "16-pad left stock (reapply exit %s)", num);
    return ok;
}

static void restart_mpc(time_t t0) {
#ifdef AR_TEST
    (void)t0;
    put_file("/restarted", "1\n");   /* tests: what would restart MPC */
#else
    time_t left = RESTART_AFTER - (time(NULL) - t0);
    if (left > 0) sleep((unsigned)left);
    if (system("systemctl daemon-reload") != 0) logmsg("daemon-reload failed", NULL);
    sleep(1);
    if (system("systemctl restart acvs") != 0) logmsg("restart failed", NULL);
#endif
}

/* lua_CFunction: package.loadlib returns it as callable; L is not used. Returns 0 (no Lua results). */
__attribute__((visibility("default"))) int addin_restore_check(void *L) {
    (void)L;
    const char *r = getenv("AR_ROOT");
#ifdef AR_TEST
    snprintf(root, sizeof root, "%s", r ? r : "");
#else
    (void)r;
    root[0] = 0;
#endif
    logmsg("checking addins (1.2.0)", NULL);
    if (exists(GUARD)) { logmsg("guard file exists, already ran this boot", NULL); return 0; }
    time_t t0 = time(NULL);
    int addins = restore_addins();
    int dp = drumpad_wanted();
    if (!addins && !dp) { put_file(GUARD, "ok\n"); return 0; }
    put_file(GUARD, "restarting\n");
#ifdef AR_TEST
    int child = 0;   /* tests: in the foreground */
#else
    pid_t p = fork();
    if (p < 0) { logmsg("fork failed", NULL); return 0; }
    if (p > 0) { waitpid(p, NULL, 0); return 0; }   /* the first child exits at once; MPC's Lua thread goes on */
    setsid();
    if (fork() != 0) _exit(0);
    int nul = open("/dev/null", O_RDWR);
    if (nul >= 0) { dup2(nul, 0); dup2(nul, 1); dup2(nul, 2); if (nul > 2) close(nul); }
    int child = 1;
#endif
    if (dp) dp = drumpad_reapply();
    if (addins || dp) { logmsg("drop-in written, restarting MPC once", NULL); restart_mpc(t0); }
    else logmsg("nothing to restart for", NULL);
    if (child) _exit(0);
    return 0;
}

/* Host test driver: loads the AR_TEST build of addin_restore.so and calls addin_restore_check once (AR_ROOT names the fake device). */
#include <dlfcn.h>
#include <stdio.h>
int main(int argc, char **argv) {
    void *h = dlopen(argc > 1 ? argv[1] : "./addin_restore_test.so", RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    int (*check)(void *) = (int (*)(void *))dlsym(h, "addin_restore_check");
    if (!check) { fprintf(stderr, "no addin_restore_check\n"); return 2; }
    return check(NULL);
}

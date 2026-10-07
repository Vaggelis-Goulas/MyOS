#include "userlib.h"

/* INIT.ELF -- optional init task run once at boot before the shell.
 * Keep it short and non-interactive: print a line and return. */
int _start(int argc, char **argv) {
    (void)argc;
    (void)argv;
    print("[INIT] user init task running.\n");
    return 0;
}

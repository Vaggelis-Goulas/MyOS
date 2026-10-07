#include "userlib.h"

/* ARGS.ELF -- echoes CLI arguments passed by the shell exec command. */
int _start(int argc, char **argv) {
    print("[ARGS] echoing ");
    print_dec_u(argc);
    print(" argument(s):\n");
    for (int i = 0; i < argc; i++) {
        print("  <");
        print(argv[i]);
        print(">\n");
    }
    return argc;
}

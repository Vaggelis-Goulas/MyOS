#include "userlib.h"

/* HELLO.ELF -- first native MyOS user program. */
int _start(int argc, char **argv) {
    print("Hello from userland! (HELLO.ELF)\n");
    print("argc=");
    print_dec_u(argc);
    print("\n");
    for (int i = 0; i < argc; i++) {
        print(" argv[");
        print_dec_u(i);
        print("]=");
        print(argv[i]);
        print("\n");
    }
    print("ticks=");
    print_dec_u(sys_ticks());
    print("\n");
    return 42;
}

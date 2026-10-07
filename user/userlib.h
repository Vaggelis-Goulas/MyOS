#ifndef USERLIB_H
#define USERLIB_H

/* Minimal userland library for MyOS native executables.
 * Programs are linked at USER_BASE with entry  int _start(int, char**);
 * I/O goes through kernel syscalls on int 0x80 (see C/syscall.h). */

#define SYS_EXIT    0
#define SYS_PRINT   1
#define SYS_PUTCHAR 2
#define SYS_TICKS   3

static inline int myos_syscall(int num, int a, int b, int c) {
    int ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(num), "b"(a), "c"(b), "d"(c)
                     : "memory");
    return ret;
}

static inline void sys_print(const char *s) {
    myos_syscall(SYS_PRINT, (int)s, 0, 0);
}

static inline void sys_putchar(char ch) {
    myos_syscall(SYS_PUTCHAR, (int)ch, 0, 0);
}

static inline void sys_exit(int code) {
    myos_syscall(SYS_EXIT, code, 0, 0);
}

static inline int sys_ticks(void) {
    return myos_syscall(SYS_TICKS, 0, 0, 0);
}

static inline void print(const char *s) {
    sys_print(s);
}

static inline void putchar_u(char c) {
    sys_putchar(c);
}

static inline void print_dec_u(int n) {
    char buf[12];
    int i = 0;
    unsigned int u;
    if (n < 0) {
        sys_putchar('-');
        u = (unsigned int)(-n);
    } else {
        u = (unsigned int)n;
    }
    if (u == 0) {
        sys_putchar('0');
        return;
    }
    while (u > 0) {
        buf[i++] = (char)('0' + (u % 10));
        u /= 10;
    }
    while (i > 0)
        sys_putchar(buf[--i]);
}

#endif

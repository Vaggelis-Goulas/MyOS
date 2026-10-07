#include "syscall.h"
#include "isr.h"
#include "vga.h"
#include "timer.h"
#include "elf.h"

/* int 0x80 handler: userland (linked at USER_BASE) requests kernel
 * services without needing kernel addresses. All pointers are validated
 * to avoid crashing the kernel on a bad user pointer. */

static int valid_user_str(const char *s, uint32_t max_len) {
    if (!s)
        return 0;
    uint32_t addr = (uint32_t)s;
    /* Allow user window + low kernel text/rodata for string literals?
     * User images live in USER_BASE..USER_END, but their argv points
     * into the shell buffer (<1MB). Accept anything mapped below 32MB. */
    if (addr >= 0x02000000u)
        return 0;
    for (uint32_t i = 0; i < max_len; i++) {
        if (s[i] == '\0')
            return 1;
    }
    return 0; /* unterminated / too long */
}

void syscall_handler(registers_t *r) {
    uint32_t num = r->eax;
    uint32_t a = r->ebx;
    /* uint32_t b = r->ecx; uint32_t c = r->edx; (reserved) */

    switch (num) {
    case SYS_EXIT:
        /* User returned via syscall: just return 0; the user stub
         * should `return` normally afterwards. Nothing to do here
         * because exec runs synchronously as a function call. */
        r->eax = (uint32_t)(int)a;
        break;
    case SYS_PRINT:
        if (valid_user_str((const char *)a, 4096))
            vga_print((const char *)a);
        r->eax = 0;
        break;
    case SYS_PUTCHAR:
        vga_putchar((char)(a & 0xFF));
        r->eax = 0;
        break;
    case SYS_TICKS:
        r->eax = timer_get_ticks();
        break;
    default:
        r->eax = (uint32_t)-1;
        break;
    }
}

void syscall_init() {
    register_interrupt_handler(128, syscall_handler);
    vga_print("[SYS] syscalls on int 0x80 ready.\n");
}

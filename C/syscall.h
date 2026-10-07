#ifndef SYSCALL_H
#define SYSCALL_H

#include "isr.h"

/* MyOS user syscalls via int 0x80.
 * eax = number, ebx/ecx/edx = args, return in eax.
 */
#define SYS_EXIT    0
#define SYS_PRINT   1
#define SYS_PUTCHAR 2
#define SYS_TICKS   3

void syscall_init();
void syscall_handler(registers_t *r);

#endif

#ifndef SERIAL_H
#define SERIAL_H

/* COM1 (0x3F8) output, used to mirror the console so a guest can be
 * watched in a log file (VirtualBox/Bochs/Virtual PC serial port, or
 * `make run-serial` with QEMU) instead of only in a window. */

void serial_init();
void serial_putc(char c);
void serial_puts(const char *s);

#endif
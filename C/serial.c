#include "serial.h"
#include "io.h"

#define COM1        0x3F8
#define REG_DATA    0
#define REG_IER     1
#define REG_FCR     2
#define REG_LCR     3
#define REG_MCR     4
#define REG_LSR     5

#define LSR_THRE    0x20    /* transmit holding register empty */
#define LCR_8N1     0x03

static int serial_ready;

void serial_init() {
    outb(COM1 + REG_IER, 0x00);     /* no interrupts */
    outb(COM1 + REG_LCR, LCR_8N1);  /* 8 data bits, no parity, 1 stop */
    outb(COM1 + REG_FCR, 0xC7);     /* enable + clear FIFOs, 14-byte trigger */
    outb(COM1 + REG_MCR, 0x03);     /* DTR | RTS */

    /* No loopback test: many BIOSes and hypervisors leave the UART
     * unconnected, and a stuck THRE there would make every console
     * write spin forever. Probing only LSR costs one port read. */
    serial_ready = (inb(COM1 + REG_LSR) & LSR_THRE) != 0;
}

void serial_putc(char c) {
    if (!serial_ready) return;
    if (c == '\n') serial_putc('\r');

    /* Bounded wait: if the line is never drained we drop the character
     * rather than wedging the kernel on a wedged UART. */
    for (int spin = 0; spin < 100000; spin++) {
        if (inb(COM1 + REG_LSR) & LSR_THRE) {
            outb(COM1 + REG_DATA, c);
            return;
        }
    }
}

void serial_puts(const char *s) {
    while (*s) serial_putc(*s++);
}
#include "isr.h"
#include "vga.h"
#include "io.h"

static void *interrupt_handlers[256];

static const char *exception_msgs[] = {
    "Division By Zero","Debug","Non Maskable Interrupt","Breakpoint",
    "Into Detected Overflow","Out of Bounds","Invalid Opcode",
    "No Coprocessor","Double Fault","Coprocessor Segment Overrun",
    "Bad TSS","Segment Not Present","Stack Fault",
    "General Protection Fault","Page Fault","Unknown Interrupt",
    "Coprocessor Fault","Alignment Check","Machine Check",
    "Reserved","Reserved","Reserved","Reserved",
    "Reserved","Reserved","Reserved","Reserved",
    "Reserved","Reserved","Reserved","Reserved"
};

void register_interrupt_handler(int n, void (*handler)(registers_t *r)) {
    interrupt_handlers[n] = handler;
}

void isr_handler(registers_t *r) {
    /* Subsystems may hook specific exceptions: paging.c registers
       the demand-paging handler for the page fault (vector 14). */
    void (*handler)(registers_t *) = interrupt_handlers[r->int_no];
    if (handler) {
        handler(r);
        return;
    }

    vga_print("\n!!! EXCEPTION: ");
    if (r->int_no < 32) vga_print(exception_msgs[r->int_no]);
    vga_print("\nSystem halted.\n");
    asm volatile("cli; hlt");
}

void irq_handler(registers_t *r) {
    /* Only hardware IRQs 32..47 need PIC EOI. Software vectors like
     * 62 (yield) and 128 (syscall) must not touch the PIC. */
    if (r->int_no >= 32 && r->int_no < 48) {
        if (r->int_no >= 40) outb(0xA0, 0x20);
        outb(0x20, 0x20);
    }
    
    void (*handler)(registers_t *) = interrupt_handlers[r->int_no];
    if (handler) handler(r);
}
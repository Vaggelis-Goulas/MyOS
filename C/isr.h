#ifndef ISR_H
#define ISR_H

#include <stdint.h>

/* Snapshot of the stack built by the ISR/IRQ stubs in boot/isr_stubs.asm.
 *
 * The stubs push int_no and err_code, then pushad, then ds, and hand
 * isr_handler()/irq_handler() a pointer to the result. Because the stack
 * grows downwards, pushad leaves its registers in *reverse* push order
 * (EDI lowest, EAX highest), which is why the general registers appear
 * here in that order. Field order must match that layout exactly.
 */
typedef struct {
    uint32_t ds, edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss;
} __attribute__((packed)) registers_t;

void isr_handler(registers_t *r);
void irq_handler(registers_t *r);
void register_interrupt_handler(int n, void (*handler)(registers_t *r));

#endif
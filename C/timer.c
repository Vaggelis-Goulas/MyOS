#include "timer.h"
#include "io.h"
#include "isr.h"
#include "task.h"

extern int preempt_count;

static volatile unsigned int ticks = 0;

void timer_handler(registers_t *r) {
    ticks++;
    // Trigger scheduler via task_switch interrupt (vector 62) if preemption enabled
    if (preempt_count == 0) {
        asm volatile("int $62");
    }
}

void timer_init(unsigned int frequency) {
    unsigned int divisor = 1193180 / frequency;
    
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    
    register_interrupt_handler(32, timer_handler);
}

unsigned int timer_get_ticks() {
    return ticks;
}

void timer_sleep(unsigned int ms) {
    unsigned int target = ticks + (ms / 10);
    while (ticks < target);
}
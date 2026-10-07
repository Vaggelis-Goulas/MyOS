#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

#define PHYS_MAX_MB 32
#define PHYS_MAX     (PHYS_MAX_MB * 1024 * 1024)
#define FRAME_SIZE   4096
#define NUM_FRAMES   (PHYS_MAX / FRAME_SIZE)

#define HEAP_START 0x01000000
#define HEAP_SIZE  0x00100000
#define HEAP_MAX   0x00800000   /* kernel heap arena may grow to 8 MB */

void memory_init();
uint32_t frame_alloc();
uint32_t frame_alloc_zero();
void frame_free(uint32_t addr);
uint32_t frame_count_free();

void kmalloc_init();
void *kmalloc(uint32_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, uint32_t size);
uint32_t kmalloc_used();
uint32_t kmalloc_extent();

#endif
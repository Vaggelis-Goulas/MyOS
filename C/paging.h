#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_SIZE   4096
#define PAGE_TABLE_CNT 1024

/* Recursive mapping: page directory at 0xFFFFF000,
   page tables at 0xFFC00000 + index*4096 */
#define REC_BASE    0xFFC00000
#define PD_VIRT     0xFFFFF000

#define PD_INDEX(v) (((uint32_t)(v) >> 22) & 0x3FF)
#define PT_INDEX(v) (((uint32_t)(v) >> 12) & 0x3FF)

#define PAGE_PRESENT     0x001
#define PAGE_WRITABLE    0x002
#define PAGE_USER        0x004
#define PAGE_GLOBAL      0x100

typedef struct {
    uint32_t entries[PAGE_TABLE_CNT];
} page_directory_t;

typedef struct {
    uint32_t entries[PAGE_TABLE_CNT];
} page_table_t;

void paging_init();
void map_page(uint32_t virt, uint32_t phys, uint32_t flags);
void unmap_page(uint32_t virt);
uint32_t virt_to_phys(uint32_t virt);
int paging_state();

/* ---------- Userland process address spaces ---------- */

/* Each userland process receives an isolated page directory (with
   its own page table frames) plus a private heap region of
   USER_HEAP_SIZE bytes starting at USER_HEAP_START. */
#define USER_HEAP_START  0x40000000u
#define USER_HEAP_SIZE   0x00400000u

page_directory_t *page_directory_create(void);
void page_directory_destroy(page_directory_t *pd);
void switch_page_directory(page_directory_t *pd);
page_directory_t *page_directory_current(void);
page_directory_t *page_directory_kernel(void);

/* ---------- User heap control (brk/sbrk equivalents) ---------- */

int      user_heap_init(page_directory_t *pd);
void     user_heap_release(page_directory_t *pd);
uint32_t sys_brk(uint32_t addr);
uint32_t sys_sbrk(int32_t inc);
uint32_t user_heap_brk(void);
uint32_t user_heap_limit(void);

#endif
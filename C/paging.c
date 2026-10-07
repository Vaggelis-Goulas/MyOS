#include "paging.h"
#include "memory.h"
#include "util.h"
#include "vga.h"
#include "isr.h"
#include <stddef.h>

static int paging_on = 0;
static page_directory_t *kernel_pd = NULL;

int paging_state() {
    return paging_on;
}

/* ---------- Per-process user heap descriptors ---------- */

#define MAX_USER_HEAPS 16

typedef struct {
    int active;
    page_directory_t *pd;
    uint32_t start;   /* first byte of the private heap region */
    uint32_t brk;     /* current break: end of the data segment */
    uint32_t limit;   /* start + USER_HEAP_SIZE */
} user_heap_t;

static user_heap_t user_heaps[MAX_USER_HEAPS];

static user_heap_t *heap_for(page_directory_t *pd) {
    if (!pd) return NULL;
    for (int i = 0; i < MAX_USER_HEAPS; i++) {
        if (user_heaps[i].active && user_heaps[i].pd == pd) return &user_heaps[i];
    }
    return NULL;
}

static user_heap_t *current_heap() {
    return heap_for(page_directory_current());
}

int user_heap_init(page_directory_t *pd) {
    if (heap_for(pd)) return -1;
    for (int i = 0; i < MAX_USER_HEAPS; i++) {
        if (!user_heaps[i].active) {
            user_heaps[i].active = 1;
            user_heaps[i].pd = pd;
            user_heaps[i].start = USER_HEAP_START;
            user_heaps[i].brk = USER_HEAP_START;
            user_heaps[i].limit = USER_HEAP_START + USER_HEAP_SIZE;
            return 0;
        }
    }
    return -1;
}

void user_heap_release(page_directory_t *pd) {
    user_heap_t *h = heap_for(pd);
    if (h) h->active = 0;
}

uint32_t user_heap_brk(void) {
    user_heap_t *h = current_heap();
    return h ? h->brk : 0;
}

uint32_t user_heap_limit(void) {
    user_heap_t *h = current_heap();
    return h ? h->limit : 0;
}

/* Set the break of the current process. addr == 0 queries the
   current break. Returns the break after the operation. */
uint32_t sys_brk(uint32_t addr) {
    user_heap_t *h = current_heap();
    if (!h) return 0;
    if (addr == 0) return h->brk;
    if (addr < h->start || addr > h->limit) return h->brk;

    if (addr < h->brk) {
        /* Shrink: release pages that now lie entirely past the break */
        uint32_t new_end = (addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        uint32_t old_end = (h->brk + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        for (uint32_t v = new_end; v < old_end; v += PAGE_SIZE) {
            uint32_t phys = virt_to_phys(v);
            if (phys) {
                unmap_page(v);
                frame_free(phys);
            }
        }
    }
    /* Growing is lazy: no frames are committed here. The page
       fault handler maps a fresh zeroed page the first time the
       process touches each page of the expanded segment. */
    h->brk = addr;
    return h->brk;
}

/* Move the break by inc bytes. Returns the old break, or
   (uint32_t)-1 if the request would leave the heap region. */
uint32_t sys_sbrk(int32_t inc) {
    user_heap_t *h = current_heap();
    if (!h) return (uint32_t)-1;
    uint32_t old = h->brk;
    uint32_t neu = old + (uint32_t)inc;
    if (inc > 0 && neu < old) return (uint32_t)-1;   /* wrapped */
    if (inc < 0 && neu > old) return (uint32_t)-1;   /* wrapped */
    if (neu < h->start || neu > h->limit) return (uint32_t)-1;
    sys_brk(neu);
    return old;
}

/* ---------- Userland page directory management ---------- */

page_directory_t *page_directory_create(void) {
    if (!kernel_pd) return NULL;

    page_directory_t *pd = (page_directory_t*)frame_alloc_zero();
    if (!pd) return NULL;

    /* Deep copy of the kernel directory: every present entry gets a
       fresh page table frame owned by this process, so later remapping
       can never corrupt the kernel's page tables. The physical pages
       those entries point at stay shared (identity-mapped kernel
       region), keeping the kernel code/data reachable while the
       process runs. */
    int i;
    for (i = 0; i < PAGE_TABLE_CNT; i++) {
        if (i == 1023) continue;
        uint32_t entry = kernel_pd->entries[i];
        if (!(entry & PAGE_PRESENT)) continue;

        page_table_t *pt = (page_table_t*)frame_alloc_zero();
        if (!pt) goto fail;

        page_table_t *src = (page_table_t*)(REC_BASE + (i << 12));
        memcpy(pt, src, PAGE_SIZE);
        pd->entries[i] = (uint32_t)pt | (entry & 0xFFF);
    }

    /* Recursive self-mapping so map_page()/unmap_page() operate on
       this process's tables while it is active */
    pd->entries[1023] = (uint32_t)pd | PAGE_PRESENT | PAGE_WRITABLE;

    user_heap_init(pd);
    return pd;

fail:
    for (int j = 0; j < i; j++) {
        if (pd->entries[j] & PAGE_PRESENT) {
            frame_free(pd->entries[j] & 0xFFFFF000);
            pd->entries[j] = 0;
        }
    }
    frame_free((uint32_t)pd);
    return NULL;
}

void page_directory_destroy(page_directory_t *pd) {
    if (!pd || pd == kernel_pd) return;
    if (page_directory_current() == pd) switch_page_directory(kernel_pd);

    user_heap_release(pd);

    uint32_t kernel_tables = PHYS_MAX / (PAGE_TABLE_CNT * PAGE_SIZE);
    for (int i = 0; i < 1023; i++) {
        uint32_t entry = pd->entries[i];
        if (!(entry & PAGE_PRESENT)) continue;
        uint32_t pt_phys = entry & 0xFFFFF000;
        uint32_t *pt = (uint32_t*)pt_phys;

        for (int j = 0; j < PAGE_TABLE_CNT; j++) {
            if (!(pt[j] & PAGE_PRESENT)) continue;
            uint32_t page_phys = pt[j] & 0xFFFFF000;
            /* Pages inside the identity-mapped kernel region are
               shared with the kernel: only release frames the
               process mapped on its own (e.g. its private heap). */
            if (i >= kernel_tables ||
                page_phys != (uint32_t)(i * PAGE_TABLE_CNT + j) * PAGE_SIZE) {
                frame_free(page_phys);
            }
        }
        frame_free(pt_phys);
        pd->entries[i] = 0;
    }
    pd->entries[1023] = 0;
    frame_free((uint32_t)pd);
}

void switch_page_directory(page_directory_t *pd) {
    if (!pd) return;
    asm volatile("mov %0, %%cr3" :: "r"(pd));
}

page_directory_t *page_directory_current(void) {
    if (!paging_on) return NULL;
    uint32_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    return (page_directory_t*)(cr3 & 0xFFFFF000);
}

page_directory_t *page_directory_kernel(void) {
    return kernel_pd;
}

/* ---------- Page fault handler: demand paging ---------- */

static void page_fault_handler(registers_t *r) {
    uint32_t addr;
    asm volatile("mov %%cr2, %0" : "=r"(addr));

    /* A touch inside the current process's heap region
       [start, brk) is satisfied on demand: commit a fresh zeroed
       frame and resume the faulting instruction. */
    user_heap_t *h = current_heap();
    if (h && addr >= h->start && addr < h->brk) {
        uint32_t page = addr & ~(PAGE_SIZE - 1);
        uint32_t phys = frame_alloc_zero();
        if (phys) {
            map_page(page, phys, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
            return;
        }
    }

    vga_print("\n!!! PAGE FAULT at ");
    print_hex(addr);
    vga_print(" err=");
    print_hex(r->err_code);
    vga_print(" eip=");
    print_hex(r->eip);
    if (r->err_code & 1) vga_print(" (protection violation)");
    else vga_print(" (page not present)");
    vga_print("\nSystem halted.\n");
    asm volatile("cli; hlt");
}

/* ---------- Boot-time paging setup ---------- */

void paging_init() {
    /* Before paging is enabled, linear == physical. */
    page_directory_t *pd = (page_directory_t*)frame_alloc_zero();
    kernel_pd = pd;

    uint32_t pt_count = PHYS_MAX / (PAGE_TABLE_CNT * PAGE_SIZE);  /* 32MB / 4MB = 8 */

    for (uint32_t pti = 0; pti < pt_count; pti++) {
        page_table_t *pt = (page_table_t*)frame_alloc_zero();
        for (uint32_t j = 0; j < PAGE_TABLE_CNT; j++) {
            uint32_t phys = (pti * PAGE_TABLE_CNT + j) * PAGE_SIZE;
            pt->entries[j] = phys | PAGE_PRESENT | PAGE_WRITABLE;
        }
        pd->entries[pti] = (uint32_t)pt | PAGE_PRESENT | PAGE_WRITABLE;
    }

    /* Recursive mapping self-entry so page tables are addressable from code */
    pd->entries[1023] = (uint32_t)pd | PAGE_PRESENT | PAGE_WRITABLE;

    asm volatile("mov %0, %%cr3" :: "r"(pd));
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  /* set PG, PE is already on */
    asm volatile("mov %0, %%cr0" :: "r"(cr0));

    paging_on = 1;

    /* Route page faults to the demand-paging handler above. idt_init()
       (run later) wires vector 14 to isr14, which dispatches here
       through the registered exception handler table. */
    register_interrupt_handler(14, page_fault_handler);

    vga_print("[PAG] Paging enabled. Page directory: ");
    print_hex((uint32_t)pd);
    vga_print("\n");
}

void map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pdi = PD_INDEX(virt);
    uint32_t *pd = (uint32_t*)PD_VIRT;

    if (!(pd[pdi] & PAGE_PRESENT)) {
        uint32_t pt_phys = frame_alloc_zero();
        pd[pdi] = pt_phys | PAGE_PRESENT | PAGE_WRITABLE;
    }
    uint32_t *pt = (uint32_t*)(REC_BASE + (pdi << 12));
    pt[PT_INDEX(virt)] = (phys & 0xFFFFF000) | flags;
    asm volatile("invlpg (%0)" :: "r"(virt));
}

void unmap_page(uint32_t virt) {
    uint32_t pdi = PD_INDEX(virt);
    uint32_t *pd = (uint32_t*)PD_VIRT;
    if (!(pd[pdi] & PAGE_PRESENT)) return;
    uint32_t *pt = (uint32_t*)(REC_BASE + (pdi << 12));
    pt[PT_INDEX(virt)] = 0;
    asm volatile("invlpg (%0)" :: "r"(virt));
}

uint32_t virt_to_phys(uint32_t virt) {
    if (!paging_on) return virt;

    uint32_t *pd = (uint32_t*)PD_VIRT;
    uint32_t pdi = PD_INDEX(virt);
    if (!(pd[pdi] & PAGE_PRESENT)) return 0;

    uint32_t *pt = (uint32_t*)(REC_BASE + (pdi << 12));
    uint32_t pte = pt[PT_INDEX(virt)];
    if (!(pte & PAGE_PRESENT)) return 0;

    return (pte & 0xFFFFF000) | (virt & 0xFFF);
}

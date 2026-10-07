#include "memory.h"
#include "util.h"
#include "vga.h"
#include <stddef.h>

static uint8_t frame_bitmap[NUM_FRAMES / 8];

static void bitmap_set(int i) { frame_bitmap[i >> 3] |= (1 << (i & 7)); }
static void bitmap_clear(int i) { frame_bitmap[i >> 3] &= ~(1 << (i & 7)); }
static int bitmap_get(int i) { return (frame_bitmap[i >> 3] >> (i & 7)) & 1; }

static void mark_range(uint32_t start, uint32_t size) {
    uint32_t s = start >> 12;
    uint32_t e = (start + size + FRAME_SIZE - 1) >> 12;
    for (uint32_t i = s; i < e && i < NUM_FRAMES; i++) bitmap_set(i);
}

uint32_t frame_alloc() {
    for (uint32_t i = 0; i < NUM_FRAMES; i++) {
        if (!bitmap_get(i)) {
            bitmap_set(i);
            return i * FRAME_SIZE;
        }
    }
    return 0;
}

uint32_t frame_alloc_zero() {
    uint32_t f = frame_alloc();
    if (f) memset((void*)f, 0, FRAME_SIZE);
    return f;
}

void frame_free(uint32_t addr) {
    uint32_t i = addr / FRAME_SIZE;
    if (i > 0 && i < NUM_FRAMES) bitmap_clear(i);
}

uint32_t frame_count_free() {
    uint32_t n = 0;
    for (uint32_t i = 0; i < NUM_FRAMES; i++) {
        if (!bitmap_get(i)) n++;
    }
    return n;
}

/* ---------- KERNEL HEAP ---------- */

#define ALIGN8(n) (((n) + 7) & ~7)

#define MAGIC_FREE 0xF1EEF1EE
#define MAGIC_USED 0xC0DEC0DE

typedef struct block_header {
    uint32_t size;
    uint32_t magic;
    struct block_header *next;
} __attribute__((aligned(8))) block_header_t;

static block_header_t *free_list;
static uint32_t heap_used;
static uint32_t heap_extent;

static void free_list_insert(block_header_t *b);

void kmalloc_init() {
    free_list = (block_header_t*)HEAP_START;
    free_list->size = HEAP_SIZE;
    free_list->magic = MAGIC_FREE;
    free_list->next = NULL;
    heap_used = 0;
    heap_extent = HEAP_SIZE;
}

static void free_list_insert(block_header_t *b);
static void coalesce(void);

/* Grow the kernel heap arena by attaching reserved frames from the
   region [HEAP_START, HEAP_START + HEAP_MAX). The whole arena was
   marked reserved in memory_init(), so the frames backing it are
   never handed out by frame_alloc(), and the kernel's identity page
   tables already map them 1:1 at their physical addresses. */
static int heap_extend(uint32_t need) {
    if (heap_extent >= HEAP_MAX) return 0;

    uint32_t grow = (need + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
    if (grow < FRAME_SIZE) grow = FRAME_SIZE;
    uint32_t room = HEAP_MAX - heap_extent;
    if (grow > room) grow = room & ~(FRAME_SIZE - 1);
    if (grow == 0) return 0;

    uint32_t base = HEAP_START + heap_extent;

    block_header_t *cur = free_list;
    while (cur) {
        if (cur->magic == MAGIC_FREE && (uint8_t*)cur + cur->size == (uint8_t*)base) {
            cur->size += grow;      /* merge with trailing free block */
            heap_extent += grow;
            return 1;
        }
        cur = cur->next;
    }

    block_header_t *b = (block_header_t*)base;
    b->size = grow;
    b->magic = MAGIC_FREE;
    free_list_insert(b);
    heap_extent += grow;
    return 1;
}

void *kmalloc(uint32_t size) {
    uint32_t want = ALIGN8(size) + sizeof(block_header_t);

    for (int attempt = 0; attempt < 2; attempt++) {
        block_header_t *prev = NULL;
        block_header_t *cur = free_list;

        while (cur) {
            if (cur->magic != MAGIC_FREE || cur->size < want) {
                prev = cur;
                cur = cur->next;
                continue;
            }

            /* Unlink before repurposing: the free list must only ever
             * contain FREE blocks. Leaving the allocated block linked in
             * would make a later kfree() re-insert it at its own address
             * and turn the list into a cycle. */
            if (prev) prev->next = cur->next;
            else free_list = cur->next;

            if (cur->size - want >= sizeof(block_header_t) + 8) {
                block_header_t *rest = (block_header_t*)((uint8_t*)cur + want);
                rest->size = cur->size - want;
                rest->magic = MAGIC_FREE;
                rest->next = NULL;
                free_list_insert(rest);
            }
            cur->size = want;
            cur->magic = MAGIC_USED;
            cur->next = NULL;
            heap_used += cur->size;
            return (void*)(cur + 1);
        }

        /* No free block fits: extend the arena and retry once. */
        if (!heap_extend(want)) break;
    }
    return NULL;
}

static void free_list_insert(block_header_t *b) {
    block_header_t *prev = NULL, *cur = free_list;
    while (cur && (uint32_t)cur < (uint32_t)b) { prev = cur; cur = cur->next; }
    b->next = cur;
    if (prev) prev->next = b;
    else free_list = b;
}

/* Merge physically adjacent FREE blocks into one. Both operands have to be
 * free, otherwise the merged block would hand out memory that is still
 * allocated. */
static void coalesce() {
    block_header_t *cur = free_list;
    while (cur && cur->next) {
        block_header_t *next = cur->next;
        if (next->magic == MAGIC_FREE &&
            (uint8_t*)cur + cur->size == (uint8_t*)next) {
            cur->size += next->size;
            cur->next = next->next;
            continue;
        }
        cur = cur->next;
    }
}

void kfree(void *ptr) {
    if (!ptr) return;
    block_header_t *b = (block_header_t*)ptr - 1;
    if (b->magic != MAGIC_USED) return;
    b->magic = MAGIC_FREE;
    heap_used -= b->size;
    free_list_insert(b);
    coalesce();
}

void *krealloc(void *ptr, uint32_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) { kfree(ptr); return NULL; }

    block_header_t *b = (block_header_t*)ptr - 1;
    if (b->magic != MAGIC_USED) return NULL;

    uint32_t want = ALIGN8(size) + sizeof(block_header_t);
    if (want <= b->size) return ptr;    /* already fits in place */

    void *fresh = kmalloc(size);
    if (!fresh) return NULL;

    uint32_t copy = b->size - sizeof(block_header_t);
    if (copy > size) copy = size;
    memcpy(fresh, ptr, copy);
    kfree(ptr);
    return fresh;
}

uint32_t kmalloc_used() {
    return heap_used;
}

uint32_t kmalloc_extent() {
    return heap_extent;
}

void memory_init() {
    memset(frame_bitmap, 0, sizeof(frame_bitmap));

    /* Reserve legacy region: IVT, BIOS, VGA, and the loaded kernel */
    mark_range(0, 0x100000);

    /* Reserve the entire growable kernel heap arena so the frame
       allocator never hands its pages out behind the heap's back */
    mark_range(HEAP_START, HEAP_MAX);

    kmalloc_init();

    vga_print("[MEM] 32MB physical memory. ");
    print_dec(NUM_FRAMES);
    vga_print(" frames.\n");
}
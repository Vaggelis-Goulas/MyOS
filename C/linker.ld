/* ==========================================================================
 * MyOS kernel linker script
 *
 * Memory layout (32-bit, identity-mapped paging over 32MB physical):
 *
 *   KERNEL SPACE (linked here):
 *     0x00008000 - 0x003FFFFF   kernel image (.text/.rodata/.data/.bss)
 *       - kernel is loaded by stage2 at 0x8000 and runs there
 *       - _kernel_start .. _kernel_end delimit the linked image
 *       - heap lives at 0x01000000 (see memory.h HEAP_START, 1MB)
 *       - low 1MB (IVT/BIOS/VGA) is reserved, never linked
 *
 *   USER SPACE (NOT linked here -- see user/user.ld):
 *     0x00400000 (USER_BASE) .. 0x00800000 (USER_END)  4MB window
 *       - standalone userland ELF programs are linked at USER_BASE
 *       - the ELF loader (C/elf.c) maps PT_LOAD segments only inside
 *         [USER_BASE, USER_END) and jumps to e_entry with argc/argv
 *       - kernel never links anything into user range (ASSERT below)
 *
 * Keeping the two address spaces disjoint lets the same Makefile produce
 * both the flat kernel image (floppy.img) and native user executables
 * (stored on disk.img) without overlap.
 * ========================================================================== */
ENTRY(kernel_main)

/* Address-space boundaries shared with C/elf.h and user/user.ld */
KERNEL_BASE = 0x8000;
USER_BASE   = 0x00400000;
USER_END    = 0x00800000;

SECTIONS
{
    . = KERNEL_BASE;
    _kernel_start = .;
    __kernel_start = .;
    .text : ALIGN(4)
    {
        *(.text.start)
        *(.text*)
        . = ALIGN(4);
    }
    .rodata : ALIGN(4)
    {
        *(.rodata*)
        . = ALIGN(4);
    }
    .data : ALIGN(4)
    {
        *(.data*)
        . = ALIGN(4);
    }
    /* End of the bytes stage2 actually reads off the floppy (.bss is
       NOBITS: kernel_main zeroes it at boot, it is never loaded). */
    _load_end = .;
    .bss : ALIGN(4)
    {
        _bss_start = .;
        *(COMMON)
        *(.bss*)
        . = ALIGN(4);
        _bss_end = .;
    }
    _kernel_end = .;
    __kernel_end = .;
    _kernel_size = _kernel_end - _kernel_start;
    _load_size = _load_end - _kernel_start;

    /* boot/stage2.asm loads the image as two 32 KiB reads (0x8000..0xFFFF
       and 0x10000..0x1FFFF) because the floppy DMA engine cannot cross a
       64 KiB page. Anything bigger would silently fail to boot, so refuse
       to link it instead. */
    ASSERT(_load_size <= 0x10000,
           "MyOS: kernel exceeds the 64 KiB stage2 load window (0x8000..0x1FFFF)")

    /* The image also has to fit in the 2880-sector floppy after the boot
       sector and stage2, i.e. at most 2878 sectors (1.4 MiB). */
    ASSERT(_load_size <= 2878 * 512,
           "MyOS: kernel does not fit on the floppy image")

    /* NOTE: kernel must stay below USER_BASE (0x00400000) and below the
       kernel heap at 0x01000000. User executables are linked separately
       with user/user.ld at USER_BASE; they never overlap this image. */

    /DISCARD/ : { *(.eh_frame) *(.note*) *(.comment) }
}
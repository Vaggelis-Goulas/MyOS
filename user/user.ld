/* ==========================================================================
 * MyOS userland linker script
 *
 * Standalone user programs are native 32-bit ELF executables linked at
 * USER_BASE (0x00400000), well above the kernel image (0x8000) and below
 * the kernel heap (0x01000000 is heap -- actually 16MB, so 4MB is safe).
 * See C/linker.ld for the full kernel/user address-space split and
 * C/elf.h for USER_BASE/USER_END definitions (must stay in sync).
 * ========================================================================== */
ENTRY(_start)

USER_BASE = 0x00400000;

SECTIONS
{
    . = USER_BASE;
    .text : ALIGN(4)
    {
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
    .bss : ALIGN(4)
    {
        *(COMMON)
        *(.bss*)
        . = ALIGN(4);
    }
    /DISCARD/ : { *(.eh_frame) *(.note*) *(.comment) }
}

#ifndef ELF_H
#define ELF_H

#include <stdint.h>

/* ------------------------------------------------------------------
 * MyOS 32-bit ELF loader.
 *
 * Kernel space  : linked at 0x8000        (see C/linker.ld)
 * User space    : 0x00400000..0x00800000   (see C/linker.ld, user/user.ld)
 * Only PT_LOAD segments whose [p_vaddr, p_vaddr+p_memsz) lies fully
 * inside user space are mapped. Identity paging already covers the
 * range, but elf_load() still calls map_page() for every page so the
 * "map into process memory space" requirement is explicit.
 *
 * User ABI: entry is  int _start(int argc, char **argv);
 * The shell (kernel.c cmd_exec) loads the file from FAT12 disk,
 * maps it, then calls entry(argc, argv) synchronously and reports
 * the return value as the exit code.
 * ------------------------------------------------------------------ */

#define USER_BASE 0x00400000u
#define USER_END  0x00800000u
#define USER_MAX_SIZE (USER_END - USER_BASE)

/* ELF identification indexes */
#define EI_NIDENT  16
#define EI_MAG0    0
#define EI_MAG1    1
#define EI_MAG2    2
#define EI_MAG3    3
#define EI_CLASS   4
#define EI_DATA    5
#define EI_VERSION 6

#define ELFMAG0 0x7F
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'

#define ELFCLASS32  1
#define ELFDATA2LSB 1
#define EV_CURRENT  1

#define ET_EXEC 2
#define EM_386  3

#define PT_LOAD 1

#define PF_X 1
#define PF_W 2
#define PF_R 4

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} __attribute__((packed)) Elf32_Phdr;

/* Validate an in-memory image. Returns 0 if valid, <0 with reason otherwise. */
int elf_check(const uint8_t *img, uint32_t size);

/* Map PT_LOAD segments of img[0..size) into user space.
 * On success writes *entry_out = e_entry and returns 0. */
int elf_load(const uint8_t *img, uint32_t size, uint32_t *entry_out);

/* Load file <path> from FAT12 disk and map it. Same return convention. */
int elf_load_file(const char *path, uint32_t *entry_out);

/* Load + execute: calls entry(argc, argv) and returns its return value,
 * or <0 on load failure. */
int elf_exec(const char *path, int argc, char **argv);

/* Debug helper: print e_entry + all program headers via vga. */
void elf_info(const uint8_t *img, uint32_t size);

#endif

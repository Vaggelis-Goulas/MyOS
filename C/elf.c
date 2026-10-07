#include "elf.h"
#include "fs.h"
#include "memory.h"
#include "paging.h"
#include "util.h"
#include "vga.h"

#define PAGE_ALIGN_DOWN(x) ((x) & ~0xFFFu)
#define PAGE_ALIGN_UP(x)   (((x) + 0xFFFu) & ~0xFFFu)

int elf_check(const uint8_t *img, uint32_t size) {
    if (!img || size < sizeof(Elf32_Ehdr))
        return -1;
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)img;

    if (eh->e_ident[EI_MAG0] != ELFMAG0 ||
        eh->e_ident[EI_MAG1] != ELFMAG1 ||
        eh->e_ident[EI_MAG2] != ELFMAG2 ||
        eh->e_ident[EI_MAG3] != ELFMAG3)
        return -2; /* bad magic */
    if (eh->e_ident[EI_CLASS] != ELFCLASS32)
        return -3; /* not 32-bit */
    if (eh->e_ident[EI_DATA] != ELFDATA2LSB)
        return -4; /* not little-endian */
    if (eh->e_ident[EI_VERSION] != EV_CURRENT)
        return -5;
    if (eh->e_type != ET_EXEC)
        return -6; /* must be executable */
    if (eh->e_machine != EM_386)
        return -7; /* must be i386 */
    if (eh->e_version != EV_CURRENT)
        return -8;
    if (eh->e_ehsize != sizeof(Elf32_Ehdr))
        return -9;
    if (eh->e_phentsize != sizeof(Elf32_Phdr))
        return -10;
    if (eh->e_phnum == 0 || eh->e_phnum > 16)
        return -11;
    /* program-header table must be inside the file */
    uint32_t ph_end = eh->e_phoff + (uint32_t)eh->e_phnum * sizeof(Elf32_Phdr);
    if (eh->e_phoff >= size || ph_end > size || ph_end < eh->e_phoff)
        return -12;
    /* entry point must live in user space */
    if (eh->e_entry < USER_BASE || eh->e_entry >= USER_END)
        return -13;

    const Elf32_Phdr *ph = (const Elf32_Phdr *)(img + eh->e_phoff);
    int have_load = 0;
    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD)
            continue;
        have_load = 1;
        if (ph[i].p_memsz < ph[i].p_filesz)
            return -14;
        if (ph[i].p_memsz == 0)
            continue;
        /* bounds inside the file image */
        if (ph[i].p_offset >= size)
            return -15;
        if (ph[i].p_filesz > size - ph[i].p_offset)
            return -16;
        /* bounds inside user address space, no wrap */
        if (ph[i].p_vaddr < USER_BASE || ph[i].p_vaddr >= USER_END)
            return -17;
        if (ph[i].p_memsz > USER_END - ph[i].p_vaddr)
            return -18;
    }
    if (!have_load)
        return -19;
    return 0;
}

int elf_load(const uint8_t *img, uint32_t size, uint32_t *entry_out) {
    int rc = elf_check(img, size);
    if (rc != 0)
        return rc;

    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)img;
    const Elf32_Phdr *ph = (const Elf32_Phdr *)(img + eh->e_phoff);

    for (int i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD)
            continue;
        if (ph[i].p_memsz == 0)
            continue;

        uint32_t vaddr  = ph[i].p_vaddr;
        uint32_t filesz = ph[i].p_filesz;
        uint32_t memsz  = ph[i].p_memsz;
        uint32_t offset = ph[i].p_offset;

        /* 1. Make sure every page in [vaddr, vaddr+memsz) is mapped.
         * Identity paging already covers USER_BASE..USER_END, but we
         * map explicitly so the loader owns the mapping. */
        uint32_t start = PAGE_ALIGN_DOWN(vaddr);
        uint32_t end   = PAGE_ALIGN_UP(vaddr + memsz);
        uint32_t flags = PAGE_PRESENT | PAGE_USER;
        if (ph[i].p_flags & PF_W)
            flags |= PAGE_WRITABLE;

        for (uint32_t page = start; page < end; page += 0x1000) {
            uint32_t phys = virt_to_phys(page);
            if (phys == 0) {
                /* Not mapped yet: allocate a fresh frame. */
                uint32_t frame = frame_alloc_zero();
                if (frame == 0)
                    return -20;
                map_page(page, frame, flags);
            } else {
                /* Already identity-mapped: re-assert flags (W/U). */
                uint32_t base = phys & ~0xFFFu;
                map_page(page, base, flags);
            }
        }

        /* 2. Zero bss + copy file data (memset/memcpy via virtual addr). */
        memset((void *)vaddr, 0, memsz);
        if (filesz > 0)
            memcpy((void *)vaddr, img + offset, filesz);
    }

    /* Flush any stale TLB entries is done inside map_page (invlpg). */
    if (entry_out)
        *entry_out = eh->e_entry;
    return 0;
}

int elf_load_file(const char *path, uint32_t *entry_out) {
    int fd = fs_open(path, 0);
    if (fd < 0)
        return -30;
    uint32_t sz = fs_size(fd);
    if (sz == 0 || sz > 512 * 1024)
        return -31; /* empty or unreasonably large for 4MB window */
    uint8_t *buf = (uint8_t *)kmalloc(sz);
    if (!buf)
        return -32;
    int n = fs_read(fd, buf, sz);
    if (n < 0 || (uint32_t)n < sz) {
        /* fs_read may return fewer bytes if file shrank; use what we got */
        if (n <= 0) {
            kfree(buf);
            return -33;
        }
        sz = (uint32_t)n;
    }
    int rc = elf_load(buf, sz, entry_out);
    kfree(buf);
    return rc;
}

int elf_exec(const char *path, int argc, char **argv) {
    uint32_t entry = 0;
    int rc = elf_load_file(path, &entry);
    if (rc != 0)
        return rc;
    /* Synchronous call in the current (ring-0, shared address space)
     * context. User entry follows C cdecl: int _start(int, char**). */
    int (*user_main)(int, char **) = (int (*)(int, char **))entry;
    return user_main(argc, argv);
}

void elf_info(const uint8_t *img, uint32_t size) {
    if (!img || size < sizeof(Elf32_Ehdr)) {
        vga_print("elf: too small for header\n");
        return;
    }
    const Elf32_Ehdr *eh = (const Elf32_Ehdr *)img;
    vga_print("ELF entry: ");
    print_hex(eh->e_entry);
    vga_print("  phoff=");
    print_dec(eh->e_phoff);
    vga_print("  phnum=");
    print_dec(eh->e_phnum);
    vga_print("\n");
    if (eh->e_phoff >= size) {
        vga_print("elf: phoff outside file\n");
        return;
    }
    const Elf32_Phdr *ph = (const Elf32_Phdr *)(img + eh->e_phoff);
    for (int i = 0; i < eh->e_phnum; i++) {
        uint32_t ph_addr = (uint32_t)&ph[i];
        uint32_t img_end = (uint32_t)img + size;
        if (ph_addr + sizeof(Elf32_Phdr) > img_end)
            break;
        vga_print(" PH");
        print_dec(i);
        vga_print(": type=");
        print_dec(ph[i].p_type);
        vga_print(" off=");
        print_hex(ph[i].p_offset);
        vga_print(" vaddr=");
        print_hex(ph[i].p_vaddr);
        vga_print(" filesz=");
        print_dec(ph[i].p_filesz);
        vga_print(" memsz=");
        print_dec(ph[i].p_memsz);
        vga_print(" flags=");
        print_dec(ph[i].p_flags);
        vga_print("\n");
    }
}

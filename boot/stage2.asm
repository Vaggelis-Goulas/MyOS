[BITS 16]
[ORG 0x7E00]

; ---------------------------------------------------------------------------
; Stage 2 -- kernel loader and protected-mode switch.
;
; The image (linker script KERNEL_BASE) is linked at 0x8000 and the linker
; refuses to build anything larger than KERNEL_LIMIT, so the load is always
; exactly these two reads:
;
;   LBA 2..65   (64 sectors) -> 0x00008000   CHS C0/H0/S3
;   LBA 66..129 (64 sectors) -> 0x00010000   CHS C1/H1/S13
;
; Why not one read: the floppy DMA engine can only describe a transfer that
; stays inside a single 64 KiB page, so a 64 KiB image at 0x8000 has to be
; split at the 0x10000 boundary. Asking for the whole range in one int 13h
; call comes back as AH=09h (DMA boundary crossing).
;
; Why not LBA reads: AH=42h is not implemented by every BIOS for floppies
; (SeaBIOS answers AH=01h), so CHS is used unconditionally. It is also the
; only form that works on a raw 1.44M image with no partition table.
; ---------------------------------------------------------------------------
KERNEL_BASE   equ 0x8000        ; must match C/linker.ld KERNEL_BASE
KERNEL_LIMIT  equ 0x10000       ; 64 KiB: enforced by the linker ASSERT
PART1_SECTORS equ 64            ; 32 KiB, fills 0x8000..0xFFFF
PART2_SECTORS equ 64            ; 32 KiB, fills 0x10000..0x1FFFF

    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov ax, 0x0003                ; 80x25 text mode
    int 0x10

    in al, 0x92                   ; fast A20 gate
    or al, 2
    out 0x92, al

    ; ---- image part 1: sectors 2..65 -> 0x8000 ----
    mov ax, KERNEL_BASE >> 4
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, PART1_SECTORS
    mov ch, 0                     ; cylinder
    mov cl, 3                     ; sector (LBA 2 == C0/H0/S3)
    mov dh, 0                     ; head
    mov dl, 0x00                  ; drive A:
    int 0x13
    jc disk_error

    ; ---- image part 2: sectors 66..129 -> 0x10000 ----
    mov ax, (KERNEL_BASE + PART1_SECTORS * 512) >> 4
    mov es, ax
    xor bx, bx
    mov ah, 0x02
    mov al, PART2_SECTORS
    mov ch, 1                     ; cylinder  (LBA 66 == C1/H1/S13)
    mov cl, 13                    ; sector
    mov dh, 1                     ; head
    mov dl, 0x00                  ; drive A:
    int 0x13
    jc disk_error

protected:
    cli
    lgdt [gdt_desc]

    mov eax, cr0
    or al, 1                      ; set PE
    mov cr0, eax

    jmp 0x08:pmode

gdt:
dq 0                             ; null
dw 0xFFFF, 0, 0x9A00, 0x00CF     ; 0x08 code, ring 0, 32-bit
dw 0xFFFF, 0, 0x9200, 0x00CF     ; 0x10 data, ring 0, 32-bit

gdt_desc:
dw 23
dd gdt

disk_error:
    ; A failed load leaves no kernel to jump to, so there is nothing
    ; sensible left to do. Park the CPU with interrupts off instead of
    ; executing whatever happens to follow the image on the floppy.
    cli
    hlt
    jmp disk_error

[BITS 32]
pmode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000

    jmp 0x08:KERNEL_BASE

times 512 db 0
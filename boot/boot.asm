[BITS 16]
[ORG 0x7C00]

start:
cli
xor ax, ax
mov ds, ax
mov es, ax
mov ss, ax
mov sp, 0x7C00
sti

mov ax, 0x0003
int 0x10

mov ah, 0x02
mov al, 1
mov ch, 0
mov cl, 2
mov dh, 0
mov dl, 0
mov bx, 0x7E00
int 0x13

jmp 0x0000:0x7E00

times 510-($-$$) db 0
dw 0xAA55
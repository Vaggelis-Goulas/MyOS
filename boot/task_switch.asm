[BITS 32]
global task_switch
extern schedule

; task_context_t layout (matching C struct):
; uint32_t eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
; uint32_t cs, ds, es, fs, gs, ss;
; Total: 16 * 4 = 64 bytes

%define CTX_EIP     0
%define CTX_EFLAGS  4
%define CTX_EAX     8
%define CTX_ECX     12
%define CTX_EDX     16
%define CTX_EBX     20
%define CTX_ESP     24
%define CTX_EBP     28
%define CTX_ESI     32
%define CTX_EDI     36
%define CTX_CS      40
%define CTX_DS      44
%define CTX_ES      48
%define CTX_FS      52
%define CTX_GS      56
%define CTX_SS      60
%define CTX_SIZE    64

task_switch:
    ; Entered via INT 62. Stack frame (ring 0, no privilege change):
    ; [ESP]     = EIP
    ; [ESP+4]   = CS
    ; [ESP+8]   = EFLAGS
    
    ; Save segment registers
    push ds
    push es
    push fs
    push gs
    
    ; Save general registers
    push eax
    push ecx
    push edx
    push ebx
    push esp        ; Placeholder for ESP, will fix
    push ebp
    push esi
    push edi
    
    ; Fix ESP value: original ESP = current ESP + 48 (12 pushes * 4)
    mov eax, esp
    add eax, 48
    mov [esp + 44], eax   ; ds is at offset 44
    
    ; Save EIP, CS, EFLAGS from interrupt frame (at esp+48, esp+52, esp+56)
    mov eax, [esp + 48]
    mov [esp + CTX_EIP], eax
    mov eax, [esp + 52]
    mov [esp + CTX_CS], eax
    mov eax, [esp + 56]
    mov [esp + CTX_EFLAGS], eax
    
    ; Save SS (kernel data segment)
    mov eax, 0x10
    mov [esp + CTX_SS], eax
    
    ; Move segment registers to correct positions in context
    mov eax, [esp + 44]   ; ds
    mov [esp + CTX_DS], eax
    mov eax, [esp + 40]   ; es
    mov [esp + CTX_ES], eax
    mov eax, [esp + 36]   ; fs
    mov [esp + CTX_FS], eax
    mov eax, [esp + 32]   ; gs
    mov [esp + CTX_GS], eax
    
    ; Now [esp] through [esp+60] contain a complete task_context_t
    ; Call schedule with pointer to this context
    push esp
    call schedule
    add esp, 4
    
    ; schedule() has updated the context with the NEXT task's state
    ; Extract new stack pointer
    mov eax, [esp + CTX_ESP]
    
    ; Pre-write iret frame on NEW stack (at new_esp - 12)
    ; We can safely write to the new task's stack memory before switching
    mov ecx, [esp + CTX_EFLAGS]
    mov [eax - 4], ecx       ; EFLAGS at new_esp - 4
    mov ecx, [esp + CTX_CS]
    mov [eax - 8], ecx       ; CS at new_esp - 8
    mov ecx, [esp + CTX_EIP]
    mov [eax - 12], ecx      ; EIP at new_esp - 12
    
    ; New stack pointer will be new_esp - 12 (points to iret frame)
    sub eax, 12
    
    ; Restore segment registers from context
    mov ecx, [esp + CTX_DS]
    mov ds, ecx
    mov ecx, [esp + CTX_ES]
    mov es, ecx
    mov ecx, [esp + CTX_FS]
    mov fs, ecx
    mov ecx, [esp + CTX_GS]
    mov gs, ecx
    
    ; Restore general registers from context
    mov eax, [esp + CTX_EAX]
    mov ecx, [esp + CTX_ECX]
    mov edx, [esp + CTX_EDX]
    mov ebx, [esp + CTX_EBX]
    mov ebp, [esp + CTX_EBP]
    mov esi, [esp + CTX_ESI]
    mov edi, [esp + CTX_EDI]
    
    ; Switch to new stack (which has iret frame ready)
    mov esp, eax
    
    ; Return to new task via iret
    iret
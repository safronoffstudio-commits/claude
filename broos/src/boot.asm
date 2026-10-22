; BroOS boot code: Multiboot2 header, 32-bit entry from GRUB,
; identity-mapped paging for the first 4 GiB, switch to 64-bit long mode.

MB2_MAGIC     equ 0xE85250D6
MB2_ARCH_I386 equ 0

section .multiboot_header
align 8
mb2_header:
    dd MB2_MAGIC
    dd MB2_ARCH_I386
    dd mb2_header_end - mb2_header
    dd -(MB2_MAGIC + MB2_ARCH_I386 + (mb2_header_end - mb2_header))

    ; Framebuffer request: 1024x768x32 preferred, GRUB may pick another mode.
    align 8
    dw 5        ; type: framebuffer
    dw 1        ; flags: optional
    dd 20
    dd 1024
    dd 768
    dd 32

    ; End tag.
    align 8
    dw 0
    dw 0
    dd 8
mb2_header_end:

section .bss
align 4096
global pml4
pml4:   resb 4096
pdpt:   resb 4096
pd:     resb 4096 * 4           ; 4 page directories * 1 GiB = first 4 GiB
align 16
stack_bottom:
    resb 65536
global stack_top
stack_top:

section .rodata
align 8
gdt64:
    dq 0                        ; null
    dq 0x00AF9A000000FFFF       ; 0x08: 64-bit code, ring 0
    dq 0x00CF92000000FFFF       ; 0x10: data, ring 0
gdt64_end:
gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dq gdt64

no_lm_msg: db "BroOS: this CPU is not 64-bit (x86_64 required).", 0

section .text
bits 32
global _start
_start:
    cli
    mov esp, stack_top
    mov edi, eax                ; multiboot2 magic
    mov esi, ebx                ; multiboot2 info pointer

    ; Long mode supported?
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode

    ; PML4[0] -> PDPT
    mov eax, pdpt
    or eax, 0x3
    mov [pml4], eax
    mov dword [pml4 + 4], 0

    ; PDPT[0..3] -> PD[0..3]
    xor ecx, ecx
.fill_pdpt:
    mov eax, ecx
    shl eax, 12
    add eax, pd
    or eax, 0x3
    mov [pdpt + ecx * 8], eax
    mov dword [pdpt + ecx * 8 + 4], 0
    inc ecx
    cmp ecx, 4
    jne .fill_pdpt

    ; 2048 x 2 MiB pages, present + writable + huge
    xor ecx, ecx
.fill_pd:
    mov eax, ecx
    shl eax, 21
    or eax, 0x83
    mov [pd + ecx * 8], eax
    mov dword [pd + ecx * 8 + 4], 0
    inc ecx
    cmp ecx, 2048
    jne .fill_pd

    mov eax, pml4
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5              ; PAE
    mov cr4, eax

    mov ecx, 0xC0000080         ; EFER
    rdmsr
    or eax, 1 << 8              ; LME
    wrmsr

    mov eax, cr0
    or eax, (1 << 31) | 1       ; PG | PE
    and eax, ~((1 << 30) | (1 << 29)) ; make sure caches are on (CD=0, NW=0)
    mov cr0, eax

    lgdt [gdt64_ptr]
    jmp 0x08:long_mode_entry

.no_long_mode:
    ; Legacy VGA text output works on BIOS machines only, which is exactly
    ; where a 32-bit-only CPU could show up.
    mov esi, no_lm_msg
    mov edi, 0xB8000
.print:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x4F
    stosw
    jmp .print
.halt:
    hlt
    jmp .halt

bits 64
extern kmain
long_mode_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax
    mov rsp, stack_top
    mov edi, edi                ; zero-extend magic
    mov esi, esi                ; zero-extend info pointer
    call kmain
.hang:
    cli
    hlt
    jmp .hang

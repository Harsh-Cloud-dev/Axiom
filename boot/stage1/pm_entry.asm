; =========================================================
; Stage1 — pm_entry.asm
; Loads Stage2, Stage3, queries E820 memory map,
; then enters Protected Mode and jumps to Stage2.
;
; E820 map stored at 0x500:
;   0x500        : uint32_t  entry_count
;   0x504 + N*24 : e820_entry_t[N]
;
; Each e820_entry_t (24 bytes):
;   uint64_t base
;   uint64_t length
;   uint32_t type      (1=usable, 2=reserved, ...)
;   uint32_t acpi_ext  (extended attrs, can be 0)
; =========================================================
[org  0x8000]
[bits 16]

E820_MAP_ADDR   equ 0x500   ; where we store the map
E820_MAGIC      equ 0x534D4150  ; 'SMAP'

_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [BOOT_DRIVE], dl

    mov  si, msg_s1
    call print16

    ;Load Stage2 → 0x9000
    xor ax, ax
    mov es, ax
    mov bx, 0x9000
    mov ah, 0x02
    mov al, 2
    mov ch, 0
    mov cl, 5
    mov dh, 0
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc  .disk_err

    ;  Load Stage3 → 0xA000
    xor ax, ax
    mov es, ax
    mov bx, 0xA000
    mov ah, 0x02
    mov al, 2
    mov ch, 0
    mov cl, 9
    mov dh, 0
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc  .disk_err

    ;  Query E820 memory map 
    call e820_query

    ;Enable A20
    in  al, 0x92
    or  al, 2
    out 0x92, al

    ; Load GDT and enter Protected Mode
    lgdt [gdt_desc]

    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry

.disk_err:
    mov  si, msg_err
    call print16
.hang:
    cli
    hlt
    jmp .hang

; ── e820_query ────────────────────────────────────────────
; Calls BIOS int 0x15 EAX=0xE820 in a loop.
; Stores entries at E820_MAP_ADDR+4, count at E820_MAP_ADDR.
e820_query:
    xor  ax, ax
    mov  es, ax
    mov  di, E820_MAP_ADDR + 4
    xor  ebx, ebx
    xor  bp, bp
.next_entry:
    mov  eax, 0xE820
    mov  edx, E820_MAGIC
    mov  ecx, 24
    int  0x15
    jc   .done
    cmp  eax, E820_MAGIC
    jne  .done
    ; skip zero-length entries
    cmp  dword [es:di+8], 0
    je   .skip_zero
    add  di, 24
    inc  bp
.skip_zero:
    test ebx, ebx
    jz   .done
    cmp  bp, 64
    jge  .done
    jmp  .next_entry
.done:
    mov  [E820_MAP_ADDR], bp
    ret

print16:
    lodsb
    or  al, al
    jz  .done
    mov ah, 0x0E
    int 0x10
    jmp print16
.done:
    ret

align 8
gdt_start:
    dq 0x0000000000000000   ; null
    dq 0x00CF9A000000FFFF   ; 0x08 32-bit code
    dq 0x00CF92000000FFFF   ; 0x10 32-bit data
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

BOOT_DRIVE  db 0
msg_s1      db "S1OK", 13, 10, 0
msg_err     db "S1ERR", 13, 10, 0

[bits 32]
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov esp, 0x9FC00

    mov word [0xB8000], 0x2F50   
    mov word [0xB8002], 0x2F4D   

    jmp 0x9000
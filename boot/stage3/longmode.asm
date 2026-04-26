org  0xA000
bits 32

%define PML4 0x1000
%define PDPT 0x2000
%define PD   0x3000

_start:
    ; set data segments
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    ; zero all three page tables (0x1000 - 0x3FFF)
    xor  eax, eax
    mov  edi, PML4
    mov  ecx, (3 * 4096) / 4
    rep  stosd

    ; PML4[0] → PDPT
    mov dword [PML4],   (PDPT | 3)
    mov dword [PML4+4], 0

    ; PDPT[0] → PD
    mov dword [PDPT],   (PD | 3)
    mov dword [PDPT+4], 0

    ; PD entries — map first 128MB using 2MB huge pages
    ; each entry covers 2MB, 64 entries = 128MB
    mov edi, PD
    mov eax, 0x83          ; present + writable + huge page (2MB)
    mov ecx, 64            ; 64 entries
.map_loop:
    mov dword [edi],   eax
    mov dword [edi+4], 0
    add edi, 8             ; next PD entry (8 bytes each)
    add eax, 0x200000      ; next 2MB physical block
    loop .map_loop

    ; enable PAE (Physical Address Extension) — required for long mode
    mov eax, cr4
    or  eax, (1 << 5)
    mov cr4, eax

    ; load PML4 into CR3
    mov eax, PML4
    mov cr3, eax

    ; enable long mode in EFER MSR
    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8)
    wrmsr

    ; enable paging (activates long mode)
    mov eax, cr0
    or  eax, 0x80000000
    mov cr0, eax

    ; load 64-bit GDT and far jump to 64-bit code
    lgdt [gdt64_desc]
    jmp  dword 0x08:lm_entry

align 8
gdt64_start:
    dq 0x0000000000000000   ; NULL descriptor
    dq 0x00209A0000000000   ; 64-bit code segment (L=1, P=1, DPL=0)
    dq 0x00CF920000000000   ; 64-bit data segment (limit=4GB) ← fixed
gdt64_end:

gdt64_desc:
    dw gdt64_end - gdt64_start - 1
    dd gdt64_start

bits 64
lm_entry:
    ; reload data segments with 64-bit selectors
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax

    ; set up proper 64-bit stack
    ; putting it at 0x7C00 direction — just below Stage 0
    ; safe, in mapped region, 128MB covers this
    mov rsp, 0x9FC00

    ; debug markers on VGA (so we know we reached long mode)
    mov word [0xB800C], 0x1F4C 
    mov word [0xB800E], 0x1F4D 

    ; jump to kernel
    mov rax, 0x100000
    jmp rax
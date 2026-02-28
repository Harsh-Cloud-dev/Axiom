BITS 16
ORG 0x7C00
;do not make any changes!
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [BOOT_DRIVE], dl

    in  al, 0x92
    or  al, 2
    out 0x92, al
    sti

    mov si, msg_ok
    call print16

    xor ax, ax
    mov es, ax
    mov bx, 0x8000
    mov ah, 0x02
    mov al, 1
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc  .err

    jmp 0x0000:0x8000

.err:
    mov si, msg_err
    call print16
.hang:
    cli
    hlt
    jmp .hang

print16:
    lodsb
    or  al, al
    jz  .done
    mov ah, 0x0E
    int 0x10
    jmp print16
.done:
    ret

BOOT_DRIVE db 0
msg_ok     db "S0OK", 13, 10, 0
msg_err    db "S0ERR", 13, 10, 0

times 510-($-$$) db 0
dw 0xAA55
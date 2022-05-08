bits 16
org 0x7c00
top:
        push dx                 ; dl is set by BIOS to drive number we're loaded from, and we want it for int 13h

        jmp start

loading:
        db "Loading subsequent sectors...", 0x0d, 0x0a, 0
loaded:
        db "Loaded!", 0x0d, 0x0a, 0
msg32bit:
        db "In 32-bit protected mode!",0

teleprint:
        mov ah, 0x0e            ; Teletype output
.loop:
        mov al, [bx]
        cmp al, 0
        jz done

        int 10h
        inc bx
        jmp .loop
done:
        ret

start:
        ; ; Move cursor to first position at upper left
        ; mov ah, 2               ; set cursor position
        ; mov bh, 0               ; page
        ; mov dh, 0               ; row
        ; mov dl, 0               ; column
        ; int 0x10

        ; ; Then clear screen by filling with spaces
        ; mov ah, 0x0a            ; write character
        ; mov al, ' '             ; character
        ; mov cx, 80*25           ; how many times to print it
        ; int 10h

        ; Clear screen by setting VGA mode (to the normal mode we're already in)
        mov ax, 0x3             ; ah: 0 (set video mode); al: 3 (80x25 text with colors)
        int 0x10

        ; Turn off the cursor
        mov ah, 1               ; set cursor size
        mov ch, 1<<5            ; bit 5 disables cursor
        int 10h

        mov bx, loading
        call teleprint

        ;; Based on https://en.wikipedia.org/wiki/INT_13H
        xor ax, ax    ; Idiomatic, and better in some ways than mov ax, 0
        mov ds, ax    ; ds and es need to be set to 0, and mov needs another register
        mov es, ax
        cld
        mov ah, 2     ; Int 13h function 2: "Read sectors from drive"
        mov al, 8     ; How many sectors to read -- change as necessary
        mov ch, 0     ; Cylinder
        mov cl, 2     ; 1-indexed sector to start reading from
        pop dx        ; dl is drive number, and BIOS set it to the drive number we're loaded from (which we
                      ;   want to keep reading from).  We pushed dx first thing so we can restore it now.
        mov dh, 0     ; Head
        mov bx, top+512 ; Let's load it right after us
        int 13h
        jmp top+512

        ; I just checked, and as expected and hoped, NASM will complain if we put too much above, because
        ;  this vaule will end up being negative, so we can't assemble.  So go ahead and put as much above
        ;  here as feels right; we'll find out if we pass 510 bytes and need to move some stuff down.
        times 510 - ($-$$) db 0
        dw 0xaa55

        ; Now past the 512-byte limitation; do what you need to do, for the most part

        mov bx, loaded
        call teleprint


        ; Enable A20 bit
        mov ax, 0x2401
        int 0x15

        cli
        lgdt [gdt_pointer]

        ; Set the protected mode bit of CR0
        mov eax, cr0
        or eax, 1
        mov cr0, eax

        jmp CODE_SEG:start2

        ; Access byte format:
        ; 8: Segment is present (1)
        ; 7-6: ring (0)
        ; 5: Descriptor (1 for code or data segments, 0 for task state segments)
        ; 4: Executable (1 for code, 0 for data)
        ; 3: Direction for data (0 grows up, 1 grows down) / conforming for code (0 this ring only, 1 this or lower)
        ; 2: Readable (for code) / writable (for data)
        ; 1: CPU access (leave clear)
        PRESENT equ 0b1000_0000
        RING0 equ 0b0000_0000
        RING1 equ 0b0010_0000
        RING2 equ 0b0100_0000
        RING3 equ 0b0110_0000
        NONTSS equ 0b0001_0000
        TSS equ 0b0000_0000
        CODESEG equ 0b0000_1000
        DATASEG equ 0b0000_0000
        DATADOWN equ 0b0000_0100
        DATAUP equ 0b0000_0000
        CONFORMING equ 0b0000_0100
        READABLE equ 0b0000_0010
        WRITABLE equ 0b0000_0010

gdt_start:
        dq 0
gdt_code:
        dw 0xFFFF               ; Limit (top) of segment (16 of 20 bits)
        dw 0x0000               ; Base of segment (16 of 32 bits)
        db 0x00                 ; 8 more bits of base
        db PRESENT | RING0 | NONTSS | CODESEG | READABLE ; Access byte
        ;db 1001_1010b           ; Access byte
        db 1100_1111b           ; 4 bits of flags, and last 4 bits of limit
        ; 4: Granularity (0 for 1-byte blocks; 1 for 4-KiB blocks)
        ; 3: Size (0 for 16-bit protected-mode segment; 1 for 32-bit protected-mode segment)
        ; 2: Long mode (1 for 64-bit protected mode segment: clear bit 3 if turning this bit on; 64 is not 32 or 16)
        ; 1: Reserved
        db 0x00                 ; 8 more bits of base
gdt_data:
        dw 0xFFFF
        dw 0x0000
        db 0x00
        db PRESENT | RING0 | NONTSS | DATASEG | WRITABLE
        ;db 1001_0010b
        db 1100_1111b
        db 0x00
gdt_end:
gdt_pointer:
        dw gdt_end - gdt_start-1
        dd gdt_start

        CODE_SEG equ gdt_code - gdt_start
        DATA_SEG equ gdt_data - gdt_start

bits 32

start2:
        ; Set up segment registers
        mov ax, DATA_SEG
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax

        mov eax, 0xb8000+320
        mov ebx, msg32bit
        mov ch, 0x05
        call print

        hlt

print:
        mov cl, [ebx]
        cmp cl, 0
        jz .done

        mov byte [eax], cl
        inc eax
        mov byte [eax], ch
        inc eax
        inc ebx
        jmp print
.done:
        ret

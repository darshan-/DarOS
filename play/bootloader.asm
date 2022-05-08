bits 16
[org 0x7c00]
top:
        push dx                 ; dl is set by BIOS to drive number we're loaded from, and we want it for int 13h

        jmp start

loading:
        db "Loading...", 0x0d, 0x0a, 0
loaded:
        db "Loaded!", 0x0d, 0x0a, 0

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
        ; Move cursor to first position at upper left
        mov ah, 2               ; set cursor position
        mov bh, 0               ; page
        mov dh, 0               ; row
        mov dl, 0               ; column
        int 0x10

        ; Then clear screen by filling with spaces
        mov ah, 0x0a            ; write character
        mov al, ' '             ; character
        mov cx, 80*25           ; how many times to print it
        int 10h

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

        times 510 - ($-$$) db 0
        dw 0xaa55

        ; Now past the 512-byte limitation; do what you need to do, for the most part

        mov bx, loaded
        call teleprint

        ; Go to 32-bit protected mode, then jump to main.asm entry point?  See if I can ditch GRUB, at least for now?

        cli
        hlt

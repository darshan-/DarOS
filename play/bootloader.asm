bits 16
[org 0x7c00]
top:
        ;; Based on https://en.wikipedia.org/wiki/INT_13H
        xor ax, ax    ; Idiomatic, and better in some ways than mov ax, 0
        mov ds, ax    ; ds and es need to be set to 0, and mov needs another register
        mov es, ax
        cld
        ; dl is already set by BIOS to the drive number we're loaded from, which we want to keep reading from
        mov ah, 2     ; Int 13h function 2: "Read sectors from drive"
        mov al, 8     ; How many sectors to read -- change as necessary
        mov ch, 0     ; Cylinder
        mov cl, 2     ; 1-indexed sector to start reading from
        mov dh, 0     ; Head
        mov bx, top+512 ; Let's load it right after us
        int 13h
        jmp top+512

        times 510 - ($-$$) db 0
        dw 0xaa55

        ; Now past the 512-byte limitation; do what you need to do, for the most part

        mov bh, 0               ; page
        mov dh, 0               ; row
        mov dl, 0               ; column
        mov ah, 2               ; set cursor position
        int 0x10

        mov ah, 0x0a            ; write character
        mov al, ' '             ; character
        mov cx, 80*25           ; how many times to print it
        int 10h

        mov ah, 0x0e            ; Teletype output
        mov al, 'L'
        int 10h
        mov al, 'o'
        int 10h
        mov al, 'a'
        int 10h
        mov al, 'd'
        int 10h
        mov al, 'e'
        int 10h
        mov al, 'd'
        int 10h
        mov al, '!'
        int 10h

        cli
        hlt

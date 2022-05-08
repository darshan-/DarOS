; nasm hi.asm && hexdump -C hi && qemu-system-x86_64 -drive format=raw,file=hi

bits 16
        mov bh, 0               ; page
        mov dh, 0               ; row
        mov dl, 0               ; column
        mov ah, 2               ; set cursor position
        int 0x10

        mov ah, 0x0a            ; write character
        mov al, ' '             ; character
        mov cx, 80*25           ; how many times to print it
        int 10h

        mov ah, 0x0a            ; write character
        mov al, 'H'             ; character
        mov cx, 1               ; how many times to print it
        int 10h

        mov bh, 0               ; page
        mov dh, 0               ; row
        mov dl, 1               ; column
        mov ah, 2               ; set cursor position
        int 0x10

        mov ah, 0x0a            ; write character
        mov al, 'i'             ; character
        mov cx, 1               ; how many times to print it
        int 10h


        times 510 - ($-$$) db 0
        dw 0xaa55

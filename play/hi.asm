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

        mov ah, 0x0e            ; Teletype output
        mov al, 'H'
        int 10h
        mov al, 'i'
        int 10h
        mov al, '!'
        int 10h

        mov eax, 0xb8000+320
        mov ch, 0x07
        mov dx, sp
        call print_dx

        mov eax, 0xb8000+320+160
        mov ch, 0x07
        mov dx, bp
        call print_dx

        cli                     ; Aha!  Need to turn of interrupts first.
        hlt                     ; does nothing in real mode?
        ;jmp $

print_dx:
        add eax, 8

        call print_dx_2
        call print_dx_2
        ;; call print_edx_2
        ;; call print_edx_2

        ret

print_dx_2:
        mov cl, dl
        and cl, 0x0f
        call print_dx_1

        mov cl, dl
        shr cl, 4
        call print_dx_1

        shr dx, 8

        ret

print_dx_1:
        add cl, 0x30
        cmp cl, 0x3a
        jl pr                   ;0-9
        add cl, 7               ;a-f
pr:
        dec eax
        mov byte [eax], ch
        dec eax
        mov byte [eax], cl
        ret


        times 510 - ($-$$) db 0
        dw 0xaa55

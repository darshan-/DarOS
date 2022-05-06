global start

section .text

section .data
lin1: dd 0xb8000
msg1: db "This is DarOS.... in theory, anyway!",0

lin2: dd 0xb8000 + 160 + 160 + 160 + 6
msg2: db "Anyway, I hope you have a nice day!",0

lin3: dd 0xb8000 + 640 + 640 + 32
msg3: db "(It's pretty darn cool, though, right???)",0

bits 32
start:
	;; mov dword [0xb8000], 0x2f4b2f4f

        mov eax, [lin1]
        mov ebx, msg1
        mov dh, 0x0f
        call print

        mov eax, [lin2]
        mov ebx, msg2
        mov dh, 0x07
        call print

        mov eax, [lin3]
        mov ebx, msg3
        mov dh, 0x15
        call print

	hlt

print:
        mov cl, [ebx]
        cmp cl, 0
        jz done

        mov byte [eax], cl
        inc eax
        mov byte [eax], dh
        inc eax
        inc ebx
        jmp print
done:
        ret


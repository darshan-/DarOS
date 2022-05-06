global start
extern long_mode_start

section .text

section .data
lin1: dd 0xb8000
msg1: db "This is DarOS.... in theory, anyway!",0

lin2: dd 0xb8000 + 160 + 160 + 160 + 6
msg2: db "Anyway, I hope you have a nice day!",0

lin3: dd 0xb8000 + 640 + 640 + 32
msg3: db "(It's pretty darn cool, though, right???)",0

lin4: dd 0xb8000 + 640 + 640 + 320
msg4: db "start: 0x",0

lin5: dd 0xb8000 + 640 + 640 + 640
msg5: db "esp: 0x",0

lin6: dd 0xb8000 + 640 + 640 + 640 + 320
msg6: db "lin1: 0x",0

lin7: dd 0xb8000 + 640 + 640 + 640 + 640
msg7: db "stack_top: 0x",0

lin8: dd 0xb8000 + 640 + 640 + 640 + 640 + 320
msg8: db "Ready! esp now: 0x",0

bits 32
start:
        ; I want to see what's in esp before I set it...
        ; Was a small stack already set up for me by the assembler?
        ; Was it dangerous to issue the `call' instruction before I set up a stack?

        mov eax, [lin1]
        mov ebx, msg1
        mov ch, 0x0f
        call print

        mov eax, [lin2]
        mov ebx, msg2
        mov ch, 0x07
        call print

        mov eax, [lin3]
        mov ebx, msg3
        mov ch, 0x15
        call print

        mov eax, [lin4]
        mov ebx, msg4
        mov ch, 0x0f
        call print

        ;mov edx, 0x12345678
        ;mov edx, 0x09abcdef
        mov edx, start
        call print_edx

        mov eax, [lin5]
        mov ebx, msg5
        mov ch, 0x0f
        call print

        mov edx, esp
        call print_edx

        mov eax, [lin6]
        mov ebx, msg6
        mov ch, 0x0f
        call print

        mov edx, lin1
        call print_edx

        mov eax, [lin7]
        mov ebx, msg7
        mov ch, 0x0f
        call print

        mov edx, stack_top
        call print_edx

        mov esp, stack_top

        call setup_page_tables
	call enable_paging

        mov eax, [lin8]
        mov ebx, msg8
        mov ch, 0x0f
        call print

        mov edx, esp
        call print_edx

        lgdt [gdt64.pointer]
	jmp gdt64.code_segment:long_mode_start

	;hlt

print:
        mov cl, [ebx]
        cmp cl, 0
        jz done

        mov byte [eax], cl
        inc eax
        mov byte [eax], ch
        inc eax
        inc ebx
        jmp print
done:
        ret

print_edx:
        add eax, 16

        call print_edx_2
        call print_edx_2
        call print_edx_2
        call print_edx_2

        ret

print_edx_2:
        mov cl, dl
        and cl, 0x0f
        call print_edx_1

        mov cl, dl
        shr cl, 4
        call print_edx_1

        shr edx, 8

        ret

print_edx_1:
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


setup_page_tables:
	mov eax, page_table_l3
	or eax, 0b11 ; present, writable
	mov [page_table_l4], eax

	mov eax, page_table_l2
	or eax, 0b11 ; present, writable
	mov [page_table_l3], eax

	mov ecx, 0 ; counter
.loop:

	mov eax, 0x200000 ; 2MiB
	mul ecx
	or eax, 0b10000011 ; present, writable, huge page
	mov [page_table_l2 + ecx * 8], eax

	inc ecx ; increment counter
	cmp ecx, 512 ; checks if the whole table is mapped
	jne .loop ; if not, continue

	ret

enable_paging:
	; pass page table location to cpu
	mov eax, page_table_l4
	mov cr3, eax

	; enable PAE
	mov eax, cr4
	or eax, 1 << 5
	mov cr4, eax

	; enable long mode
	mov ecx, 0xC0000080
	rdmsr
	or eax, 1 << 8
	wrmsr

	; enable paging
	mov eax, cr0
	or eax, 1 << 31
	mov cr0, eax

	ret


section .bss
align 4096
page_table_l4:
	resb 4096
page_table_l3:
	resb 4096
page_table_l2:
	resb 4096
stack_bottom:
	resb 4096 * 4
stack_top:


section .rodata
gdt64:
	dq 0 ; zero entry
.code_segment: equ $ - gdt64
	dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53) ; code segment
.pointer:
	dw $ - gdt64 - 1 ; length
	dq gdt64 ; address

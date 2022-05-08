global start
extern long_mode_start

section .text

section .data
lin1: dd 0xb8000
msg1: db "Loading...       ",0  ; Extra spaces to clear Grub output

bits 32
start:
        mov esp, stack_top

        ; Disable the cursor, after so much digging, thanks to: https://wiki.osdev.org/Text_Mode_Cursor
	mov dx, 0x3D4
	mov al, 0xA	; low cursor shape register
	out dx, al
	inc dx
	mov al, 0b0010_0000	; bits 6-7 unused, bit 5 disables the cursor, bits 0-4 control the cursor shape
	out dx, al

        mov eax, [lin1]
        mov ebx, msg1
        mov ch, 0x05
        call print

        call setup_page_tables
	call enable_paging

        lgdt [gdt64.pointer]
	jmp gdt64.code_segment:long_mode_start

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

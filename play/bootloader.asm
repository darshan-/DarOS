bits 16
org 0x7c00
top:
        push dx                 ; dl is set by BIOS to drive number we're loaded from, and we want it for int 13h

        jmp start

loading: db "Loading subsequent sectors...", 0x0d, 0x0a, 0
loaded: db "Loaded!", 0x0d, 0x0a, 0
msg32bit: db "In 32-bit protected mode!",0
msg64bit: db "In 64-bit protected mode?",0

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

        ; Access byte format:
        ; 8: Segment is present (1)
        ; 7-6: ring (0)
        ; 5: Descriptor (1 for code or data segments, 0 for task state segments)
        ; 4: Executable (1 for code, 0 for data)
        ; 3: Direction for data (0 grows up, 1 grows down) / conforming for code (0 this ring only, 1 this or lower)
        ; 2: Readable (for code) / writable (for data)
        ; 1: CPU access (leave clear)
        PRESENT equ 1<<7
        RING0 equ 0
        RING1 equ 1<<5
        RING2 equ 2<<5
        RING3 equ 3<<5
        NONTSS equ 1<<4
        TSS equ 0
        CODESEG equ 1<<3
        DATASEG equ 0
        DATADOWN equ 1<<2
        DATAUP equ 0
        CONFORMING equ 1<<2
        READABLE equ 1<<1
        WRITABLE equ 1<<1

        ; Flags nibble format:
        ; 4: Granularity (0 for 1-byte blocks; 1 for 4-KiB blocks)
        ; 3: Size (0 for 16-bit protected-mode segment; 1 for 32-bit protected-mode segment)
        ; 2: Long mode (1 for 64-bit protected mode segment: clear bit 3 if turning this bit on; 64 is not 32 or 16)
        ; 1: Reserved

        GRAN4K equ 1<<7
        GRANBYTE equ 0
        MODE16 equ 0
        MODE32 equ 1<<6
        MODE64 equ 1<<5

gdt_start:
        dq 0
gdt_code:
        dd 0
        db 0
        db PRESENT | RING0 | NONTSS | CODESEG | READABLE ; Access byte
        db GRAN4K | MODE64                               ; Flags
        db 0
; gdt_code:
;         dw 0xffff               ; Limit (top) of segment (16 of 20 bits)
;         dw 0x0000               ; Base of segment (16 of 32 bits)
;         db 0x00                 ; 8 more bits of base
;         db PRESENT | RING0 | NONTSS | CODESEG | READABLE ; Access byte
;         db GRAN4K | MODE64 + 0xff ; 4 bits of flags and last 4 bits of limit
;         db 0x00                 ; 8 more bits of base
; gdt_data:
;         dw 0xffff
;         dw 0x0000
;         db 0x00
;         db PRESENT | RING0 | NONTSS | DATASEG | WRITABLE
;         db GRAN4K | MODE64 + 0xff
;         db 0x00
gdt_end:
gdt_pointer:
        dw gdt_end - gdt_start-1
        dd gdt_start

        CODE_SEG equ gdt_code - gdt_start
        ;DATA_SEG equ gdt_data - gdt_start

start:
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

        ; Set the protected mode bit of CR0
        mov eax, cr0
        or eax, 1
        mov cr0, eax

        call setup_page_tables
	call enable_paging

        lgdt [gdt_pointer]

        ;lgdt [gdt64.pointer]
	;jmp gdt64.code_segment:long_mode_start

        jmp CODE_SEG:start64

;bits 32

; print:
;         mov cl, [ebx]
;         cmp cl, 0
;         jz .done

;         mov byte [eax], cl
;         inc eax
;         mov byte [eax], ch
;         inc eax
;         inc ebx
;         jmp print
; .done:
;         ret

; start32:
;         ; Set up segment registers (The far jump down here set up CS)
;         mov ax, DATA_SEG
;         ;xor ax, ax
;         mov ds, ax
;         mov es, ax
;         mov fs, ax
;         mov gs, ax
;         mov ss, ax

;         mov eax, 0xb8000+320
;         mov ebx, msg32bit
;         mov ch, 0x05
;         call print

;         mov esp, stack_top

;         hlt

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

bits 64
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

start64:
        ; mov ax, 0
        ; mov ss, ax
        ; mov ds, ax
        ; mov es, ax
        ; mov fs, ax
        ; mov gs, ax

        ; Set up segment registers (The far jump down here set up CS)
        ;mov ax, DATA_SEG
        xor ax, ax
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax

        mov esp, stack_top

        mov eax, 0xb8000+320
        mov ebx, msg64bit
        mov ch, 0x05
        call print

        hlt


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

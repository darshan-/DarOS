        ; Access byte format:
        ; 8: Segment is present (1)
        ; 7-6: ring (0)
        ; 5: Descriptor (1 for code or data segments, 0 for task state segments)
        ; 4: Executable (1 for code, 0 for data)
        ; 3: Direction for data (0 grows up, 1 grows down) / conforming for code (0 this ring only, 1 this or lower)
        ; 2: Readable (for code) / writable (for data)
        ; 1: CPU access (leave clear)

        ; Access byte is bits 40-47, so bit shift from 40 to 47 for bits 0-7 of access byte
        PRESENT equ 1<<47
        RING0 equ 0
        RING1 equ 1<<45
        RING2 equ 2<<45
        RING3 equ 3<<45
        NONTSS equ 1<<44
        TSS equ 0
        CODESEG equ 1<<43
        DATASEG equ 0
        DATADOWN equ 1<<42
        DATAUP equ 0
        CONFORMING equ 1<<42
        READABLE equ 1<<41
        WRITABLE equ 1<<41

        ; Flags nibble format:
        ; 4: Granularity (0 for 1-byte blocks; 1 for 4-KiB blocks)
        ; 3: Size (0 for 16-bit protected-mode segment; 1 for 32-bit protected-mode segment)
        ; 2: Long mode (1 for 64-bit protected mode segment: clear bit 3 if turning this bit on; 64 is not 32 or 16)
        ; 1: Reserved

        ; Flags nibble is bits 52-55, so bit shift from 52 to 55 for bits 0-3 of access byte
        GRAN4K equ 1<<55
        GRANBYTE equ 0
        MODE16 equ 0
        MODE32 equ 1<<54
        MODE64 equ 1<<53

        ; 0x500-0x7bff available and a good place for stack and page tables
        ; Page tables need to be 0x1000 aligned.  Wasting 0x500 from 0x500 to 0x1000 is better
        ;   than wasting 0xbff from 0x7000 to 0x7bff, so doing it this way.
        page_table_l4 equ 0x1000
        page_table_l3 equ 0x2000
        page_table_l2 equ 0x3000
        stack_bottom equ 0x4000
        stack_top equ 0x7bff
        idt equ 0               ; 0-0x1000 available in long mode

        PTABLE_PRESENT equ 1
        PTABLE_WRITABLE equ 1<<1
        PTABLE_HUGE equ 1<<7

        PIC_PRIMARY_CMD equ 0x20
        PIC_PRIMARY_DATA equ 0x21
        PIC_SECONDARY_CMD equ 0xa0
        PIC_SECONDARY_DATA equ 0xa1

        ICW1 equ 1<<4
        ICW1_ICW4_NEEDED equ 1

bits 16
org 0x7c00
top:
        push dx                 ; dl is set by BIOS to drive number we're loaded from, and we want it for int 13h

        jmp start

loading: db "Loading sectors after boot sector...", 0x0d, 0x0a, 0
loaded: db "Sectors loaded!", 0x0d, 0x0a, 0
sect2running: db "Running from second sector code!", 0x0d, 0x0a, 0
msg64bit: db "In 64-bit protected mode!", 0
trap_gate_s: db "Trap gate: 0", 0
interrupt_gate_s: db "Interrupt gate: 0", 0
;trap_count: db 0

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

gdt64:
	dq 0
.code_segment: equ $ - gdt64
        dq PRESENT | RING0 | NONTSS | CODESEG | READABLE | GRAN4K | MODE64
.pointer:
	dw $ - gdt64 - 1        ; Length in bytes minus 1
	dq gdt64                ; Address

idtr:
        dw 4095                 ; Size of IDT minus 1
        dq 0                    ; Address

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
        mov bx, sect2 ; Let's load it right after us
        int 13h

        ; TODO: Check CF to see if error?

        mov bx, loaded
        call teleprint

        ; Apparently I'll want to ask BIOS about memory (https://wiki.osdev.org/Detecting_Memory_(x86))
        ;   while I'm still in real mode, probably somewhere around here, at some point.

        ; Enable A20 bit
        mov ax, 0x2401
        int 0x15

        ; Let BIOS know we're going to long mode, per https://wiki.osdev.org/X86-64
        mov ax, 0xec00
        mov bl, 2               ; We'll switch to long mode and stay there
        int 0x15

        cli

        ; l4 page has just the one entry for l3
	mov eax, page_table_l3 | PTABLE_PRESENT | PTABLE_WRITABLE
	mov [page_table_l4], eax

        ; l3 page has just the one entry for l2
	mov eax, page_table_l2 | PTABLE_PRESENT | PTABLE_WRITABLE
	mov [page_table_l3], eax

        ; l2 identity maps first 1 GB of memory with huge pages (512*2MB)
        mov eax, PTABLE_PRESENT | PTABLE_WRITABLE | PTABLE_HUGE
        mov ebx, page_table_l2
	mov ecx, 512
.loop:
        mov [ebx], eax
	add eax, 0x200000       ; Huge page bit makes for 2MB pages, so each page is this far apart
        add ebx, 8
        loop .loop

        ; Now we're ready to load and use tables
	mov eax, page_table_l4
	mov cr3, eax

	; Enable PAE
	mov eax, cr4
	or eax, 1 << 5
	mov cr4, eax

	; Enable long mode
	mov ecx, 0xC0000080
	rdmsr
	or eax, 1 << 8
	wrmsr

        jmp sect2

        ; I just checked, and as expected and hoped, NASM will complain if we put too much above, because
        ;  this vaule will end up being negative, so we can't assemble.  So go ahead and put as much above
        ;  here as feels right; we'll find out if we pass 510 bytes and need to move some stuff down.
        times 510 - ($-$$) db 0
        dw 0xaa55

sect2:
        mov bx, sect2running
        call teleprint

	; Enable paging and enter protected mode
	mov eax, cr0
	or eax, 1 << 31 | 1
	mov cr0, eax

        lgdt [gdt64.pointer]
	jmp gdt64.code_segment:start64

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

trap_gate:
        push rax
        push rbx
        push rcx

        mov eax, trap_gate_s
        mov bl, [trap_gate_s+11]
        inc bl
        mov [eax+11], bl
        mov eax, 0xb8000+960
        mov ebx, trap_gate_s
        mov ch, 0x07
        call print

        pop rcx
        pop rbx
        pop rax

        ; Don't return from divide-by-zero handler?  Others not to return from?
        iretq

interrupt_gate:
        push rax
        push rbx
        push rcx

        int 3                   ; Cool, seems to work fine!

        mov eax, interrupt_gate_s
        mov bl, [interrupt_gate_s+16]
        inc bl
        mov [eax+16], bl
        mov eax, 0xb8000+1120
        mov ebx, interrupt_gate_s
        mov ch, 0x07
        call print

        in al, 0x60

        mov al, 0x20
        out PIC_PRIMARY_CMD, al
        out PIC_SECONDARY_CMD, al

        pop rcx
        pop rbx
        pop rax

        iretq

start64:
        ; Set up segment registers (The far jump down here set up CS)
        ;mov ax, DATA_SEG
        xor ax, ax
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax

        mov esp, stack_top

        mov eax, 0xb8000+480
        mov ebx, msg64bit
        mov ch, 0x05
        call print

        ; Set up 31 trap gate entries starting at 0x0
        ; Then set up the rest (225) as interrupt gates?
        ; I guess have two routines, as they need to work slightly differently, one for traps, one for interrupts
        ; So for now make them all the same, of those two, and have them just print "trap gate" or "interrupt gate"?

        mov ebx, idt
        mov ecx, 32
loop_idt:
        mov rax, trap_gate
        mov [ebx], ax
        mov [ebx+4], rax
        mov word [ebx+2], gdt64.code_segment
        mov word [ebx+4], 1<<15 | 0b1111 << 8
        add ebx, 16
        loop loop_idt

        mov ecx, 224
loop_idt2:
        mov rax, interrupt_gate
        mov [ebx], ax
        mov [ebx+4], rax
        mov word [ebx+2], gdt64.code_segment
        mov word [ebx+4], 1<<15 | 0b1110 << 8
        add ebx, 16
        loop loop_idt2

        mov al, ICW1 | ICW1_ICW4_NEEDED
        out PIC_PRIMARY_CMD, al
        out PIC_SECONDARY_CMD, al

        mov al, 0x20
        out PIC_PRIMARY_DATA, al        ; Map primary PIC to 0x20 - 0x27
        mov al, 0x28
        out PIC_SECONDARY_DATA, al      ; Map secondary PIC to 0x28 - 0x2f

        mov al, 0x04
        out PIC_PRIMARY_DATA, al
        mov al, 0x02
        out PIC_SECONDARY_DATA, al

        mov al, 0x01
        out PIC_PRIMARY_DATA, al
        out PIC_SECONDARY_DATA, al

        xor al, al
        out PIC_PRIMARY_DATA, al
        out PIC_SECONDARY_DATA, al

        mov al, 0xfd
        out PIC_PRIMARY_DATA, al
        mov al, 0xff
        out PIC_SECONDARY_DATA, al

        lidt [idtr]

        sti

        int 3

        ; mov bl, 0
        ; div bl

        ; mov edi, 0xB8000              ; Set the destination index to 0xB8000.
        ; mov rax, 0x1720272037204720   ; Set the A-register to 0x1F201F201F201F20.
        ; mov ecx, 80*25*2/8            ; How many times to rep
        ; rep stosq

        call kernel_entry

        ; After handling an interrupt, we come back and execute the first instruction after hlt -- so loop
        ;   and hlt again
halt_loop:
        hlt
        jmp halt_loop

kernel_entry:

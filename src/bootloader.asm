        SZ_2MB equ 0x200000
        SZ_QW equ 8

        ; Segment Descriptor constants

        ; Access byte format:
        ; 8: Segment is present (1)
        ; 7-6: ring (0)
        ; 5: Descriptor (1 for code or data segments, 0 for task state segments)
        ; 4: Executable (1 for code, 0 for data)
        ; 3: Direction for data (0 grows up, 1 grows down) / conforming for code (0 this ring only, 1 this or lower)
        ; 2: Readable (for code) / writable (for data)
        ; 1: CPU access (leave clear)

        ; Access byte is bits 40-47, so bit shift from 40 to 47 for bits 0-7 of access byte
        SD_PRESENT equ 1<<47
        SD_RING0 equ 0
        SD_RING1 equ 1<<45
        SD_RING2 equ 2<<45
        SD_RING3 equ 3<<45
        SD_NONTSS equ 1<<44
        SD_TSS equ 0
        SD_CODESEG equ 1<<43
        SD_DATASEG equ 0
        SD_DATADOWN equ 1<<42
        SD_DATAUP equ 0
        SD_CONFORMING equ 1<<42
        SD_READABLE equ 1<<41
        SD_WRITABLE equ 1<<41

        ; Flags nibble format:
        ; 4: Granularity (0 for 1-byte blocks; 1 for 4-KiB blocks)
        ; 3: Size (0 for 16-bit protected-mode segment; 1 for 32-bit protected-mode segment)
        ; 2: Long mode (1 for 64-bit protected mode segment: clear bit 3 if turning this bit on; 64 is not 32 or 16)
        ; 1: Reserved

        ; Flags nibble is bits 52-55, so bit shift from 52 to 55 for bits 0-3 of access byte
        SD_GRAN4K equ 1<<55
        SD_GRANBYTE equ 0
        SD_MODE16 equ 0
        SD_MODE32 equ 1<<54
        SD_MODE64 equ 1<<53

        ; 0x500-0x7bff available and a good place for stack and page tables
        ; Page tables need to be 0x1000 aligned.  Wasting 0x500 from 0x500 to 0x1000 is better
        ;   than wasting 0xbff from 0x7000 to 0x7bff, so doing it this way.
        page_table_l4 equ 0x1000
        page_table_l3 equ 0x2000
        page_table_l2 equ 0x3000
        page_tables_l2 equ 0x100000
        stack_top equ 0x7bff
        idt equ 0               ; 0-0x1000 available in long mode

        ; Page Table constants
        PT_PRESENT equ 1
        PT_WRITABLE equ 1<<1
        PT_HUGE equ 1<<7

        CR4_PAE equ 1<<5
        CR0_PROTECTION equ 1
        CR0_PAGING equ 1 << 31

        MSR_IA32_EFER equ 0xC0000080 ; Extended Feature Enable Register
        EFER_LONG_MODE_ENABLE equ 1<<8

        ; Let's load 960 sectors, 120 at a time (128 is max at a time, 961 total is max in safe area)
        SECT_PER_LOAD equ 120
        LOAD_COUNT equ 8

        INT_0x10_TELETYPE equ 0x0e
        INT_0x13_LBA_READ equ 0x42

section .boot
bits 16
        jmp 0:start16           ; Make sure cs is set to 0
start16:
        xor ax, ax
        mov ds, ax
        mov es, ax

        mov esp, stack_top

        mov ah, 0
        mov al, 3h
        int 10h

        mov cx, LOAD_COUNT
	mov si, dap
        ; dl is set by BIOS to drive number we're loaded from, so just leave it as is
lba_read:
	mov ah, INT_0x13_LBA_READ      ; Must set on every loop, as ah gets return code
	int 0x13

        mov ax, [dap.toseg]
        add ax, (SECT_PER_LOAD*512)>>4 ; Increment segment rather than offset (segment is address shifted 4)
        mov [dap.toseg], ax

        mov eax, [dap.from]
        add eax, SECT_PER_LOAD
        mov [dap.from], eax
        jc lba_error
        loop lba_read

        jmp lba_success

lba_error:
        mov bx, lba_error_s
        call teleprint
        cli
        hlt

lba_success:
        mov bx, loaded
        call teleprint

        ; Apparently I'll want to ask BIOS about memory (https://wiki.osdev.org/Detecting_Memory_(x86))
        ;   while I'm still in real mode, probably somewhere around here, at some point.

        ; enable A20 bit
        mov ax, 0x2401
        int 0x15

        cli

        mov al, 0x0b            ; Disable NMIs too
        mov dx, 0x70
        out dx, al
        mov dx, 0x71
        in al, dx

        ; l4 page has just the one entry for l3
	mov eax, page_table_l3 | PT_PRESENT | PT_WRITABLE
	mov [page_table_l4], eax

        ; Let's load l3 with just one 1GB l2 table for now; more once in long mode
        mov eax, page_table_l2 | PT_PRESENT | PT_WRITABLE
        mov [page_table_l3], eax

        ; Each l2 table identity maps 1 GB of memory with huge pages (512*2MB)
        ; We'll set up just one now, and do the rest once in long mode
        mov eax, PT_PRESENT | PT_WRITABLE | PT_HUGE
        mov ebx, page_table_l2
	mov ecx, 512
l2_loop:
        mov [ebx], eax
	add eax, SZ_2MB       ; Huge page bit makes for 2MB pages, so each page is this far apart
        add ebx, SZ_QW
        loop l2_loop

        ; Now we're ready to load and use tables
	mov eax, page_table_l4
	mov cr3, eax

	; Enable PAE
	mov eax, cr4
	or eax, CR4_PAE
	mov cr4, eax

	; Enable long mode
	mov ecx, MSR_IA32_EFER
	rdmsr
	or eax, EFER_LONG_MODE_ENABLE
	wrmsr

        jmp sect2

loaded: db "Sectors loaded!", 0x0d, 0x0a, 0
sect2running: db "Running from second sector code!", 0x0d, 0x0a, 0
lba_error_s: db "LBA returned an error; please check AH for return code", 0x0d, 0x0a, 0

teleprint:
        mov ah, INT_0x10_TELETYPE
.loop:
        mov al, [bx]
        test al, al
        jz done
        ;cmp al, 0
        ;je done

        int 0x10
        inc bx
        jmp .loop
done:
        ret


        CODE_SEG equ 0+(8*1)    ; Code segment is 1st (and only, at least for now) segment
gdt:
	dq 0
        dq SD_PRESENT | SD_NONTSS | SD_CODESEG | SD_READABLE | SD_GRAN4K | SD_MODE64 ; Code segment
gdtr:
	dw $ - gdt - 1          ; Length in bytes minus 1
	dq gdt                  ; Address

idtr:
        dw 4095                 ; Size of IDT minus 1
        dq idt                  ; Address

dap:
        db 0x10                 ; Size of DAP (Disk Address Packet)
        db 0                    ; Unused; should be zero
.cnt:   dw SECT_PER_LOAD        ; Number of sectors to read, 0x80 (128) max; overwritten with number read
.to:    dw sect2
.toseg: dw 0                    ; segment
.from:  dq 1                    ; LBA number (sector to start reading from)


        BOOTABLE equ 1<<7
        times 440 - ($-$$) db 0
        db "PURP"
        dw 0
        db BOOTABLE
        db 0, 2, 0              ; CHS of partition start
        db 0x0b                 ; FAT32
        db 0, 41, 0             ; CHS of partition end
        dd 1                    ; LBA of partition start
        dd 40                   ; Number of sectors in partition
        dq 0
        dq 0
        dq 0
        dq 0
        dq 0
        dq 0
        dw 0xaa55

sect2:
        ; AMD manual says we have to enter protected mode before entering long mode, so it's possible that this
        ;   works in QEMU but wouldn't on real hardware...  Maybe do it the "right" way?
        ; ** On the other hand, https://forum.osdev.org/viewtopic.php?t=11093 has plenty of people saying it's
        ;   fine on real-world hardware, including AMD processors, despite the manual.  So let's leave it.

        lgdt [gdtr]

	; Enable paging and enter protected mode
	mov eax, cr0
	or eax, CR0_PAGING | CR0_PROTECTION
	mov cr0, eax

        jmp CODE_SEG:start64

bits 64
start64:
        ; Set up segment registers (the far jump down here set up cs)
        xor ax, ax
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax

        ;mov rsp, 0x100000
        ;mov rsp, stack_top
        ;mov rsp, 0x10fff0
        mov rsp, 0x30fff0

        lidt [idtr]


        mov eax, PT_PRESENT | PT_WRITABLE | PT_HUGE
        mov ebx, page_tables_l2
	mov ecx, 512*256
l2s_loop:
        mov [ebx], eax
	add eax, SZ_2MB       ; Huge page bit makes for 2MB pages, so each page is this far apart
        add ebx, SZ_QW
        loop l2s_loop

        mov eax, page_tables_l2 | PT_PRESENT | PT_WRITABLE
        mov ebx, page_table_l3
	mov ecx, 256
l3_loop2:
        mov [ebx], eax
	add eax, SZ_QW*512
        add ebx, SZ_QW
        loop l3_loop2


        ; Do we just use the new ones as necesary?  Are they cached?  Do we need this?
	; mov rax, page_table_l4
	; mov cr3, rax

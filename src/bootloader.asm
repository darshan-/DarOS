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
        int_15_mem_table equ 0x4000
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
        ; Well, VirtualBox, and apparently some other BIOSes, can only handle reading one sector at a time...
        SECT_PER_LOAD equ 1
        LOAD_COUNT equ 960

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

        mov dword [int_15_mem_table], 1
        mov ax, int_15_mem_table>>4
        mov es, ax
        mov di, 4

        SMAP equ 'S' << 24 | 'M' << 16 | 'A' << 8 | 'P' ; 0x534d4150 aka 'SMAP'

        mov ebx, 0
        mov edx, SMAP

smap_start:
        mov eax, 0xe820
        mov ecx, 20
        int 0x15
        jc smap_done
        mov edx, SMAP
        cmp eax, edx
        jne smap_done
        cmp ebx, 0
        je smap_done
        inc dword [int_15_mem_table]
        add di, 24
        jmp smap_start

smap_done:

        ; Enable A20 bit
        mov ax, 0x2401
        int 0x15

        cli

        mov al, 0x0b            ; Disable NMIs too
        mov dx, 0x70
        out dx, al
        mov dx, 0x71
        in al, dx

        jmp sect2

        ; ; TODO: Intel manual specifically says we need to be in protected mode *with paging enabled* before
        ; ;   entering long mode, and then turn it off.  So I guess that might be worth a try too?  AMD says
        ; ;   paging doesn't have to be on, only to turn it off if it is on.  And I've seen lots of examples
        ; ;   that don't, but presumably work on Intel hardware?

        ; mov eax, cr0
        ; or eax, CR0_PROTECTION
        ; mov cr0, eax

        ; jmp CODE_SEG32:start32

loaded: db "Sectors loaded!", 0x0d, 0x0a, 0
lba_error_s: db "LBA returned an error; please check AH for return code", 0x0d, 0x0a, 0

teleprint:
        mov ah, INT_0x10_TELETYPE
.loop:
        mov al, [bx]
        test al, al
        jz done

        int 0x10
        inc bx
        jmp .loop
done:
        ret


        CODE_SEG equ gdt.code - gdt
        ;CODE_SEG32 equ gdt.code32 - gdt
        ;DATA_SEG32 equ gdt.data32 - gdt
gdt:
        dq 0
.code:
        dq SD_PRESENT | SD_NONTSS | SD_CODESEG | SD_READABLE | SD_GRAN4K | SD_MODE64 ; Code segment
; .code32:
;         dw 0xFFFF
;         dw 0x0
;         db 0x0
;         db 10011010b
;         db 11001111b
;         db 0x0
; .data32:
;         dw 0xFFFF
;         dw 0x0
;         db 0x0
;         db 10010010b
;         db 11001111b
;         db 0x0
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
        ;db 0, 2, 0             ; CHS of partition start
        db 0, 0x28, 0x31        ; CHS of partition start
        db 0x0b                 ; FAT32
        ;db 0, 41, 0            ; CHS of partition end
        db 0, 0xe9, 0xff        ; CHS of partition end
        dd 0x800                ; LBA of partition start
        dd 0x7af000             ; Number of sectors in partition
        dq 0
        dq 0
        dq 0
        dq 0
        dq 0
        dq 0
        dw 0xaa55

sect2:

;bits 32
;start32:
        ; mov ax, DATA_SEG32
        ; mov ds, ax
        ; mov ss, ax
        ; mov esp, stack_top

        mov eax, page_table_l3 | PT_PRESENT | PT_WRITABLE
        mov [page_table_l4], eax

        mov eax, page_table_l2 | PT_PRESENT | PT_WRITABLE
        mov [page_table_l3], eax

        mov eax, PT_PRESENT | PT_WRITABLE | PT_HUGE
        mov ebx, page_table_l2
        mov ecx, 512
l2_loop:
        mov [ebx], eax
        add eax, SZ_2MB       ; Huge page bit makes for 2MB pages, so each page is this far apart
        add ebx, SZ_QW
        loop l2_loop

        mov eax, page_table_l4
        mov cr3, eax

        mov eax, cr4
        or eax, CR4_PAE
        mov cr4, eax

        mov ecx, MSR_IA32_EFER
        rdmsr
        or eax, EFER_LONG_MODE_ENABLE
        wrmsr

        lgdt [gdtr]

        mov eax, cr0
        or eax, CR0_PAGING | CR0_PROTECTION
        mov cr0, eax

        jmp CODE_SEG:start64

bits 64
start64:
        xor ax, ax
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax

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

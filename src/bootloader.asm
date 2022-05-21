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
        ;stack_bottom equ 0x4000
        rtc_loc equ 0x4000
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

        CMOS_REG_SEL equ 0x70
        CMOS_IO equ 0x71
        NMI_DISABLED equ 1<<7
        RTC_SEC equ 0
        RTC_MIN equ 0x02
        RTC_HR equ 0x04
        RTC_WKD equ 0x06
        RTC_DAY equ 0x07
        RTC_MTH equ 0x08
        RTC_YR equ 0x09
        RTC_CEN equ 0x32
        RTC_STA equ 0x0a
        RTC_STB equ 0x0a
        ;RTC_STA_UPDATING equ 7
        RTC_STA_UPDATING equ 1<<7

section .boot
bits 16
        mov esp, stack_top

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

        cli

        ; l4 page has just the one entry for l3
	mov eax, page_table_l3 | PT_PRESENT | PT_WRITABLE
	mov [page_table_l4], eax

        ; l3 page has just the one entry for l2
	mov eax, page_table_l2 | PT_PRESENT | PT_WRITABLE
	mov [page_table_l3], eax

        ; l2 identity maps first 1 GB of memory with huge pages (512*2MB)
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

        ; Let's make it easy for C to get location for RTC data with this file as the single source of truth
global RTC
RTC:    dq rtc_loc

dap:
        db 0x10                 ; Size of DAP (Disk Address Packet)
        db 0                    ; Unused; should be zero
.cnt:   dw SECT_PER_LOAD        ; Number of sectors to read, 0x80 (128) max; overwritten with number read
.to:    dw sect2
.toseg: dw 0                    ; segment
.from:  dq 1                    ; LBA number (sector to start reading from)

        ; I just checked, and as expected and hoped, NASM will complain if we put too much above, because
        ;  this vaule will end up being negative, so we can't assemble.  So go ahead and put as much above
        ;  here as feels right; we'll find out if we pass 510 bytes and need to move some stuff down.
        times 510 - ($-$$) db 0
        dw 0xaa55


sect2:
        mov bx, sect2running
        call teleprint

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

        ; If I ever make read_rtc global/extern, and call it from C, I'll want to cli first and sti last.
        ; If I call it from assembly, I want to etiher *not* do that, or do it before I've disabled interrupts
        ;   to set up long mode
read_rtc:
        mov rbx, rtc_loc
        mov cl, 0
        jmp .top

.store_or_compare:
        test cl, cl
        jnz .compare
        mov [rbx], al
        ret

.compare:
        cmp al, [rbx]
        je .same
        pop rbx
        push read_rtc           ; values were different, we need to start over
.same:
        ret

.top:
        mov al, RTC_STA
        out CMOS_REG_SEL, al
        in al, CMOS_IO
        ;cmp al, 0
        ;jne read_rtc
        ;shr al, RTC_STA_UPDATING
        and al, RTC_STA_UPDATING
        test al, al
        jnz read_rtc

        ; read each of the registers and store to their locations -- clock stuff and stb (format)
        ; then do the whole thing again but rather than storing, compare to last stored
        ; if they're the same, we're done, otherwise start over from scratch

        mov al, RTC_SEC
        out CMOS_REG_SEL, al
        in al, CMOS_IO
        ; So either store it or compare it here.  Maybe have caller (which can be us, and use a helper function,
        ;   so C doesn't have to mess with registers) set bl to 0 for first pass (store) and 1 for second pass
        ;   (compare)?

        ;test cl, cl
        ;jnz

        call .store_or_compare

        ret

start64:
        ; Set up segment registers (the far jump down here set up cs)
        xor ax, ax
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax

        call read_rtc
        lidt [idtr]

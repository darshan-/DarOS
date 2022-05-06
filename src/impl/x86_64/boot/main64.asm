global long_mode_start
extern kernel_main

section .text
bits 64
long_mode_start:
        ; load null into all data segment registers
        mov ax, 0
        mov ss, ax
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax

        mov dword [0xb8000 + 640 + 640 + 640 + 640 + 640], 0x3a6b3a4f
	;call kernel_main

        hlt

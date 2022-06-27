.DEFAULT_GOAL := out/boot.img

c_objects := $(patsubst src/kernel/%.c, build/%.o, $(wildcard src/kernel/*.c))

GCC_OPTS := -Wall -Wextra -c -ffreestanding -fno-stack-protector -mgeneral-regs-only -mno-red-zone -fno-PIC -mcmodel=large -momit-leaf-frame-pointer #--static-pie

LD_OPTS := -N --warn-common -T src/kernel/linker.ld #--print-map

include build/headers.mk

build out build/userspace:
	mkdir -p $@

# TODO: Doesn't account for headers including other local headers (could use gcc --freestanding -M instead?)
#   For now let's just have a rule against doing that, and deal with it if I ever need to do it.
build/headers.mk: src/kernel/*.c src/kernel/*.h | build
	cd src/kernel && for cf in *.c; do echo -n "build/$$cf" | sed 's/\.c$$/\.o: /'; grep "#include \"" "$$cf" | awk '{print $$2}' | awk -F '"' '{print "src/kernel/"$$2}' | xargs; done >../../build/headers.mk

build/bootloader.o: Makefile src/kernel/bootloader.asm | build
	nasm -f elf64 src/kernel/bootloader.asm -o build/bootloader.o

build/*.o: Makefile
build/%.o: src/kernel/%.c | build
	gcc $(GCC_OPTS) $< -o $@

build/userspace/app.o1: Makefile src/userspace/app.c | build/userspace
	gcc $(GCC_OPTS) src/userspace/app.c -o build/userspace/app.o1
build/userspace/app.o2: Makefile build/userspace/app.o1
	ld -o build/userspace/app.o2 -N --warn-common -T src/userspace/linker.ld build/userspace/app.o1
build/userspace/app.c: Makefile build/userspace/app.o2
	echo "#include <stdint.h>" >build/userspace/app.c
	echo "uint64_t app[] = {" >>build/userspace/app.c
	hexdump -v -e '1/8 "0x%xull," "\n"' build/userspace/app.o2 >>build/userspace/app.c
	echo "0};" >>build/userspace/app.c
	echo -n "uint64_t app_len = " >>build/userspace/app.c
	wc -c <build/userspace/app.o2 | tr -d '\n' >>build/userspace/app.c
	echo " / 8 + 1;" >>build/userspace/app.c
build/userspace/app.o: Makefile build/userspace/app.c
	gcc $(GCC_OPTS) build/userspace/app.c -o build/userspace/app.o

out/boot.img: $(c_objects) src/kernel/linker.ld build/bootloader.o build/userspace/app.o | out
	ld -o out/boot.img $(LD_OPTS) build/bootloader.o $(c_objects) build/userspace/app.o

out/bochs.img: out/boot.img
	cp out/boot.img out/bochs.img
	truncate -s 499712 out/bochs.img

# -display gtk,zoom-to-fit=on
# -full-screen
# -cpu host 
.PHONY: run
run: out/boot.img
	qemu-system-x86_64 -rtc base=localtime -enable-kvm -m 4G -drive format=raw,file=out/boot.img -d int -no-reboot -no-shutdown

.PHONY: run-bochs
run-bochs: out/bochs.img
	rm -f out/bochs.img.lock
	echo c >out/bochs.command
	bochs -qf /dev/null -rc out/bochs.command 'memory: host=128, guest=512' 'boot: disk' 'ata0-master: type=disk, path="out/bochs.img", mode=flat, cylinders=4, heads=4, spt=61, sect_size=512, model="Generic 1234", biosdetect=auto, translation=auto' 'magic_break: enabled=1' 'clock: sync=realtime, time0=local, rtc_sync=1' 'vga: update_freq=30' 'romimage: options=fastboot' 'com1: enabled=1, mode=file, dev=out/bochs-serial.txt'

.PHONY: clean
clean:
	rm -rf out build

.PHONY: crun
crun: clean
	$(MAKE) run

.PHONY: usb
usb: out/boot.img
	sudo dd if=out/boot.img of=/dev/disk/by-id/usb-Verbatim_STORE_N_GO_077516801042-0\:0 status=progress conv=fdatasync && sync

# TODO: I guess with C I need to have each object file list any .h files the .c file includes as prereq
# And I want to set bootloader up to read the right number of sectors.  Oh, er, hmm... BIOS can only
#   read number in al sectors, so one byte, or 256 sectors, so 128 Kb?  I guess just always do that?  And
#   if kernel is every bigger than that, it can do it itself?  Oh, well, **at a time**.  So, duh, I can
#   still have the bootloader do it, and I probably still want that, but not a byte value, but a word value,
#   and bootloader loops as necessary.  Yeah, I think I prefer that.  Although, wait...  jeesh, I'm exhausted
#   and bouncing around on this a lot, but  we only have so much memory that's safe to read into at that point.
#   481 Kb including boot sector -- 961 sectors can be read into memory to fill that space.
#
#   So maybe I have a loop to read 240 sectors 4 times, and just do that starting now, rather than have build system
#     alter assembled bootloader to read exactly what's necessary.  Reading half a meg should be pretty much
#     instantaneous.  I *can* and *should* have build system complain if boot.img is any larger than
#     961*512 (bootloader plus 240*4, leaving one possible to read unread) = 492,032 bytes.
#   Ah, file:///home/darshan/asm/asm_024.3.html says I can do 128 sectors max at a time.

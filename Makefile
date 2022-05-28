.DEFAULT_GOAL := out/boot.img

c_objects := $(patsubst src/%.c, build/%.o, $(wildcard src/*.c))

GCC_OPTS := -Wall -Wextra -c -ffreestanding -fno-stack-protector -mgeneral-regs-only -mno-red-zone #-fno-zero-initialized-in-bss

LD_OPTS := -N --warn-common -T src/linker.ld # --print-map

include build/headers.mk

build out:
	mkdir -p $@

# TODO: Doesn't account for headers including other local headers (could use gcc --freestanding -M instead?)
#   For now let's just have a rule against doing that, and deal with it if I ever need to do it.
build/headers.mk: src/*.c src/*.h | build
	cd src && for cf in *.c; do echo -n "build/$$cf" | sed 's/\.c$$/\.o: /'; grep "#include \"" "$$cf" | awk '{print $$2}' | awk -F '"' '{print "src/"$$2}' | xargs; done >../build/headers.mk

build/bootloader.o: Makefile src/bootloader.asm | build
	nasm -f elf64 src/bootloader.asm -o build/bootloader.o

build/*.o: Makefile
build/%.o: src/%.c | build
	gcc $(GCC_OPTS) $< -o $@

out/boot.img: $(c_objects) src/linker.ld build/bootloader.o | out
	ld -o out/boot.img $(LD_OPTS) build/bootloader.o $(c_objects)

out/bochs.img: out/boot.img
	cp out/boot.img out/bochs.img
	truncate -s 499712 out/bochs.img

# -display gtk,zoom-to-fit=on
# -full-screen
# -cpu host 
.PHONY: run
run: out/boot.img
	qemu-system-x86_64 -rtc base=localtime -enable-kvm -drive format=raw,file=out/boot.img

.PHONY: run-bochs
run-bochs: out/bochs.img
	bochs -qf bochs.cfg

.PHONY: clean
clean:
	rm -rf out build

.PHONY: crun
crun: clean
	$(MAKE) run
#	make run

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

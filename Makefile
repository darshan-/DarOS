.DEFAULT_GOAL := out/boot.img

c_source_files := $(shell find src -name *.c)
c_object_files := $(patsubst src/%.c, build/%.o, $(c_source_files))

build/bootloader: Makefile src/bootloader.asm
	mkdir -p build && nasm src/bootloader.asm && mv src/bootloader build/

$(c_object_files): build/%.o : Makefile src/%.c
	gcc -c -ffreestanding -fno-stack-protector -mgeneral-regs-only $(patsubst build/%.o, src/%.c, $@) -o $@

.PHONY: run
run: out/boot.img
	qemu-system-x86_64 -drive format=raw,file=out/boot.img

.PHONY: clean
clean:
	rm -rf out build

out/boot.img: build/bootloader src/linker.ld $(c_object_files)
	mkdir -p out && ld -n -o build/kernel.bin -T src/linker.ld -Ttext $(shell ./calc-text-offset) build/*.o && cat build/bootloader build/kernel.bin >out/boot.img

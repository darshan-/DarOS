.DEFAULT_GOAL := out/boot.img

c_objects := $(patsubst src/%.c, build/%.o, $(wildcard src/*.c))

build out:
	mkdir -p $@

build/bootloader: Makefile src/bootloader.asm | build
	nasm src/bootloader.asm -o build/bootloader

build/*.o: Makefile
build/%.o: src/%.c | build
	gcc -c -ffreestanding -fno-stack-protector -mgeneral-regs-only -mno-red-zone $< -o $@

build/kernel.bin: $(c_objects) build/bootloader
	ld -n -o build/kernel.bin -T src/linker.ld -Ttext $(shell echo $$((`wc -c <build/bootloader`))) $(c_objects)

out/boot.img: build/bootloader src/linker.ld build/kernel.bin | out
	cat build/bootloader build/kernel.bin >out/boot.img

.PHONY: run
run: out/boot.img
	qemu-system-x86_64 -drive format=raw,file=out/boot.img

.PHONY: clean
clean:
	rm -rf out build


# TODO: I guess with C I need to have each object file list any .h files the .c file includes as prereq

.DEFAULT_GOAL := out/boot.img

c_source_files := $(shell find src -name *.c)
c_object_files := $(patsubst src/%.c, build/%.o, $(c_source_files))

build:
	mkdir build

build/bootloader: build src/bootloader.asm
	nasm src/bootloader.asm && mv src/bootloader build/

$(c_object_files): build/%.o : src/%.c
	gcc -c -ffreestanding $(patsubst build/%.o, src/%.c, $@) -o $@

.PHONY: run
run: out/boot.img
	qemu-system-x86_64 -drive format=raw,file=out/boot.img

.PHONY: clean
clean:
	rm -rf out build

out/boot.img: build/bootloader $(c_object_files)
	mkdir -p out && ./link && cat build/bootloader build/kernel.bin >out/boot.img

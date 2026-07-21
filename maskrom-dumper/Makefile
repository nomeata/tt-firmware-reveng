# Self-contained: our own api.h/sdk.c/*.ld (no tt-homebrew, no ../../lib). One main.c, both pens.
all: build/2N.bin build/ZC3201.bin

build/2N.bin: main.c api.h sdk.c startup.s 2N.ld gme_media.h
	mkdir -p build
	arm-none-eabi-gcc -Dbuild_for_2N -I . -T 2N.ld -nostdlib -fno-builtin -fshort-wchar startup.s main.c -o build/2N.out
	arm-none-eabi-objcopy -O binary build/2N.out build/2N.bin

build/ZC3201.bin: main.c api.h sdk.c startup.s ZC3201.ld gme_media.h
	mkdir -p build
	arm-none-eabi-gcc -Dbuild_for_ZC3201 -I . -T ZC3201.ld -nostdlib -fno-builtin -fshort-wchar startup.s main.c -o build/ZC3201.out
	arm-none-eabi-objcopy -O binary build/ZC3201.out build/ZC3201.bin

# Makefile for CalculusOS

# Compiler and tools
AS = nasm
CC = gcc
LD = ld

# Flags
ASFLAGS = -f elf32
CFLAGS = -m32 -c -ffreestanding -O2 -Wall -Wextra -nostdlib -fno-builtin -nostartfiles -nodefaultlibs
LDFLAGS = -m elf_i386 -T linker.ld -nostdlib

# Source files
ASM_SOURCES = boot.asm
C_SOURCES = kernel/kernel.c kernel/window.c kernel/font.c

# Object files
ASM_OBJECTS = $(ASM_SOURCES:.asm=.o)
C_OBJECTS = $(C_SOURCES:.c=.o)
OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# Output
KERNEL = kernel.bin
ISO = CalculusOS.iso

# Targets
.PHONY: all clean run iso

all: $(KERNEL)

$(KERNEL): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $(OBJECTS)

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

iso: $(KERNEL)
	mkdir -p isodir/boot/grub
	cp $(KERNEL) isodir/boot/
	cp grub/grub.cfg isodir/boot/grub/
	grub-mkrescue -o $(ISO) isodir

run: iso
	qemu-system-i386 -cdrom $(ISO)

clean:
	rm -f $(OBJECTS) $(KERNEL) $(ISO)
	rm -rf isodir

# Dependencies
boot.o: boot.asm
kernel/kernel.o: kernel/kernel.c
kernel/window.o: kernel/window.c kernel/font.h
kernel/font.o: kernel/font.c kernel/font.h
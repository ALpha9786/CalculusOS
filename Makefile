# Makefile for Calculus OS

# Compiler and linker settings
CC = gcc
AS = nasm
LD = ld

# Flags
CFLAGS = -m32 -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
         -nostartfiles -nodefaultlibs -Wall -Wextra -Werror -c
LDFLAGS = -m elf_i386 -T linker.ld
ASFLAGS = -f elf32

# Directories
KERNEL_DIR = kernel
BUILD_DIR = build

# Source files
BOOT_ASM = boot.asm
KERNEL_C = kernel.c
WINDOW_C = $(KERNEL_DIR)/window.c

# Object files
BOOT_OBJ = $(BUILD_DIR)/boot.o
KERNEL_OBJ = $(BUILD_DIR)/kernel.o
WINDOW_OBJ = $(BUILD_DIR)/window.o

# Output
OS_BIN = $(BUILD_DIR)/calculus.bin
OS_ISO = $(BUILD_DIR)/calculus.iso

.PHONY: all clean run iso

all: $(OS_BIN)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Assemble bootloader
$(BOOT_OBJ): $(BOOT_ASM) | $(BUILD_DIR)
	$(AS) $(ASFLAGS) $< -o $@

# Compile kernel
$(KERNEL_OBJ): $(KERNEL_C) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

# Compile window manager
$(WINDOW_OBJ): $(WINDOW_C) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

# Link everything
$(OS_BIN): $(BOOT_OBJ) $(KERNEL_OBJ) $(WINDOW_OBJ)
	$(LD) $(LDFLAGS) -o $@ $^

# Create bootable ISO (requires grub-mkrescue)
iso: $(OS_BIN)
	mkdir -p $(BUILD_DIR)/iso/boot/grub
	cp $(OS_BIN) $(BUILD_DIR)/iso/boot/calculus.bin
	echo 'set timeout=0' > $(BUILD_DIR)/iso/boot/grub/grub.cfg
	echo 'set default=0' >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	echo '' >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	echo 'menuentry "Calculus OS" {' >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	echo '    multiboot /boot/calculus.bin' >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	echo '    boot' >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	echo '}' >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	grub-mkrescue -o $(OS_ISO) $(BUILD_DIR)/iso

# Run in QEMU
run: $(OS_BIN)
	qemu-system-i386 -kernel $(OS_BIN) -m 512M -vga std

# Run ISO in QEMU
run-iso: iso
	qemu-system-i386 -cdrom $(OS_ISO) -m 512M -vga std

# Clean build files
clean:
	rm -rf $(BUILD_DIR)

# Help
help:
	@echo "Calculus OS Build System"
	@echo "========================"
	@echo "make          - Build the OS kernel"
	@echo "make iso      - Create bootable ISO"
	@echo "make run      - Run in QEMU (kernel only)"
	@echo "make run-iso  - Run ISO in QEMU"
	@echo "make clean    - Remove build files"
	@echo "make help     - Show this help message"

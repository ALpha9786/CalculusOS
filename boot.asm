; boot.asm - Bootloader for CalculusOS with Multiboot support
[BITS 32]
[EXTERN kernel_main]
[EXTERN call_constructors]
[GLOBAL start]

; Multiboot header constants
MULTIBOOT_MAGIC equ 0x1BADB002
MULTIBOOT_PAGE_ALIGN equ 1<<0
MULTIBOOT_MEMORY_INFO equ 1<<1
MULTIBOOT_VIDEO_MODE equ 1<<2
MULTIBOOT_FLAGS equ MULTIBOOT_PAGE_ALIGN | MULTIBOOT_MEMORY_INFO | MULTIBOOT_VIDEO_MODE
MULTIBOOT_CHECKSUM equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

section .multiboot
    align 4
multiboot_header:
    dd MULTIBOOT_MAGIC
    dd MULTIBOOT_FLAGS
    dd MULTIBOOT_CHECKSUM
    
    ; address fields (unused since we use ELF)
    dd 0
    dd 0
    dd 0
    dd 0
    dd 0
    
    ; video mode fields
    dd 0    ; mode_type: 0 = linear graphics
    dd 320  ; width
    dd 200  ; height
    dd 8    ; depth (bits per pixel)

section .bss
    align 16
stack_bottom:
    resb 16384 ; 16 KB stack
stack_top:

section .text
start:
    ; Disable interrupts
    cli
    
    ; Set up stack
    mov esp, stack_top
    
    ; Reset EFLAGS
    push 0
    popf
    
    ; Push multiboot info (they're already in eax and ebx from GRUB)
    ; eax contains the magic number
    ; ebx contains the address of multiboot info structure
    push ebx    ; multiboot info pointer
    push eax    ; multiboot magic number
    
    ; Call global constructors
    call call_constructors
    
    ; Call kernel main with multiboot parameters
    call kernel_main
    
    ; If kernel returns, halt
    cli
.hang:
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
/* linker.ld - Linker script for Calculus OS */

ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 1MB physical address */
    . = 0x00100000;
    
    /* Multiboot header must be in first 8KB */
    .multiboot : ALIGN(4K)
    {
        *(.multiboot)
    }
    
    /* Read-only data */
    .rodata : ALIGN(4K)
    {
        *(.rodata*)
    }
    
    /* Code section */
    .text : ALIGN(4K)
    {
        *(.text)
    }
    
    /* Initialized data */
    .data : ALIGN(4K)
    {
        *(.data)
    }
    
    /* Uninitialized data (BSS) */
    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }
    
    /* End of kernel marker */
    _kernel_end = .;
}

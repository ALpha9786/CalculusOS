// kernel.c - Main kernel initialization
#include <stdint.h>
#include <stddef.h>

#define VGA_WIDTH 320
#define VGA_HEIGHT 200
#define VGA_MEMORY 0xA0000

// Multiboot structures
#define MULTIBOOT_MAGIC 0x2BADB002

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
} multiboot_info_t;

typedef struct {
    uint8_t r, g, b;
} Color;

extern void window_manager_init(void);
extern void window_manager_run(void);

static multiboot_info_t* mboot_info = NULL;
static uint32_t mboot_magic = 0;

// Simple terminal output for debugging
static char* term_buffer = (char*)0xB8000;
static int term_col = 0;
static int term_row = 0;

void term_putc(char c) {
    if (c == '\n') {
        term_col = 0;
        term_row++;
        return;
    }
    int pos = (term_row * 80 + term_col) * 2;
    term_buffer[pos] = c;
    term_buffer[pos + 1] = 0x0F;
    term_col++;
    if (term_col >= 80) {
        term_col = 0;
        term_row++;
    }
}

void term_puts(const char* str) {
    while (*str) {
        term_putc(*str++);
    }
}

// Helper to print hex number
void term_puthex(uint32_t n) {
    const char* hex = "0123456789ABCDEF";
    term_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        term_putc(hex[(n >> i) & 0xF]);
    }
}

// Port I/O functions
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Set VGA mode 13h (320x200 256 colors) using VGA registers
void set_vga_mode(void) {
    // Write to VGA registers to set mode 13h
    // This works in protected mode unlike BIOS interrupts
    
    // Miscellaneous Output Register
    outb(0x3C2, 0x63);
    
    // Sequencer Registers
    outb(0x3C4, 0x00); outb(0x3C5, 0x03); // Reset
    outb(0x3C4, 0x01); outb(0x3C5, 0x01); // Clocking Mode
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F); // Map Mask
    outb(0x3C4, 0x03); outb(0x3C5, 0x00); // Character Map
    outb(0x3C4, 0x04); outb(0x3C5, 0x0E); // Memory Mode
    
    // CRTC Registers
    outb(0x3D4, 0x11); outb(0x3D5, 0x00); // Unlock CRTC
    
    const uint8_t crtc_regs[] = {
        0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
        0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x9C, 0x8E, 0x8F, 0x28, 0x40, 0x96, 0xB9, 0xA3,
        0xFF
    };
    
    for (int i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, crtc_regs[i]);
    }
    
    // Graphics Controller Registers
    outb(0x3CE, 0x00); outb(0x3CF, 0x00);
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);
    outb(0x3CE, 0x02); outb(0x3CF, 0x00);
    outb(0x3CE, 0x03); outb(0x3CF, 0x00);
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x40);
    outb(0x3CE, 0x06); outb(0x3CF, 0x05);
    outb(0x3CE, 0x07); outb(0x3CF, 0x0F);
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);
    
    // Attribute Controller Registers
    inb(0x3DA); // Reset flip-flop
    
    for (int i = 0; i < 16; i++) {
        outb(0x3C0, i);
        outb(0x3C0, i);
    }
    
    outb(0x3C0, 0x10); outb(0x3C0, 0x41);
    outb(0x3C0, 0x11); outb(0x3C0, 0x00);
    outb(0x3C0, 0x12); outb(0x3C0, 0x0F);
    outb(0x3C0, 0x13); outb(0x3C0, 0x00);
    outb(0x3C0, 0x14); outb(0x3C0, 0x00);
    
    outb(0x3C0, 0x20); // Enable display
}

// Set palette color
void set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    outb(0x3C8, index);
    outb(0x3C9, r >> 2);
    outb(0x3C9, g >> 2);
    outb(0x3C9, b >> 2);
}

// Initialize palette with basic colors
void init_palette(void) {
    // Color 0: Black
    set_palette(0, 0, 0, 0);
    // Color 1: White
    set_palette(1, 255, 255, 255);
    // Color 2: Blue (Windows style)
    set_palette(2, 0, 120, 215);
    // Color 3: Light gray
    set_palette(3, 200, 200, 200);
    // Color 4: Dark gray
    set_palette(4, 100, 100, 100);
    // Color 5: Red
    set_palette(5, 255, 0, 0);
    // Color 6: Green
    set_palette(6, 0, 255, 0);
    // Color 7: Light blue
    set_palette(7, 173, 216, 230);
    // Color 8: Dock gray
    set_palette(8, 240, 240, 245);
    // Color 9: Shadow
    set_palette(9, 60, 60, 60);
    
    // Fill rest with gradients
    for (int i = 10; i < 256; i++) {
        set_palette(i, i, i, i);
    }
}

// Clear screen with color
void clear_screen(uint8_t color) {
    uint8_t* vga = (uint8_t*)VGA_MEMORY;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = color;
    }
}

// Constructor support
typedef void (*constructor)(void);
extern constructor start_ctors;
extern constructor end_ctors;

void call_constructors(void) {
    for (constructor* i = &start_ctors; i != &end_ctors; i++) {
        (*i)();
    }
}

// Main kernel entry
void kernel_main(uint32_t magic, multiboot_info_t* mboot) {
    mboot_magic = magic;
    mboot_info = mboot;
    
    // Print startup message on text mode
    term_puts("CalculusOS Starting...\n");
    
    // Verify multiboot
    if (magic == MULTIBOOT_MAGIC) {
        term_puts("Multiboot verified!\n");
        term_puts("Magic: ");
        term_puthex(mboot_magic);
        term_puts("\n");
        
        // Display memory info if available
        if (mboot_info && (mboot_info->flags & 0x01)) {
            term_puts("Lower memory: ");
            term_puthex(mboot_info->mem_lower);
            term_puts(" KB\n");
            term_puts("Upper memory: ");
            term_puthex(mboot_info->mem_upper);
            term_puts(" KB\n");
        }
        
        // Display boot loader name if available
        if (mboot_info && (mboot_info->flags & 0x200) && mboot_info->boot_loader_name) {
            term_puts("Bootloader: ");
            char* name = (char*)mboot_info->boot_loader_name;
            for (int i = 0; i < 50 && name[i]; i++) {
                term_putc(name[i]);
            }
            term_puts("\n");
        }
    } else {
        term_puts("Warning: Multiboot magic invalid\n");
        term_puts("Expected: ");
        term_puthex(MULTIBOOT_MAGIC);
        term_puts("\n");
        term_puts("Got: ");
        term_puthex(magic);
        term_puts("\n");
    }
    
    term_puts("Switching to VGA Mode 13h...\n");
    
    // Small delay to see the message
    for (volatile int i = 0; i < 10000000; i++);
    
    // Switch to VGA mode 13h
    set_vga_mode();
    
    // Initialize palette
    init_palette();
    
    // Clear screen to light blue
    clear_screen(7);
    
    // Initialize and run window manager
    window_manager_init();
    window_manager_run();
    
    // Should never reach here
    while(1) {
        asm("hlt");
    }
}
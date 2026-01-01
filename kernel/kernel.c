// window.c - Window Manager and UI
#include <stdint.h>
#include <stddef.h>
#include "font.h"

#define VGA_WIDTH 320
#define VGA_HEIGHT 200
#define VGA_MEMORY 0xA0000
#define DOCK_HEIGHT 40

// Port I/O functions (must be declared early)
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Keyboard scan codes
#define KEY_UP 0x48
#define KEY_DOWN 0x50
#define KEY_LEFT 0x4B
#define KEY_RIGHT 0x4D
#define KEY_LCTRL 0x1D
#define KEY_LALT 0x38
#define KEY_C 0x2E
#define KEY_ENTER 0x1C
#define KEY_BACKSPACE 0x0E
#define KEY_SPACE 0x39

// Forward declarations
void handle_click(void);
void process_command(void);
void shutdown_system(void);
void reboot_system(void);

// Terminal state
#define TERM_BUFFER_SIZE 256
#define TERM_OUTPUT_LINES 10
static char terminal_buffer[TERM_BUFFER_SIZE];
static int terminal_cursor = 0;
static char terminal_output[TERM_OUTPUT_LINES][40];
static int output_line_count = 0;
static uint32_t char_count = 0;
static uint32_t start_time = 0;

// Double buffer for flicker-free rendering
static uint8_t back_buffer[VGA_WIDTH * VGA_HEIGHT];
static uint8_t* vga = (uint8_t*)VGA_MEMORY;
static int mouse_x = VGA_WIDTH / 2;
static int mouse_y = VGA_HEIGHT / 2;
static uint8_t ctrl_pressed = 0;
static uint8_t alt_pressed = 0;
static uint8_t terminal_open = 0;
static uint8_t start_menu_open = 0;
static int last_clicked_icon = -1;
static uint8_t click_frame_count = 0;

// Keyboard scan code to ASCII mapping
char scancode_to_ascii(uint8_t scan) {
    static const char scancode_map[] = {
        0,   0,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,   0,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0,   0,  'a', 's',
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'',0,   0,  '\\','z', 'x', 'c', 'v',
        'b', 'n', 'm', ',', '.', '/', 0,   0,   0,  ' '
    };
    
    if (scan < sizeof(scancode_map)) {
        return scancode_map[scan];
    }
    return 0;
}

// Simple string functions
int str_len(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

int str_cmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void str_cpy(char* dest, const char* src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest = 0;
}

int str_starts_with(const char* str, const char* prefix) {
    while (*prefix) {
        if (*str++ != *prefix++) return 0;
    }
    return 1;
}

// Add line to terminal output
void add_output_line(const char* line) {
    if (output_line_count < TERM_OUTPUT_LINES) {
        str_cpy(terminal_output[output_line_count], line);
        output_line_count++;
    } else {
        // Scroll up
        for (int i = 0; i < TERM_OUTPUT_LINES - 1; i++) {
            str_cpy(terminal_output[i], terminal_output[i + 1]);
        }
        str_cpy(terminal_output[TERM_OUTPUT_LINES - 1], line);
    }
}

// Process terminal command
void process_command(void) {
    // Add command to output
    char cmd_line[42];
    str_cpy(cmd_line, "> ");
    for (int i = 0; i < terminal_cursor && i < 38; i++) {
        cmd_line[i + 2] = terminal_buffer[i];
    }
    cmd_line[terminal_cursor + 2] = 0;
    add_output_line(cmd_line);
    
    // Process commands
    if (str_cmp(terminal_buffer, "help") == 0) {
        add_output_line("commands: ls dir cd mkdir");
        add_output_line("clear wpm help shutdown reboot");
    } else if (str_cmp(terminal_buffer, "ls") == 0 || str_cmp(terminal_buffer, "dir") == 0) {
        add_output_line("documents/  pictures/");
        add_output_line("downloads/  system/");
    } else if (str_starts_with(terminal_buffer, "ls ")) {
        add_output_line("file1.txt  file2.txt");
        add_output_line("readme.md  config.sys");
    } else if (str_starts_with(terminal_buffer, "cd ")) {
        add_output_line("changed directory");
    } else if (str_starts_with(terminal_buffer, "mkdir ")) {
        add_output_line("directory created");
    } else if (str_cmp(terminal_buffer, "clear") == 0) {
        output_line_count = 0;
        char_count = 0;
        start_time = 0;
    } else if (str_cmp(terminal_buffer, "shutdown") == 0) {
        add_output_line("shutting down...");
        shutdown_system();
    } else if (str_cmp(terminal_buffer, "reboot") == 0) {
        add_output_line("rebooting...");
        reboot_system();
    } else if (str_cmp(terminal_buffer, "wpm") == 0) {
        // Calculate WPM
        char wpm_str[40];
        str_cpy(wpm_str, "chars typed: ");
        int chars = char_count;
        int pos = str_len(wpm_str);
        if (chars == 0) {
            wpm_str[pos++] = '0';
        } else {
            char temp[10];
            int i = 0;
            while (chars > 0) {
                temp[i++] = '0' + (chars % 10);
                chars /= 10;
            }
            while (i > 0) {
                wpm_str[pos++] = temp[--i];
            }
        }
        wpm_str[pos] = 0;
        add_output_line(wpm_str);
    } else if (terminal_cursor > 0) {
        add_output_line("command not found");
    }
}

// Shutdown system
void shutdown_system(void) {
    asm volatile("cli; hlt");
    while(1);
}

// Reboot system
void reboot_system(void) {
    uint8_t good = 0x02;
    while (good & 0x02) {
        good = inb(0x64);
    }
    outb(0x64, 0xFE);
    asm volatile("hlt");
}

// Check which icon mouse is hovering over
int get_hovered_icon(void) {
    int dock_width = 200;
    int dock_x = (VGA_WIDTH - dock_width) / 2;
    int dock_y = VGA_HEIGHT - DOCK_HEIGHT + 5;
    int icon_y = dock_y + 5;
    int icon_size = 20;
    
    int start_x = dock_x + 10;
    int term_x = start_x + 35;
    int files_x = term_x + 35;
    int settings_x = files_x + 35;
    
    if (mouse_x >= start_x && mouse_x <= start_x + icon_size &&
        mouse_y >= icon_y && mouse_y <= icon_y + icon_size) {
        return 0;
    }
    
    if (mouse_x >= term_x && mouse_x <= term_x + icon_size &&
        mouse_y >= icon_y && mouse_y <= icon_y + icon_size) {
        return 1;
    }
    
    if (mouse_x >= files_x && mouse_x <= files_x + icon_size &&
        mouse_y >= icon_y && mouse_y <= icon_y + icon_size) {
        return 2;
    }
    
    if (mouse_x >= settings_x && mouse_x <= settings_x + icon_size &&
        mouse_y >= icon_y && mouse_y <= icon_y + icon_size) {
        return 3;
    }
    
    return -1;
}

// Draw pixel to back buffer
void put_pixel(int x, int y, uint8_t color) {
    if (x >= 0 && x < VGA_WIDTH && y >= 0 && y < VGA_HEIGHT) {
        back_buffer[y * VGA_WIDTH + x] = color;
    }
}

// Copy back buffer to screen
void flip_buffer(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga[i] = back_buffer[i];
    }
}

// Draw filled rectangle
void draw_rect(int x, int y, int w, int h, uint8_t color) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            put_pixel(x + i, y + j, color);
        }
    }
}

// Draw rectangle outline
void draw_rect_outline(int x, int y, int w, int h, uint8_t color) {
    for (int i = 0; i < w; i++) {
        put_pixel(x + i, y, color);
        put_pixel(x + i, y + h - 1, color);
    }
    for (int j = 0; j < h; j++) {
        put_pixel(x, y + j, color);
        put_pixel(x + w - 1, y + j, color);
    }
}

// Draw text character
void draw_char(int x, int y, char c, uint8_t color) {
    const uint8_t* font_data = get_font_char(c);
    
    for (int j = 0; j < 8; j++) {
        uint8_t line = font_data[j];
        uint8_t reversed = 0;
        for (int b = 0; b < 8; b++) {
            if (line & (1 << b)) {
                reversed |= (1 << (7 - b));
            }
        }
        
        for (int i = 0; i < 8; i++) {
            if (reversed & (1 << (7 - i))) {
                put_pixel(x + i, y + j, color);
            }
        }
    }
}

void draw_string(int x, int y, const char* str, uint8_t color) {
    int cx = x;
    while (*str) {
        draw_char(cx, y, *str, color);
        cx += 8;
        str++;
    }
}

// Draw wallpaper
void draw_wallpaper(void) {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        uint8_t color = 7;
        for (int x = 0; x < VGA_WIDTH; x++) {
            put_pixel(x, y, color);
        }
    }
}

// Draw dock
void draw_dock(void) {
    int dock_width = 200;
    int dock_x = (VGA_WIDTH - dock_width) / 2;
    int dock_y = VGA_HEIGHT - DOCK_HEIGHT + 5;
    int dock_h = 30;
    
    draw_rect(dock_x + 2, dock_y + 2, dock_width, dock_h, 9);
    draw_rect(dock_x, dock_y, dock_width, dock_h, 1);
    draw_rect_outline(dock_x, dock_y, dock_width, dock_h, 3);
    
    int icon_size = 20;
    int icon_y = dock_y + 5;
    int start_x = dock_x + 10;
    int term_x = start_x + 35;
    int files_x = term_x + 35;
    int settings_x = files_x + 35;
    
    int hovered = get_hovered_icon();
    
    // Start button
    draw_rect(start_x, icon_y, icon_size, icon_size, 2);
    uint8_t border_color = 1;
    if (click_frame_count > 0 && last_clicked_icon == 0) {
        border_color = 5;
    } else if (hovered == 0) {
        border_color = 2;
    }
    draw_rect_outline(start_x, icon_y, icon_size, icon_size, border_color);
    draw_rect(start_x + 3, icon_y + 3, 7, 7, 1);
    draw_rect(start_x + 11, icon_y + 3, 7, 7, 1);
    draw_rect(start_x + 3, icon_y + 11, 7, 7, 1);
    draw_rect(start_x + 11, icon_y + 11, 7, 7, 1);
    
    // Terminal icon
    draw_rect(term_x, icon_y, icon_size, icon_size, 0);
    border_color = 6;
    if (click_frame_count > 0 && last_clicked_icon == 1) {
        border_color = 5;
    } else if (hovered == 1) {
        border_color = 2;
    }
    draw_rect_outline(term_x, icon_y, icon_size, icon_size, border_color);
    draw_rect(term_x + 2, icon_y + 2, icon_size - 4, 3, 6);
    draw_char(term_x + 3, icon_y + 7, '>', 6);
    draw_char(term_x + 11, icon_y + 7, '_', 6);
    draw_rect(term_x + 3, icon_y + 13, 8, 1, 6);
    draw_rect(term_x + 3, icon_y + 16, 12, 1, 6);
    
    // Files icon
    draw_rect(files_x, icon_y, icon_size, icon_size, 11);
    border_color = 4;
    if (click_frame_count > 0 && last_clicked_icon == 2) {
        border_color = 5;
    } else if (hovered == 2) {
        border_color = 2;
    }
    draw_rect_outline(files_x, icon_y, icon_size, icon_size, border_color);
    draw_rect(files_x + 2, icon_y, 8, 3, 11);
    draw_rect_outline(files_x + 2, icon_y, 8, 3, border_color);
    
    // Settings icon
    draw_rect(settings_x, icon_y, icon_size, icon_size, 4);
    border_color = 9;
    if (click_frame_count > 0 && last_clicked_icon == 3) {
        border_color = 5;
    } else if (hovered == 3) {
        border_color = 2;
    }
    draw_rect_outline(settings_x, icon_y, icon_size, icon_size, border_color);
    draw_rect(settings_x + 8, icon_y + 8, 4, 4, 1);
}

// Draw start menu
void draw_start_menu(void) {
    if (!start_menu_open) return;
    
    int menu_w = 180;
    int menu_h = 150;
    int menu_x = (VGA_WIDTH - 200) / 2 + 10;
    int menu_y = VGA_HEIGHT - DOCK_HEIGHT - menu_h - 10;
    
    draw_rect(menu_x + 3, menu_y + 3, menu_w, menu_h, 9);
    draw_rect(menu_x, menu_y, menu_w, menu_h, 1);
    draw_rect_outline(menu_x, menu_y, menu_w, menu_h, 3);
    
    draw_rect(menu_x, menu_y, menu_w, 25, 2);
    draw_string(menu_x + 10, menu_y + 8, "calculusos", 1);
    
    draw_rect(menu_x + 5, menu_y + 25, menu_w - 10, 1, 4);
    
    int item_h = 25;
    int item_y = menu_y + 30;
    
    draw_rect(menu_x + 5, item_y, menu_w - 10, item_h, 7);
    draw_rect_outline(menu_x + 5, item_y, menu_w - 10, item_h, 3);
    draw_char(menu_x + 15, item_y + 8, '>', 6);
    draw_string(menu_x + 30, item_y + 8, "terminal", 0);
    
    item_y += item_h + 5;
    draw_rect(menu_x + 5, item_y, menu_w - 10, item_h, 7);
    draw_rect_outline(menu_x + 5, item_y, menu_w - 10, item_h, 3);
    draw_string(menu_x + 15, item_y + 8, "files", 0);
    
    item_y += item_h + 5;
    draw_rect(menu_x + 5, item_y, menu_w - 10, item_h, 7);
    draw_rect_outline(menu_x + 5, item_y, menu_w - 10, item_h, 3);
    draw_string(menu_x + 15, item_y + 8, "settings", 0);
    
    item_y += item_h + 5;
    draw_rect(menu_x + 5, item_y - 2, menu_w - 10, 1, 4);
    
    draw_rect(menu_x + 5, item_y, (menu_w - 15) / 2, item_h, 5);
    draw_rect_outline(menu_x + 5, item_y, (menu_w - 15) / 2, item_h, 0);
    draw_string(menu_x + 15, item_y + 8, "shutdown", 1);
    
    draw_rect(menu_x + 10 + (menu_w - 15) / 2, item_y, (menu_w - 15) / 2, item_h, 10);
    draw_rect_outline(menu_x + 10 + (menu_w - 15) / 2, item_y, (menu_w - 15) / 2, item_h, 0);
    draw_string(menu_x + 20 + (menu_w - 15) / 2, item_y + 8, "reboot", 1);
}

// Draw terminal window
void draw_terminal(void) {
    if (!terminal_open) return;
    
    int win_w = 260;
    int win_h = 160;
    int win_x = 30;
    int win_y = 15;
    
    draw_rect(win_x + 3, win_y + 3, win_w, win_h, 9);
    draw_rect(win_x, win_y, win_w, win_h, 0);
    draw_rect(win_x, win_y, win_w, 20, 2);
    draw_rect(win_x, win_y + 19, win_w, 1, 3);
    draw_string(win_x + 10, win_y + 6, "terminal", 1);
    
    draw_rect(win_x + win_w - 54, win_y + 3, 14, 14, 11);
    draw_rect_outline(win_x + win_w - 54, win_y + 3, 14, 14, 0);
    draw_rect(win_x + win_w - 51, win_y + 12, 8, 2, 0);
    
    draw_rect(win_x + win_w - 36, win_y + 3, 14, 14, 6);
    draw_rect_outline(win_x + win_w - 36, win_y + 3, 14, 14, 0);
    put_pixel(win_x + win_w - 29, win_y + 10, 0);
    put_pixel(win_x + win_w - 30, win_y + 11, 0);
    put_pixel(win_x + win_w - 28, win_y + 11, 0);
    put_pixel(win_x + win_w - 31, win_y + 12, 0);
    put_pixel(win_x + win_w - 27, win_y + 12, 0);
    
    draw_rect(win_x + win_w - 18, win_y + 3, 14, 14, 5);
    draw_rect_outline(win_x + win_w - 18, win_y + 3, 14, 14, 0);
    for (int i = 0; i < 8; i++) {
        put_pixel(win_x + win_w - 14 + i, win_y + 7 + i, 1);
        put_pixel(win_x + win_w - 14 + i, win_y + 13 - i, 1);
    }
    
    draw_rect_outline(win_x, win_y, win_w, win_h, 3);
    
    int content_y = win_y + 25;
    
    int start_line = output_line_count > 7 ? output_line_count - 7 : 0;
    for (int i = start_line; i < output_line_count; i++) {
        draw_string(win_x + 8, content_y + (i - start_line) * 10, terminal_output[i], 3);
    }
    
    int cmd_y = content_y + ((output_line_count < 7 ? output_line_count : 7) * 10);
    draw_string(win_x + 8, cmd_y, "c:/home>", 6);
    
    int buffer_x = win_x + 72;
    for (int i = 0; i < terminal_cursor && i < 20; i++) {
        draw_char(buffer_x + i * 8, cmd_y, terminal_buffer[i], 6);
    }
    
    static uint8_t cursor_blink = 0;
    cursor_blink = (cursor_blink + 1) % 60;
    if (cursor_blink < 30 && terminal_open) {
        draw_rect(buffer_x + terminal_cursor * 8, cmd_y, 6, 8, 6);
    }
}

// Draw small pixel cursor
void draw_cursor(int x, int y) {
    const char cursor[10][8] = {
        "X      ",
        "XX     ",
        "X.X    ",
        "X..X   ",
        "X...X  ",
        "X....X ",
        "X.XXX  ",
        "X.X    ",
        "XX     ",
        "X      "
    };
    
    uint8_t border_color = 0;
    
    if (click_frame_count > 0) {
        border_color = 5;
    } else if (alt_pressed) {
        border_color = 1;
    }
    
    if (border_color != 0) {
        for (int i = -1; i < 8; i++) {
            put_pixel(x + i, y - 1, border_color);
            put_pixel(x + i, y + 10, border_color);
        }
        for (int j = -1; j < 11; j++) {
            put_pixel(x - 1, y + j, border_color);
            put_pixel(x + 7, y + j, border_color);
        }
    }
    
    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < 7; i++) {
            if (cursor[j][i] == 'X' || cursor[j][i] == '.') {
                put_pixel(x + i + 1, y + j + 1, 9);
            }
        }
    }
    
    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < 7; i++) {
            if (cursor[j][i] == 'X') {
                put_pixel(x + i, y + j, 1);
            } else if (cursor[j][i] == '.') {
                put_pixel(x + i, y + j, 0);
            }
        }
    }
}

// Read keyboard scan code
uint8_t read_scan_code(void) {
    uint8_t status = inb(0x64);
    if (status & 0x01) {
        return inb(0x60);
    }
    return 0;
}

// Handle keyboard input
void handle_keyboard(void) {
    uint8_t scan = read_scan_code();
    
    if (scan == 0) return;
    
    if (scan < 0x80) {
        if (scan == KEY_LCTRL) {
            ctrl_pressed = 1;
        } else if (scan == KEY_LALT) {
            alt_pressed = 1;
        } else if (terminal_open) {
            if (scan == KEY_BACKSPACE) {
                if (terminal_cursor > 0) {
                    terminal_cursor--;
                    terminal_buffer[terminal_cursor] = 0;
                }
            } else if (scan == KEY_ENTER) {
                process_command();
                terminal_cursor = 0;
                for (int i = 0; i < TERM_BUFFER_SIZE; i++) {
                    terminal_buffer[i] = 0;
                }
            } else if (!ctrl_pressed && !alt_pressed) {
                char c = scancode_to_ascii(scan);
                if (c != 0 && terminal_cursor < TERM_BUFFER_SIZE - 1 && terminal_cursor < 20) {
                    terminal_buffer[terminal_cursor] = c;
                    terminal_cursor++;
                    char_count++;
                }
            }
        } else if (scan == KEY_ENTER) {
            handle_click();
        } else if (scan == KEY_C && alt_pressed) {
            handle_click();
        } else if (scan == KEY_UP) {
            if (ctrl_pressed) {
                if (mouse_y > 0) mouse_y--;
            } else {
                if (mouse_y > 4) mouse_y -= 2;
            }
        } else if (scan == KEY_DOWN) {
            if (ctrl_pressed) {
                if (mouse_y < VGA_HEIGHT - 10) mouse_y++;
            } else {
                if (mouse_y < VGA_HEIGHT - 12) mouse_y += 2;
            }
        } else if (scan == KEY_LEFT) {
            if (ctrl_pressed) {
                if (mouse_x > 0) mouse_x--;
            } else {
                if (mouse_x > 2) mouse_x -= 2;
            }
        } else if (scan == KEY_RIGHT) {
            if (ctrl_pressed) {
                if (mouse_x < VGA_WIDTH - 7) mouse_x++;
            } else {
                if (mouse_x < VGA_WIDTH - 9) mouse_x += 2;
            }
        }
    } else {
        if ((scan & 0x7F) == KEY_LCTRL) {
            ctrl_pressed = 0;
        } else if ((scan & 0x7F) == KEY_LALT) {
            alt_pressed = 0;
        }
    }
}

// Handle click
void handle_click(void) {
    int dock_width = 200;
    int dock_x = (VGA_WIDTH - dock_width) / 2;
    int dock_y = VGA_HEIGHT - DOCK_HEIGHT + 5;
    int icon_y = dock_y + 5;
    int icon_size = 20;
    
    int start_x = dock_x + 10;
    int start_y = icon_y;
    
    int term_x = start_x + 35;
    int term_y = icon_y;
    
    int files_x = term_x + 35;
    int files_y = icon_y;
    
    int settings_x = files_x + 35;
    int settings_y = icon_y;
    
    if (mouse_x >= start_x && mouse_x <= start_x + icon_size &&
        mouse_y >= start_y && mouse_y <= start_y + icon_size) {
        last_clicked_icon = 0;
        click_frame_count = 15;
        start_menu_open = !start_menu_open;
        return;
    }
    
    if (mouse_x >= term_x && mouse_x <= term_x + icon_size &&
        mouse_y >= term_y && mouse_y <= term_y + icon_size) {
        last_clicked_icon = 1;
        click_frame_count = 15;
        terminal_open = !terminal_open;
        start_menu_open = 0;
        if (start_time == 0) start_time = 1;
        return;
    }
    
    if (mouse_x >= files_x && mouse_x <= files_x + icon_size &&
        mouse_y >= files_y && mouse_y <= files_y + icon_size) {
        last_clicked_icon = 2;
        click_frame_count = 15;
        start_menu_open = 0;
        return;
    }
    
    if (mouse_x >= settings_x && mouse_x <= settings_x + icon_size &&
        mouse_y >= settings_y && mouse_y <= settings_y + icon_size) {
        last_clicked_icon = 3;
        click_frame_count = 15;
        start_menu_open = 0;
        return;
    }
    
    if (terminal_open) {
        int win_x = 30;
        int win_y = 15;
        int win_w = 260;
        
        int close_x = win_x + win_w - 18;
        int close_y = win_y + 4;
        if (mouse_x >= close_x && mouse_x <= close_x + 14 &&
            mouse_y >= close_y && mouse_y <= close_y + 14) {
            terminal_open = 0;
            return;
        }
        
        int min_x = win_x + win_w - 54;
        int min_y = win_y + 4;
        if (mouse_x >= min_x && mouse_x <= min_x + 14 &&
            mouse_y >= min_y && mouse_y <= min_y + 14) {
            terminal_open = 0;
            return;
        }
    }
    
    if (start_menu_open) {
        int menu_x = (VGA_WIDTH - 200) / 2 + 10;
        int menu_y = VGA_HEIGHT - DOCK_HEIGHT - 160;
        
        if (mouse_x >= menu_x + 5 && mouse_x <= menu_x + 175 &&
            mouse_y >= menu_y + 30 && mouse_y <= menu_y + 55) {
            terminal_open = 1;
            start_menu_open = 0;
            if (start_time == 0) start_time = 1;
            return;
        }
        
        if (mouse_x >= menu_x + 5 && mouse_x <= menu_x + 175 &&
            mouse_y >= menu_y + 60 && mouse_y <= menu_y + 85) {
            start_menu_open = 0;
            return;
        }
        
        if (mouse_x >= menu_x + 5 && mouse_x <= menu_x + 175 &&
            mouse_y >= menu_y + 90 && mouse_y <= menu_y + 115) {
            start_menu_open = 0;
            return;
        }
        
        if (mouse_x >= menu_x + 5 && mouse_x <= menu_x + 5 + (170 / 2) &&
            mouse_y >= menu_y + 125 && mouse_y <= menu_y + 150) {
            add_output_line("shutting down...");
            shutdown_system();
            return;
        }
        
        if (mouse_x >= menu_x + 10 + (170 / 2) && mouse_x <= menu_x + 175 &&
            mouse_y >= menu_y + 125 && mouse_y <= menu_y + 150) {
            add_output_line("rebooting...");
            reboot_system();
            return;
        }
    }
    
    if (start_menu_open) {
        start_menu_open = 0;
    }
}

void redraw_screen(void) {
    draw_wallpaper();
    draw_dock();
    draw_start_menu();
    draw_terminal();
    draw_cursor(mouse_x, mouse_y);
    flip_buffer();
}

void delay(uint32_t count) {
    for (volatile uint32_t i = 0; i < count * 1000; i++);
}

void window_manager_init(void) {
    mouse_x = VGA_WIDTH / 2;
    mouse_y = VGA_HEIGHT / 2;
    ctrl_pressed = 0;
    alt_pressed = 0;
    terminal_open = 0;
    start_menu_open = 0;
    last_clicked_icon = -1;
    click_frame_count = 0;
    output_line_count = 0;
    char_count = 0;
    start_time = 0;
    
    terminal_cursor = 0;
    for (int i = 0; i < TERM_BUFFER_SIZE; i++) {
        terminal_buffer[i] = 0;
    }
    
    for (int i = 0; i < TERM_OUTPUT_LINES; i++) {
        for (int j = 0; j < 40; j++) {
            terminal_output[i][j] = 0;
        }
    }
    
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        back_buffer[i] = 0;
    }
}

void window_manager_run(void) {
    redraw_screen();
    
    uint8_t needs_redraw = 0;
    uint8_t frame_count = 0;
    static uint8_t last_alt_state = 0;
    
    while (1) {
        uint8_t scan = read_scan_code();
        
        if (scan != 0) {
            needs_redraw = 1;
            handle_keyboard();
        }
        
        if (alt_pressed != last_alt_state) {
            last_alt_state = alt_pressed;
            needs_redraw = 1;
        }
        
        if (click_frame_count > 0) {
            click_frame_count--;
            needs_redraw = 1;
        }
        
        static int last_mouse_x = -1;
        static int last_mouse_y = -1;
        if (mouse_x != last_mouse_x || mouse_y != last_mouse_y) {
            last_mouse_x = mouse_x;
            last_mouse_y = mouse_y;
            needs_redraw = 1;
        }
        
        frame_count++;
        if (frame_count > 10) {
            frame_count = 0;
            if (terminal_open) {
                needs_redraw = 1;
            }
        }
        
        if (needs_redraw) {
            redraw_screen();
            needs_redraw = 0;
        }
        
        delay(2);
    }
}

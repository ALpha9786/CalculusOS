// /kernel/window.c - Calculus OS Window Manager
#include "../types.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 200
#define DOCK_HEIGHT 25

extern uint8_t* vga_memory;
extern void outb(uint16_t, uint8_t);
extern uint8_t inb(uint16_t);

typedef struct {
    int x, y, width, height;
    uint8_t border_color;
    uint8_t bg_color;
    uint8_t title_color;
    char title[64];
    int active;
    int minimized;
} Window;

Window windows[16];
int window_count = 0;

// UI state
static int start_menu_open = 0;

// Mouse state
static int mouse_x = SCREEN_WIDTH / 2;
static int mouse_y = SCREEN_HEIGHT / 2;
static int mouse_down = 0;
static int prev_mouse_x = SCREEN_WIDTH / 2;
static int prev_mouse_y = SCREEN_HEIGHT / 2;
static int prev_mouse_down = 0;

// Screen buffer
static uint8_t screen_buffer[SCREEN_WIDTH * SCREEN_HEIGHT];

void set_vga_mode() {
    outb(0x3C2, 0x63);
    outb(0x3C4, 0x00); outb(0x3C5, 0x03);
    outb(0x3C4, 0x01); outb(0x3C5, 0x01);
    outb(0x3C4, 0x02); outb(0x3C5, 0x0F);
    outb(0x3C4, 0x03); outb(0x3C5, 0x00);
    outb(0x3C4, 0x04); outb(0x3C5, 0x0E);
    outb(0x3D4, 0x11); outb(0x3D5, 0x00);
    
    uint8_t crtc[] = {0x5F,0x4F,0x50,0x82,0x54,0x80,0xBF,0x1F,0x00,0x41,0x00,0x00,0x00,0x00,0x00,0x00,0x9C,0x0E,0x8F,0x28,0x40,0x96,0xB9,0xA3,0xFF};
    for (int i = 0; i < 25; i++) { outb(0x3D4, i); outb(0x3D5, crtc[i]); }
    
    outb(0x3CE, 0x00); outb(0x3CF, 0x00);
    outb(0x3CE, 0x01); outb(0x3CF, 0x00);
    outb(0x3CE, 0x02); outb(0x3CF, 0x00);
    outb(0x3CE, 0x03); outb(0x3CF, 0x00);
    outb(0x3CE, 0x04); outb(0x3CF, 0x00);
    outb(0x3CE, 0x05); outb(0x3CF, 0x40);
    outb(0x3CE, 0x06); outb(0x3CF, 0x05);
    outb(0x3CE, 0x07); outb(0x3CF, 0x0F);
    outb(0x3CE, 0x08); outb(0x3CF, 0xFF);
    
    for (int i = 0; i < 16; i++) { (void)inb(0x3DA); outb(0x3C0, i); outb(0x3C0, i); }
    (void)inb(0x3DA); outb(0x3C0, 0x10); outb(0x3C0, 0x41);
    (void)inb(0x3DA); outb(0x3C0, 0x11); outb(0x3C0, 0x00);
    (void)inb(0x3DA); outb(0x3C0, 0x12); outb(0x3C0, 0x0F);
    (void)inb(0x3DA); outb(0x3C0, 0x13); outb(0x3C0, 0x00);
    (void)inb(0x3DA); outb(0x3C0, 0x14); outb(0x3C0, 0x00);
    (void)inb(0x3DA); outb(0x3C0, 0x20);
    
    for (int i = 0; i < 63; i++) {
        float t = (float)i / 62.0f;
        uint8_t r = 255 - (uint8_t)(t * 255);
        uint8_t g = 255 - (uint8_t)(t * 111);  
        uint8_t b = 255;
        outb(0x3C8, i); outb(0x3C9, r >> 2); outb(0x3C9, g >> 2); outb(0x3C9, b >> 2);
    }
    
    outb(0x3C8, 63); outb(0x3C9, 0x2C >> 2); outb(0x3C9, 0x3E >> 2); outb(0x3C9, 0x50 >> 2);
    outb(0x3C8, 64); outb(0x3C9, 0x34 >> 2); outb(0x3C9, 0x49 >> 2); outb(0x3C9, 0x5E >> 2);
    outb(0x3C8, 65); outb(0x3C9, 0x4A >> 2); outb(0x3C9, 0x90 >> 2); outb(0x3C9, 0xE2 >> 2);
    outb(0x3C8, 66); outb(0x3C9, 0x6B >> 2); outb(0x3C9, 0xA8 >> 2); outb(0x3C9, 0xF5 >> 2);
    outb(0x3C8, 67); outb(0x3C9, 0x35 >> 2); outb(0x3C9, 0x7A >> 2); outb(0x3C9, 0xBD >> 2);
    outb(0x3C8, 68); outb(0x3C9, 63); outb(0x3C9, 63); outb(0x3C9, 63);
}

void draw_pixel(int x, int y, uint8_t color) {
    if (x < 0 || x >= SCREEN_WIDTH || y < 0 || y >= SCREEN_HEIGHT) return;
    screen_buffer[y * SCREEN_WIDTH + x] = color;
}

void copy_buffer_to_screen() {
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        vga_memory[i] = screen_buffer[i];
    }
}

void draw_rect(int x, int y, int width, int height, uint8_t color) {
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 0; dx < width; dx++) {
            draw_pixel(x + dx, y + dy, color);
        }
    }
}

void draw_char(int x, int y, char c, uint8_t color) {
    static const uint8_t font[128][8] = {
        ['S'] = {0x3E, 0x60, 0x60, 0x3C, 0x06, 0x06, 0x7C, 0x00},
        ['t'] = {0x10, 0x10, 0x7C, 0x10, 0x10, 0x10, 0x0C, 0x00},
        ['a'] = {0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00},
        ['r'] = {0x00, 0x00, 0x5C, 0x62, 0x60, 0x60, 0x60, 0x00},
        ['M'] = {0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63, 0x00},
        ['m'] = {0x00, 0x00, 0x6C, 0xFE, 0xD6, 0xC6, 0xC6, 0x00},
        ['o'] = {0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00},
        ['u'] = {0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00},
        ['s'] = {0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00},
        ['e'] = {0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00},
        ['d'] = {0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00},
        ['w'] = {0x00, 0x00, 0x63, 0x63, 0x6B, 0x7F, 0x36, 0x00},
        ['n'] = {0x00, 0x00, 0x5C, 0x66, 0x66, 0x66, 0x66, 0x00},
        ['h'] = {0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x00},
        ['i'] = {0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00},
        ['p'] = {0x00, 0x00, 0x5C, 0x66, 0x66, 0x7C, 0x60, 0x60},
        ['A'] = {0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00},
        ['T'] = {0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00},
        ['R'] = {0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x66, 0x00},
        ['X'] = {0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00, 0x00},
        [':'] = {0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00},
        ['0'] = {0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C, 0x00},
        ['1'] = {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00},
        ['>'] = {0x00, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x00, 0x00},
        [' '] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    };
    
    if ((unsigned char)c >= 128) return;
    
    for (int dy = 0; dy < 8; dy++) {
        for (int dx = 0; dx < 8; dx++) {
            if (font[(int)c][dy] & (1 << (7 - dx))) {
                draw_pixel(x + dx, y + dy, color);
            }
        }
    }
}

void draw_string(int x, int y, const char* str, uint8_t color) {
    int offset = 0;
    while (*str) {
        draw_char(x + offset, y, *str, color);
        offset += 8;
        str++;
    }
}

void draw_gradient_background() {
    int gradient_height = SCREEN_HEIGHT - DOCK_HEIGHT;
    for (int y = 0; y < gradient_height; y++) {
        uint8_t color = (y * 62) / gradient_height;
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            draw_pixel(x, y, color);
        }
    }
}

void draw_rounded_rect(int x, int y, int width, int height, uint8_t color) {
    for (int dy = 1; dy < height - 1; dy++) {
        for (int dx = 0; dx < width; dx++) {
            draw_pixel(x + dx, y + dy, color);
        }
    }
    for (int dy = 0; dy < height; dy++) {
        for (int dx = 1; dx < width - 1; dx++) {
            draw_pixel(x + dx, y + dy, color);
        }
    }
}

void draw_start_button(int x, int y, int width, int height) {
    draw_rounded_rect(x, y, width, height, 65);
    draw_rect(x + 2, y + 1, width - 4, 1, 66);
    draw_rect(x + 2, y + height - 2, width - 4, 1, 67);
    
    int icon_x = x + 4, icon_y = y + (height - 10) / 2;
    draw_rect(icon_x, icon_y, 10, 10, 68);
    draw_rect(icon_x + 1, icon_y + 1, 8, 8, 65);
    draw_rect(icon_x + 3, icon_y + 3, 4, 4, 68);
    
    draw_string(icon_x + 14, y + (height - 8) / 2, "Start", 68);
}

void draw_start_menu() {
    int menu_x = 5;
    int menu_y = SCREEN_HEIGHT - DOCK_HEIGHT - 90;
    int menu_width = 100;
    int menu_height = 90;
    
    draw_rect(menu_x, menu_y, menu_width, menu_height, 63);
    draw_rect(menu_x, menu_y, menu_width, 1, 64);
    
    draw_string(menu_x + 5, menu_y + 5, "mathsh", 68);
    draw_rect(menu_x + 2, menu_y + 18, menu_width - 4, 1, 64);
    
    draw_string(menu_x + 5, menu_y + 25, "Shutdown", 68);
    draw_string(menu_x + 5, menu_y + 40, "Restart", 68);
    draw_string(menu_x + 5, menu_y + 55, "Apps", 68);
}

void draw_dock() {
    int dock_y = SCREEN_HEIGHT - DOCK_HEIGHT;
    draw_rect(0, dock_y, SCREEN_WIDTH, DOCK_HEIGHT, 63);
    draw_rect(0, dock_y, SCREEN_WIDTH, 1, 64);
    draw_start_button(5, dock_y + 5, 60, 20);
    
    int dock_x = 70;
    for (int i = 0; i < window_count; i++) {
        if (windows[i].active && !windows[i].minimized) {
            draw_rounded_rect(dock_x, dock_y + 5, 50, 20, 65);
            draw_string(dock_x + 3, dock_y + 11, windows[i].title, 68);
            dock_x += 55;
        }
    }
}

void draw_mouse_status() {
    draw_rect(SCREEN_WIDTH - 80, 2, 78, 10, 63);
    draw_string(SCREEN_WIDTH - 78, 3, "Mousedown:", 68);
    draw_char(SCREEN_WIDTH - 10, 3, mouse_down ? '1' : '0', mouse_down ? 66 : 68);
}

#define CURSOR_WIDTH 8
#define CURSOR_HEIGHT 12
#define CURSOR_OUTLINE_COLOR 0
#define CURSOR_FILL_COLOR 68

static const uint8_t cursor_bitmap[CURSOR_HEIGHT][CURSOR_WIDTH] = {
    {2, 2, 0, 0, 0, 0, 0, 0},
    {2, 1, 2, 0, 0, 0, 0, 0},
    {2, 1, 1, 2, 0, 0, 0, 0},
    {2, 1, 1, 1, 2, 0, 0, 0},
    {2, 1, 1, 1, 1, 2, 0, 0},
    {2, 1, 1, 1, 1, 1, 2, 0},
    {2, 1, 1, 1, 1, 2, 2, 2},
    {2, 1, 1, 2, 2, 2, 0, 0},
    {2, 1, 2, 0, 0, 0, 0, 0},
    {2, 2, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
};

void draw_cursor(int x, int y) {
    for (int dy = 0; dy < CURSOR_HEIGHT; dy++) {
        for (int dx = 0; dx < CURSOR_WIDTH; dx++) {
            uint8_t pixel = cursor_bitmap[dy][dx];
            if (pixel == 1) {
                draw_pixel(x + dx, y + dy, CURSOR_FILL_COLOR);
            } else if (pixel == 2) {
                draw_pixel(x + dx, y + dy, CURSOR_OUTLINE_COLOR);
            }
        }
    }
}

Window* create_window(int x, int y, int width, int height, const char* title) {
    if (window_count >= 16) return NULL;
    
    Window* win = &windows[window_count++];
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->border_color = 65;
    win->bg_color = 0;
    win->title_color = 68;
    win->active = 1;
    win->minimized = 0;
    
    int i = 0;
    while (title[i] && i < 63) {
        win->title[i] = title[i];
        i++;
    }
    win->title[i] = '\0';
    
    return win;
}

void draw_window(Window* win) {
    if (!win->active || win->minimized) return;
    
    draw_rect(win->x, win->y, win->width, 20, win->border_color);
    draw_string(win->x + 5, win->y + 6, win->title, win->title_color);
    
    int close_x = win->x + win->width - 18;
    int close_y = win->y + 2;
    draw_rect(close_x, close_y, 16, 16, 12);
    draw_char(close_x + 4, close_y + 4, 'X', 68);
    
    draw_rect(win->x, win->y + 20, win->width, win->height - 20, win->bg_color);
    
    if (win->title[0] == 'm' && win->title[1] == 'a') {
        draw_string(win->x + 5, win->y + 25, "mathsh>", 68);
    }
    
    draw_rect(win->x, win->y, win->width, 1, win->border_color);
    draw_rect(win->x, win->y + win->height - 1, win->width, 1, win->border_color);
    draw_rect(win->x, win->y, 1, win->height, win->border_color);
    draw_rect(win->x + win->width - 1, win->y, 1, win->height, win->border_color);
}

void handle_input() {
    if (inb(0x64) & 1) {
        uint8_t scan = inb(0x60);
        
        if (scan == 0x48) {
            mouse_y = (mouse_y > 0) ? mouse_y - 1 : 0;
        } else if (scan == 0x50) {
            mouse_y = (mouse_y < SCREEN_HEIGHT - CURSOR_HEIGHT) ? mouse_y + 1 : SCREEN_HEIGHT - CURSOR_HEIGHT;
        } else if (scan == 0x4B) {
            mouse_x = (mouse_x > 0) ? mouse_x - 1 : 0;
        } else if (scan == 0x4D) {
            mouse_x = (mouse_x < SCREEN_WIDTH - CURSOR_WIDTH) ? mouse_x + 1 : SCREEN_WIDTH - CURSOR_WIDTH;
        } else if (scan == 0x36) {
            mouse_down = 1;
            
            int dock_y = SCREEN_HEIGHT - DOCK_HEIGHT;
            if (mouse_x >= 5 && mouse_x <= 65 && mouse_y >= dock_y + 5 && mouse_y <= dock_y + 25) {
                start_menu_open = !start_menu_open;
            }
            
            if (start_menu_open) {
                int menu_x = 5;
                int menu_y = SCREEN_HEIGHT - DOCK_HEIGHT - 90;
                
                if (mouse_x >= menu_x && mouse_x <= menu_x + 100) {
                    if (mouse_y >= menu_y + 5 && mouse_y <= menu_y + 15) {
                        create_window(50, 30, 200, 120, "mathsh");
                        start_menu_open = 0;
                    }
                }
            }
            
            for (int i = 0; i < window_count; i++) {
                if (windows[i].active && !windows[i].minimized) {
                    int close_x = windows[i].x + windows[i].width - 18;
                    int close_y = windows[i].y + 2;
                    
                    if (mouse_x >= close_x && mouse_x <= close_x + 16 &&
                        mouse_y >= close_y && mouse_y <= close_y + 16) {
                        windows[i].active = 0;
                    }
                }
            }
            
        } else if (scan == 0xB6) {
            mouse_down = 0;
        }
    }
}

void init_windows() {
    window_count = 0;
    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++) {
        screen_buffer[i] = 0;
    }
}

void render_windows() {
    draw_gradient_background();
    
    for (int i = 0; i < window_count; i++) {
        draw_window(&windows[i]);
    }
    
    draw_dock();
    
    if (start_menu_open) {
        draw_start_menu();
    }
    
    draw_mouse_status();
    draw_cursor(mouse_x, mouse_y);
    copy_buffer_to_screen();
}

void update_screen() {
    handle_input();
    
    if (mouse_x != prev_mouse_x || mouse_y != prev_mouse_y || 
        mouse_down != prev_mouse_down || start_menu_open) {
        render_windows();
        prev_mouse_x = mouse_x;
        prev_mouse_y = mouse_y;
        prev_mouse_down = mouse_down;
    }
}

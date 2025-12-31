// font.h - Font header for CalculusOS
#ifndef FONT_H
#define FONT_H

#include <stdint.h>

// Get font data for a character (returns 8x8 bitmap)
const uint8_t* get_font_char(char c);

#endif // FONT_H
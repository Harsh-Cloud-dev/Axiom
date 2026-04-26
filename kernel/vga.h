#ifndef VGA_H
#define VGA_H
#include "types.h"

#define VGA_BLACK        0x0
#define VGA_BLUE         0x1
#define VGA_GREEN        0x2
#define VGA_CYAN         0x3
#define VGA_RED          0x4
#define VGA_MAGENTA      0x5
#define VGA_BROWN        0x6
#define VGA_LIGHT_GREY   0x7
#define VGA_DARK_GREY    0x8
#define VGA_LIGHT_BLUE   0x9
#define VGA_LIGHT_GREEN  0xA
#define VGA_LIGHT_CYAN   0xB
#define VGA_LIGHT_RED    0xC
#define VGA_PINK         0xD
#define VGA_YELLOW       0xE
#define VGA_WHITE        0xF

#define VGA_COLOR(fg, bg) ((uint8_t)(((bg) << 4) | (fg)))

void vga_init(void);
void vga_clear(uint8_t color);
void vga_putc(char c, uint8_t color);
void vga_puts(const char *s, uint8_t color);
void vga_puts_at(int row, int col, uint8_t color, const char *s);
void vga_puthex(uint64_t val, uint8_t color);
void vga_set_cursor(int row, int col);
void vga_get_cursor(int *row, int *col);
void vga_scroll_view_up(int lines);
void vga_scroll_view_down(int lines);
int  vga_is_scrolled(void);

#endif
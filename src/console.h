#pragma once
#include <stdint.h>

/* 80x30 colored text console rendered via PVR */
#define CON_W 80
#define CON_H 30
#define CELL_W 8 /* pixels per character */
#define CELL_H 16

/* Named colors (ARGB8888) */
#define COL_BLACK 0xFF000000
#define COL_DARK_GREY 0xFF444444
#define COL_GREY 0xFF888888
#define COL_WHITE 0xFFFFFFFF
#define COL_RED 0xFFCC2222
#define COL_DARK_RED 0xFF881111
#define COL_GREEN 0xFF22CC44
#define COL_DARK_GREEN 0xFF116622
#define COL_YELLOW 0xFFDDCC22
#define COL_DARK_YELLOW 0xFF887711
#define COL_BLUE 0xFF4466CC
#define COL_DARK_BLUE 0xFF223388
#define COL_CYAN 0xFF22CCCC
#define COL_MAGENTA 0xFFCC44CC
#define COL_ORANGE 0xFFDD7722
#define COL_BROWN 0xFF885533
#define COL_LIGHT_GREY 0xFFBBBBBB
#define COL_PANEL_BG 0xFF09090F
#define COL_GOLD 0xFFCC9922

typedef struct
{
    uint8_t ch;
    uint32_t fg;
    uint32_t bg;
} Cell;

void console_init(void);
void console_shutdown(void);
void console_clear(void);
void console_put(int x, int y, uint8_t ch, uint32_t fg, uint32_t bg);
void console_print(int x, int y, const char* s, uint32_t fg, uint32_t bg);
void console_printf(int x, int y, uint32_t fg, uint32_t bg, const char* fmt, ...);
void console_hline(int x, int y, int w, uint8_t ch, uint32_t fg, uint32_t bg);
void console_vline(int x, int y, int h, uint8_t ch, uint32_t fg, uint32_t bg);
void console_border(int x, int y, int w, int h, uint32_t fg, uint32_t bg);
void console_render(void);

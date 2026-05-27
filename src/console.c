#include "console.h"
#include <dc/pvr.h>
#include <kos.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Font atlas: 128x128, 16 cols x 16 rows of 8x8 glyphs (ARGB4444)
   The atlas is white-on-transparent so we can vertex-color the fg. */
extern const unsigned char font_atlas_data[];
extern const int font_atlas_size;

#define FONT_ATLAS_W 128
#define FONT_ATLAS_H 128
#define FONT_COLS 16
#define FONT_ROWS 16
#define GLYPH_W 8
#define GLYPH_H 8

static Cell grid[CON_H][CON_W];

static pvr_ptr_t font_tex;

static pvr_poly_hdr_t bg_hdr; /* opaque, no texture */
static pvr_poly_hdr_t fg_hdr; /* transparent, textured, vertex-colored */

void console_init(void)
{
    font_tex = pvr_mem_malloc(FONT_ATLAS_W * FONT_ATLAS_H * 2);
    pvr_txr_load((void *)font_atlas_data, font_tex, FONT_ATLAS_W * FONT_ATLAS_H * 2);

    pvr_poly_cxt_t cxt;
    pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);
    pvr_poly_compile(&bg_hdr, &cxt);

    pvr_poly_cxt_txr(&cxt, PVR_LIST_TR_POLY, PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_NONTWIDDLED, FONT_ATLAS_W, FONT_ATLAS_H, font_tex, PVR_FILTER_NEAREST);
    cxt.blend.src = PVR_BLEND_SRCALPHA;
    cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
    pvr_poly_compile(&fg_hdr, &cxt);

    console_clear();
}

void console_shutdown(void)
{
    pvr_mem_free(font_tex);
}

void console_clear(void)
{
    for (int y = 0; y < CON_H; y++)
    {
        for (int x = 0; x < CON_W; x++)
        {
            grid[y][x].ch = ' ';
            grid[y][x].fg = COL_WHITE;
            grid[y][x].bg = COL_BLACK;
        }
    }
}

void console_put(int x, int y, uint8_t ch, uint32_t fg, uint32_t bg)
{
    if (x < 0 || x >= CON_W || y < 0 || y >= CON_H)
        return;

    grid[y][x].ch = ch;
    grid[y][x].fg = fg;
    grid[y][x].bg = bg;
}

void console_print(int x, int y, const char *s, uint32_t fg, uint32_t bg)
{
    for (; *s && x < CON_W; s++, x++)
    {
        console_put(x, y, (uint8_t)*s, fg, bg);
    }
}

void console_printf(int x, int y, uint32_t fg, uint32_t bg, const char *fmt, ...)
{
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    console_print(x, y, buf, fg, bg);
}

void console_hline(int x, int y, int w, uint8_t ch, uint32_t fg, uint32_t bg)
{
    for (int i = 0; i < w; i++)
    {
        console_put(x + i, y, ch, fg, bg);
    }
}

void console_vline(int x, int y, int h, uint8_t ch, uint32_t fg, uint32_t bg)
{
    for (int i = 0; i < h; i++)
    {
        console_put(x, y + i, ch, fg, bg);
    }
}

#define CH_HLINE 0xC4 /* ─ */
#define CH_VLINE 0xB3 /* │ */
#define CH_TL 0xDA    /* ┌ */
#define CH_TR 0xBF    /* ┐ */
#define CH_BL 0xC0    /* └ */
#define CH_BR 0xD9    /* ┘ */

void console_border(int x, int y, int w, int h, uint32_t fg, uint32_t bg)
{
    console_put(x, y, CH_TL, fg, bg);
    console_put(x + w - 1, y, CH_TR, fg, bg);
    console_put(x, y + h - 1, CH_BL, fg, bg);
    console_put(x + w - 1, y + h - 1, CH_BR, fg, bg);
    console_hline(x + 1, y, w - 2, CH_HLINE, fg, bg);
    console_hline(x + 1, y + h - 1, w - 2, CH_HLINE, fg, bg);
    console_vline(x, y + 1, h - 2, CH_VLINE, fg, bg);
    console_vline(x + w - 1, y + 1, h - 2, CH_VLINE, fg, bg);
}

static inline float glyph_u0(uint8_t ch)
{
    return (ch % FONT_COLS) * (1.0f / FONT_COLS);
}
static inline float glyph_v0(uint8_t ch)
{
    return (ch / FONT_COLS) * (1.0f / FONT_ROWS);
}
static inline float glyph_u1(uint8_t ch)
{
    return glyph_u0(ch) + (1.0f / FONT_COLS);
}
static inline float glyph_v1(uint8_t ch)
{
    return glyph_v0(ch) + (1.0f / FONT_ROWS);
}

static void emit_quad_col(float x0, float y0, float x1, float y1, float z, uint32_t col)
{
    pvr_vertex_t v[4];
    v[0].flags = PVR_CMD_VERTEX;
    v[0].x = x0;
    v[0].y = y0;
    v[0].z = z;
    v[0].u = 0;
    v[0].v = 0;
    v[0].argb = col;
    v[0].oargb = 0;
    v[1].flags = PVR_CMD_VERTEX;
    v[1].x = x1;
    v[1].y = y0;
    v[1].z = z;
    v[1].u = 0;
    v[1].v = 0;
    v[1].argb = col;
    v[1].oargb = 0;
    v[2].flags = PVR_CMD_VERTEX;
    v[2].x = x0;
    v[2].y = y1;
    v[2].z = z;
    v[2].u = 0;
    v[2].v = 0;
    v[2].argb = col;
    v[2].oargb = 0;
    v[3].flags = PVR_CMD_VERTEX_EOL;
    v[3].x = x1;
    v[3].y = y1;
    v[3].z = z;
    v[3].u = 0;
    v[3].v = 0;
    v[3].argb = col;
    v[3].oargb = 0;
    pvr_prim(&v[0], sizeof(pvr_vertex_t));
    pvr_prim(&v[1], sizeof(pvr_vertex_t));
    pvr_prim(&v[2], sizeof(pvr_vertex_t));
    pvr_prim(&v[3], sizeof(pvr_vertex_t));
}

static void emit_quad_txr(float x0, float y0, float x1, float y1, float z, float u0, float v0, float u1, float v1, uint32_t col)
{
    pvr_vertex_t v[4];
    v[0].flags = PVR_CMD_VERTEX;
    v[0].x = x0;
    v[0].y = y0;
    v[0].z = z;
    v[0].u = u0;
    v[0].v = v0;
    v[0].argb = col;
    v[0].oargb = 0;
    v[1].flags = PVR_CMD_VERTEX;
    v[1].x = x1;
    v[1].y = y0;
    v[1].z = z;
    v[1].u = u1;
    v[1].v = v0;
    v[1].argb = col;
    v[1].oargb = 0;
    v[2].flags = PVR_CMD_VERTEX;
    v[2].x = x0;
    v[2].y = y1;
    v[2].z = z;
    v[2].u = u0;
    v[2].v = v1;
    v[2].argb = col;
    v[2].oargb = 0;
    v[3].flags = PVR_CMD_VERTEX_EOL;
    v[3].x = x1;
    v[3].y = y1;
    v[3].z = z;
    v[3].u = u1;
    v[3].v = v1;
    v[3].argb = col;
    v[3].oargb = 0;
    pvr_prim(&v[0], sizeof(pvr_vertex_t));
    pvr_prim(&v[1], sizeof(pvr_vertex_t));
    pvr_prim(&v[2], sizeof(pvr_vertex_t));
    pvr_prim(&v[3], sizeof(pvr_vertex_t));
}

void console_render(void)
{
    pvr_wait_ready();
    pvr_scene_begin();

    pvr_list_begin(PVR_LIST_OP_POLY);
    pvr_prim(&bg_hdr, sizeof(pvr_poly_hdr_t));
    for (int y = 0; y < CON_H; y++)
    {
        for (int x = 0; x < CON_W; x++)
        {
            float sx = x * CELL_W, sy = y * CELL_H;
            emit_quad_col(sx, sy, sx + CELL_W, sy + CELL_H, 1.0f, grid[y][x].bg);
        }
    }
    pvr_list_finish();

    pvr_list_begin(PVR_LIST_TR_POLY);
    pvr_prim(&fg_hdr, sizeof(pvr_poly_hdr_t));
    for (int y = 0; y < CON_H; y++)
    {
        for (int x = 0; x < CON_W; x++)
        {
            uint8_t ch = grid[y][x].ch;
            if (ch == ' ')
                continue;
            float sx = x * CELL_W, sy = y * CELL_H;
            emit_quad_txr(sx, sy, sx + CELL_W, sy + CELL_H, 2.0f, glyph_u0(ch), glyph_v0(ch), glyph_u1(ch), glyph_v1(ch), grid[y][x].fg);
        }
    }
    pvr_list_finish();

    pvr_scene_finish();
}

/*
 * ne_gdi.c - Phase E GDI.EXE subsystem implementation
 *
 * Provides device context management, paint session handling, and real
 * rendering via a memory-backed VGA framebuffer (640x480, 8-bit indexed).
 * Implements GDI object management, bitmap font text rendering, line and
 * shape drawing, bit-block transfer, and colour attribute management.
 *
 * Reference: Microsoft Windows 3.1 SDK – Graphics Device Interface.
 */

#include "ne_gdi.h"

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Built-in 8x8 bitmap font for printable ASCII (chars 32–126)
 *
 * Each glyph is 8 rows of 8 pixels.  Each byte encodes one row; bit 7 is
 * the leftmost pixel.  95 glyphs cover space (32) through tilde (126).
 * ---------------------------------------------------------------------- */

static const uint8_t ne_gdi_font8x8[95][8] = {
    /* 32 ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 33 '!' */ {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00},
    /* 34 '"' */ {0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00},
    /* 35 '#' */ {0x24,0x7E,0x24,0x24,0x7E,0x24,0x00,0x00},
    /* 36 '$' */ {0x18,0x3E,0x58,0x3C,0x1A,0x7C,0x18,0x00},
    /* 37 '%' */ {0x62,0x64,0x08,0x10,0x26,0x46,0x00,0x00},
    /* 38 '&' */ {0x30,0x48,0x30,0x56,0x48,0x34,0x00,0x00},
    /* 39 ''' */ {0x18,0x18,0x10,0x00,0x00,0x00,0x00,0x00},
    /* 40 '(' */ {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00},
    /* 41 ')' */ {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00},
    /* 42 '*' */ {0x00,0x24,0x18,0x7E,0x18,0x24,0x00,0x00},
    /* 43 '+' */ {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00},
    /* 44 ',' */ {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x10},
    /* 45 '-' */ {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00},
    /* 46 '.' */ {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00},
    /* 47 '/' */ {0x02,0x04,0x08,0x10,0x20,0x40,0x00,0x00},
    /* 48 '0' */ {0x3C,0x46,0x4A,0x52,0x62,0x3C,0x00,0x00},
    /* 49 '1' */ {0x18,0x38,0x18,0x18,0x18,0x7E,0x00,0x00},
    /* 50 '2' */ {0x3C,0x42,0x02,0x3C,0x40,0x7E,0x00,0x00},
    /* 51 '3' */ {0x3C,0x42,0x0C,0x02,0x42,0x3C,0x00,0x00},
    /* 52 '4' */ {0x08,0x18,0x28,0x48,0x7E,0x08,0x00,0x00},
    /* 53 '5' */ {0x7E,0x40,0x7C,0x02,0x42,0x3C,0x00,0x00},
    /* 54 '6' */ {0x1C,0x20,0x40,0x7C,0x42,0x3C,0x00,0x00},
    /* 55 '7' */ {0x7E,0x02,0x04,0x08,0x10,0x10,0x00,0x00},
    /* 56 '8' */ {0x3C,0x42,0x3C,0x42,0x42,0x3C,0x00,0x00},
    /* 57 '9' */ {0x3C,0x42,0x3E,0x02,0x04,0x38,0x00,0x00},
    /* 58 ':' */ {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00},
    /* 59 ';' */ {0x00,0x18,0x18,0x00,0x18,0x18,0x10,0x00},
    /* 60 '<' */ {0x06,0x18,0x60,0x18,0x06,0x00,0x00,0x00},
    /* 61 '=' */ {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00},
    /* 62 '>' */ {0x60,0x18,0x06,0x18,0x60,0x00,0x00,0x00},
    /* 63 '?' */ {0x3C,0x42,0x04,0x08,0x00,0x08,0x00,0x00},
    /* 64 '@' */ {0x3C,0x42,0x5E,0x56,0x5E,0x40,0x3C,0x00},
    /* 65 'A' */ {0x18,0x24,0x42,0x7E,0x42,0x42,0x00,0x00},
    /* 66 'B' */ {0x7C,0x42,0x7C,0x42,0x42,0x7C,0x00,0x00},
    /* 67 'C' */ {0x3C,0x42,0x40,0x40,0x42,0x3C,0x00,0x00},
    /* 68 'D' */ {0x78,0x44,0x42,0x42,0x44,0x78,0x00,0x00},
    /* 69 'E' */ {0x7E,0x40,0x7C,0x40,0x40,0x7E,0x00,0x00},
    /* 70 'F' */ {0x7E,0x40,0x7C,0x40,0x40,0x40,0x00,0x00},
    /* 71 'G' */ {0x3C,0x42,0x40,0x4E,0x42,0x3C,0x00,0x00},
    /* 72 'H' */ {0x42,0x42,0x7E,0x42,0x42,0x42,0x00,0x00},
    /* 73 'I' */ {0x7E,0x18,0x18,0x18,0x18,0x7E,0x00,0x00},
    /* 74 'J' */ {0x1E,0x06,0x06,0x06,0x46,0x3C,0x00,0x00},
    /* 75 'K' */ {0x44,0x48,0x70,0x48,0x44,0x42,0x00,0x00},
    /* 76 'L' */ {0x40,0x40,0x40,0x40,0x40,0x7E,0x00,0x00},
    /* 77 'M' */ {0x42,0x66,0x5A,0x42,0x42,0x42,0x00,0x00},
    /* 78 'N' */ {0x42,0x62,0x52,0x4A,0x46,0x42,0x00,0x00},
    /* 79 'O' */ {0x3C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00},
    /* 80 'P' */ {0x7C,0x42,0x42,0x7C,0x40,0x40,0x00,0x00},
    /* 81 'Q' */ {0x3C,0x42,0x42,0x4A,0x44,0x3A,0x00,0x00},
    /* 82 'R' */ {0x7C,0x42,0x42,0x7C,0x44,0x42,0x00,0x00},
    /* 83 'S' */ {0x3C,0x40,0x3C,0x02,0x42,0x3C,0x00,0x00},
    /* 84 'T' */ {0x7E,0x18,0x18,0x18,0x18,0x18,0x00,0x00},
    /* 85 'U' */ {0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00},
    /* 86 'V' */ {0x42,0x42,0x42,0x24,0x24,0x18,0x00,0x00},
    /* 87 'W' */ {0x42,0x42,0x42,0x5A,0x66,0x42,0x00,0x00},
    /* 88 'X' */ {0x42,0x24,0x18,0x18,0x24,0x42,0x00,0x00},
    /* 89 'Y' */ {0x42,0x42,0x24,0x18,0x18,0x18,0x00,0x00},
    /* 90 'Z' */ {0x7E,0x04,0x08,0x10,0x20,0x7E,0x00,0x00},
    /* 91 '[' */ {0x3C,0x30,0x30,0x30,0x30,0x3C,0x00,0x00},
    /* 92 '\' */ {0x40,0x20,0x10,0x08,0x04,0x02,0x00,0x00},
    /* 93 ']' */ {0x3C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00,0x00},
    /* 94 '^' */ {0x18,0x24,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 95 '_' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00},
    /* 96 '`' */ {0x18,0x18,0x08,0x00,0x00,0x00,0x00,0x00},
    /* 97 'a' */ {0x00,0x00,0x3C,0x02,0x3E,0x42,0x3E,0x00},
    /* 98 'b' */ {0x40,0x40,0x7C,0x42,0x42,0x42,0x7C,0x00},
    /* 99 'c' */ {0x00,0x00,0x3C,0x40,0x40,0x40,0x3C,0x00},
    /*100 'd' */ {0x02,0x02,0x3E,0x42,0x42,0x42,0x3E,0x00},
    /*101 'e' */ {0x00,0x00,0x3C,0x42,0x7E,0x40,0x3C,0x00},
    /*102 'f' */ {0x0C,0x10,0x3C,0x10,0x10,0x10,0x10,0x00},
    /*103 'g' */ {0x00,0x00,0x3E,0x42,0x42,0x3E,0x02,0x3C},
    /*104 'h' */ {0x40,0x40,0x7C,0x42,0x42,0x42,0x42,0x00},
    /*105 'i' */ {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00},
    /*106 'j' */ {0x06,0x00,0x06,0x06,0x06,0x06,0x46,0x3C},
    /*107 'k' */ {0x40,0x40,0x44,0x48,0x70,0x48,0x44,0x00},
    /*108 'l' */ {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00},
    /*109 'm' */ {0x00,0x00,0x64,0x5A,0x42,0x42,0x42,0x00},
    /*110 'n' */ {0x00,0x00,0x7C,0x42,0x42,0x42,0x42,0x00},
    /*111 'o' */ {0x00,0x00,0x3C,0x42,0x42,0x42,0x3C,0x00},
    /*112 'p' */ {0x00,0x00,0x7C,0x42,0x42,0x7C,0x40,0x40},
    /*113 'q' */ {0x00,0x00,0x3E,0x42,0x42,0x3E,0x02,0x02},
    /*114 'r' */ {0x00,0x00,0x5C,0x62,0x40,0x40,0x40,0x00},
    /*115 's' */ {0x00,0x00,0x3E,0x40,0x3C,0x02,0x7C,0x00},
    /*116 't' */ {0x10,0x10,0x3C,0x10,0x10,0x10,0x0C,0x00},
    /*117 'u' */ {0x00,0x00,0x42,0x42,0x42,0x42,0x3E,0x00},
    /*118 'v' */ {0x00,0x00,0x42,0x42,0x24,0x24,0x18,0x00},
    /*119 'w' */ {0x00,0x00,0x42,0x42,0x42,0x5A,0x24,0x00},
    /*120 'x' */ {0x00,0x00,0x42,0x24,0x18,0x24,0x42,0x00},
    /*121 'y' */ {0x00,0x00,0x42,0x42,0x42,0x3E,0x02,0x3C},
    /*122 'z' */ {0x00,0x00,0x7E,0x04,0x18,0x20,0x7E,0x00},
    /*123 '{' */ {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00},
    /*124 '|' */ {0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00},
    /*125 '}' */ {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00},
    /*126 '~' */ {0x00,0x00,0x32,0x4C,0x00,0x00,0x00,0x00}
};

/* -------------------------------------------------------------------------
 * Static helper – find a device context by handle
 * ---------------------------------------------------------------------- */

static NEGdiDC *gdi_find_dc(NEGdiContext *ctx, NEGdiHDC hdc)
{
    unsigned i;

    for (i = 0; i < NE_GDI_DC_CAP; i++) {
        if (ctx->dcs[i].active && ctx->dcs[i].hdc == hdc)
            return &ctx->dcs[i];
    }

    return NULL;
}

/* -------------------------------------------------------------------------
 * Static helper – find a GDI object by handle
 * ---------------------------------------------------------------------- */

static NEGdiObject *gdi_find_obj(NEGdiContext *ctx, NEGdiHGDIOBJ handle)
{
    unsigned i;

    for (i = 0; i < NE_GDI_OBJ_CAP; i++) {
        if (ctx->objs[i].type != NE_GDI_OBJ_UNUSED &&
            ctx->objs[i].handle == handle)
            return &ctx->objs[i];
    }

    return NULL;
}

/* -------------------------------------------------------------------------
 * Static helper – allocate a GDI object slot
 * ---------------------------------------------------------------------- */

static NEGdiObject *gdi_alloc_obj(NEGdiContext *ctx)
{
    unsigned i;

    for (i = 0; i < NE_GDI_OBJ_CAP; i++) {
        if (ctx->objs[i].type == NE_GDI_OBJ_UNUSED) {
            ctx->objs[i].handle = ctx->next_obj++;
            ctx->obj_count++;
            return &ctx->objs[i];
        }
    }

    return NULL;
}

/* -------------------------------------------------------------------------
 * Static helper – get framebuffer pointer and dimensions for a DC
 * ---------------------------------------------------------------------- */

static uint8_t *gdi_dc_fb(NEGdiContext *ctx, NEGdiDC *dc,
                          int16_t *w, int16_t *h)
{
    if (dc->own_fb) {
        *w = dc->own_fb_w;
        *h = dc->own_fb_h;
        return dc->own_fb;
    }
    *w = NE_GDI_FB_WIDTH;
    *h = NE_GDI_FB_HEIGHT;
    return ctx->framebuffer;
}

/* -------------------------------------------------------------------------
 * Static helper – plot a single pixel into the correct framebuffer
 * ---------------------------------------------------------------------- */

static void gdi_plot(NEGdiContext *ctx, NEGdiDC *dc,
                     int16_t x, int16_t y, uint8_t color)
{
    int16_t w, h;
    uint8_t *fb = gdi_dc_fb(ctx, dc, &w, &h);

    if (!fb || x < 0 || y < 0 || x >= w || y >= h)
        return;

    fb[y * w + x] = color;
}

/* -------------------------------------------------------------------------
 * Static helper – Bresenham's line algorithm
 * ---------------------------------------------------------------------- */

static void gdi_draw_line(NEGdiContext *ctx, NEGdiDC *dc,
                          int16_t x0, int16_t y0,
                          int16_t x1, int16_t y1, uint8_t color)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int sx = (dx >= 0) ? 1 : -1;
    int sy = (dy >= 0) ? 1 : -1;
    int err, e2;

    dx = dx < 0 ? -dx : dx;
    dy = dy < 0 ? -dy : dy;
    err = dx - dy;

    for (;;) {
        gdi_plot(ctx, dc, x0, y0, color);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 = (int16_t)(x0 + sx);
        }
        if (e2 < dx) {
            err += dx;
            y0 = (int16_t)(y0 + sy);
        }
    }
}

/* -------------------------------------------------------------------------
 * Static helper – get the pen colour byte for a DC
 * ---------------------------------------------------------------------- */

static uint8_t gdi_pen_color(NEGdiContext *ctx, NEGdiDC *dc)
{
    NEGdiObject *obj;

    if (dc->sel_pen != NE_GDI_HGDIOBJ_INVALID) {
        obj = gdi_find_obj(ctx, dc->sel_pen);
        if (obj && obj->type == NE_GDI_OBJ_PEN)
            return (uint8_t)(obj->u.pen.color & 0xFF);
    }
    return 0xFF; /* default white */
}

/* -------------------------------------------------------------------------
 * Static helper – get the brush colour byte for a DC
 * ---------------------------------------------------------------------- */

static uint8_t gdi_brush_color(NEGdiContext *ctx, NEGdiDC *dc)
{
    NEGdiObject *obj;

    if (dc->sel_brush != NE_GDI_HGDIOBJ_INVALID) {
        obj = gdi_find_obj(ctx, dc->sel_brush);
        if (obj && obj->type == NE_GDI_OBJ_BRUSH)
            return (uint8_t)(obj->u.brush.color & 0xFF);
    }
    return 0x00; /* default black (no fill) */
}

/* =========================================================================
 * ne_gdi_init / ne_gdi_free
 * ===================================================================== */

int ne_gdi_init(NEGdiContext *ctx)
{
    if (!ctx)
        return NE_GDI_ERR_NULL;

    memset(ctx, 0, sizeof(*ctx));

    ctx->framebuffer = (uint8_t *)malloc(
        (size_t)NE_GDI_FB_WIDTH * NE_GDI_FB_HEIGHT);
    if (!ctx->framebuffer)
        return NE_GDI_ERR_NULL;
    memset(ctx->framebuffer, 0,
           (size_t)NE_GDI_FB_WIDTH * NE_GDI_FB_HEIGHT);

    ctx->next_hdc    = 1;
    ctx->next_obj    = 1;
    ctx->initialized = 1;
    return NE_GDI_OK;
}

void ne_gdi_free(NEGdiContext *ctx)
{
    unsigned i;

    if (!ctx)
        return;

    /* Free compatible-DC framebuffers */
    for (i = 0; i < NE_GDI_DC_CAP; i++) {
        if (ctx->dcs[i].own_fb) {
            free(ctx->dcs[i].own_fb);
            ctx->dcs[i].own_fb = NULL;
        }
    }

    /* Free bitmap pixel data */
    for (i = 0; i < NE_GDI_OBJ_CAP; i++) {
        if (ctx->objs[i].type == NE_GDI_OBJ_BITMAP &&
            ctx->objs[i].u.bitmap.bits) {
            free(ctx->objs[i].u.bitmap.bits);
            ctx->objs[i].u.bitmap.bits = NULL;
        }
    }

    free(ctx->framebuffer);
    memset(ctx, 0, sizeof(*ctx));
}

/* =========================================================================
 * Device context management
 * ===================================================================== */

NEGdiHDC ne_gdi_get_dc(NEGdiContext *ctx, uint16_t hwnd)
{
    unsigned i;

    if (!ctx || !ctx->initialized)
        return NE_GDI_HDC_INVALID;

    if (ctx->dc_count >= NE_GDI_DC_CAP)
        return NE_GDI_HDC_INVALID;

    for (i = 0; i < NE_GDI_DC_CAP; i++) {
        if (!ctx->dcs[i].active) {
            ctx->dcs[i].hdc        = ctx->next_hdc++;
            ctx->dcs[i].hwnd       = hwnd;
            ctx->dcs[i].cur_x      = 0;
            ctx->dcs[i].cur_y      = 0;
            ctx->dcs[i].active     = 1;
            ctx->dcs[i].in_paint   = 0;
            ctx->dcs[i].sel_pen    = NE_GDI_HGDIOBJ_INVALID;
            ctx->dcs[i].sel_brush  = NE_GDI_HGDIOBJ_INVALID;
            ctx->dcs[i].sel_font   = NE_GDI_HGDIOBJ_INVALID;
            ctx->dcs[i].sel_bitmap = NE_GDI_HGDIOBJ_INVALID;
            ctx->dcs[i].text_color = 0x00FFFFFF;
            ctx->dcs[i].bk_color   = 0x00000000;
            ctx->dcs[i].bk_mode    = NE_GDI_OPAQUE;
            ctx->dcs[i].own_fb     = NULL;
            ctx->dcs[i].own_fb_w   = 0;
            ctx->dcs[i].own_fb_h   = 0;
            ctx->dc_count++;
            return ctx->dcs[i].hdc;
        }
    }

    return NE_GDI_HDC_INVALID;
}

int ne_gdi_release_dc(NEGdiContext *ctx, uint16_t hwnd, NEGdiHDC hdc)
{
    NEGdiDC *dc;

    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return NE_GDI_ERR_NOT_FOUND;

    (void)hwnd; /* Win31 releases regardless of hwnd match */

    if (dc->own_fb) {
        free(dc->own_fb);
        dc->own_fb = NULL;
    }

    memset(dc, 0, sizeof(*dc));
    ctx->dc_count--;
    return NE_GDI_OK;
}

/* =========================================================================
 * Paint session management
 * ===================================================================== */

NEGdiHDC ne_gdi_begin_paint(NEGdiContext *ctx, uint16_t hwnd,
                            NEGdiPaintStruct *ps)
{
    unsigned i;
    NEGdiHDC hdc;

    if (!ctx || !ctx->initialized || !ps)
        return NE_GDI_HDC_INVALID;

    if (ctx->dc_count >= NE_GDI_DC_CAP)
        return NE_GDI_HDC_INVALID;

    for (i = 0; i < NE_GDI_DC_CAP; i++) {
        if (!ctx->dcs[i].active) {
            hdc = ctx->next_hdc++;
            ctx->dcs[i].hdc        = hdc;
            ctx->dcs[i].hwnd       = hwnd;
            ctx->dcs[i].cur_x      = 0;
            ctx->dcs[i].cur_y      = 0;
            ctx->dcs[i].active     = 1;
            ctx->dcs[i].in_paint   = 1;
            ctx->dcs[i].sel_pen    = NE_GDI_HGDIOBJ_INVALID;
            ctx->dcs[i].sel_brush  = NE_GDI_HGDIOBJ_INVALID;
            ctx->dcs[i].sel_font   = NE_GDI_HGDIOBJ_INVALID;
            ctx->dcs[i].sel_bitmap = NE_GDI_HGDIOBJ_INVALID;
            ctx->dcs[i].text_color = 0x00FFFFFF;
            ctx->dcs[i].bk_color   = 0x00000000;
            ctx->dcs[i].bk_mode    = NE_GDI_OPAQUE;
            ctx->dcs[i].own_fb     = NULL;
            ctx->dcs[i].own_fb_w   = 0;
            ctx->dcs[i].own_fb_h   = 0;
            ctx->dc_count++;

            ps->hdc      = hdc;
            ps->erase_bg = 1;
            memset(&ps->paint_rect, 0, sizeof(ps->paint_rect));
            return hdc;
        }
    }

    return NE_GDI_HDC_INVALID;
}

int ne_gdi_end_paint(NEGdiContext *ctx, uint16_t hwnd,
                     const NEGdiPaintStruct *ps)
{
    NEGdiDC *dc;

    if (!ctx || !ctx->initialized || !ps)
        return NE_GDI_ERR_NULL;

    dc = gdi_find_dc(ctx, ps->hdc);
    if (!dc)
        return NE_GDI_ERR_NOT_FOUND;

    (void)hwnd;

    dc->in_paint = 0;
    if (dc->own_fb) {
        free(dc->own_fb);
        dc->own_fb = NULL;
    }
    memset(dc, 0, sizeof(*dc));
    ctx->dc_count--;
    return NE_GDI_OK;
}

/* =========================================================================
 * Drawing primitives
 * ===================================================================== */

int ne_gdi_text_out(NEGdiContext *ctx, NEGdiHDC hdc,
                    int16_t x, int16_t y,
                    const char *text, int16_t len)
{
    NEGdiDC *dc;
    int16_t i, row, col, n;
    uint8_t color;
    unsigned char ch;
    const uint8_t *glyph;

    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    if (!text)
        return NE_GDI_ERR_NULL;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return NE_GDI_ERR_NOT_FOUND;

    n = len;
    if (n < 0)
        n = (int16_t)strlen(text);

    color = (uint8_t)(dc->text_color & 0xFF);

    for (i = 0; i < n; i++) {
        ch = (unsigned char)text[i];
        if (ch < 32 || ch > 126)
            ch = 32; /* render unknown chars as space */
        glyph = ne_gdi_font8x8[ch - 32];
        for (row = 0; row < 8; row++) {
            for (col = 0; col < 8; col++) {
                if (glyph[row] & (0x80 >> col))
                    gdi_plot(ctx, dc,
                             (int16_t)(x + i * 8 + col),
                             (int16_t)(y + row), color);
            }
        }
    }

    return NE_GDI_OK;
}

int ne_gdi_move_to(NEGdiContext *ctx, NEGdiHDC hdc,
                   int16_t x, int16_t y)
{
    NEGdiDC *dc;

    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return NE_GDI_ERR_NOT_FOUND;

    dc->cur_x = x;
    dc->cur_y = y;
    return NE_GDI_OK;
}

int ne_gdi_line_to(NEGdiContext *ctx, NEGdiHDC hdc,
                   int16_t x, int16_t y)
{
    NEGdiDC *dc;
    uint8_t color;

    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return NE_GDI_ERR_NOT_FOUND;

    color = gdi_pen_color(ctx, dc);
    gdi_draw_line(ctx, dc, dc->cur_x, dc->cur_y, x, y, color);

    dc->cur_x = x;
    dc->cur_y = y;
    return NE_GDI_OK;
}

int ne_gdi_rectangle(NEGdiContext *ctx, NEGdiHDC hdc,
                     int16_t left, int16_t top,
                     int16_t right, int16_t bottom)
{
    NEGdiDC *dc;
    uint8_t pen_c, brush_c;
    int16_t x, y;

    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return NE_GDI_ERR_NOT_FOUND;

    pen_c   = gdi_pen_color(ctx, dc);
    brush_c = gdi_brush_color(ctx, dc);

    /* Fill interior */
    for (y = (int16_t)(top + 1); y < (int16_t)(bottom - 1); y++)
        for (x = (int16_t)(left + 1); x < (int16_t)(right - 1); x++)
            gdi_plot(ctx, dc, x, y, brush_c);

    /* Draw outline */
    gdi_draw_line(ctx, dc, left, top, (int16_t)(right - 1), top, pen_c);
    gdi_draw_line(ctx, dc, (int16_t)(right - 1), top,
                  (int16_t)(right - 1), (int16_t)(bottom - 1), pen_c);
    gdi_draw_line(ctx, dc, (int16_t)(right - 1), (int16_t)(bottom - 1),
                  left, (int16_t)(bottom - 1), pen_c);
    gdi_draw_line(ctx, dc, left, (int16_t)(bottom - 1), left, top, pen_c);

    return NE_GDI_OK;
}

uint32_t ne_gdi_set_pixel(NEGdiContext *ctx, NEGdiHDC hdc,
                          int16_t x, int16_t y, uint32_t color)
{
    NEGdiDC *dc;
    int16_t w, h;
    uint8_t *fb;

    if (!ctx || !ctx->initialized)
        return 0xFFFFFFFF;

    if (hdc == NE_GDI_HDC_INVALID)
        return 0xFFFFFFFF;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return 0xFFFFFFFF;

    fb = gdi_dc_fb(ctx, dc, &w, &h);
    if (!fb || x < 0 || y < 0 || x >= w || y >= h)
        return 0xFFFFFFFF;

    fb[y * w + x] = (uint8_t)(color & 0xFF);
    return color;
}

uint32_t ne_gdi_get_pixel(NEGdiContext *ctx, NEGdiHDC hdc,
                          int16_t x, int16_t y)
{
    NEGdiDC *dc;
    int16_t w, h;
    uint8_t *fb;

    if (!ctx || !ctx->initialized)
        return 0xFFFFFFFF;

    if (hdc == NE_GDI_HDC_INVALID)
        return 0xFFFFFFFF;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return 0xFFFFFFFF;

    fb = gdi_dc_fb(ctx, dc, &w, &h);
    if (!fb || x < 0 || y < 0 || x >= w || y >= h)
        return 0xFFFFFFFF;

    return (uint32_t)fb[y * w + x];
}

int ne_gdi_ellipse(NEGdiContext *ctx, NEGdiHDC hdc,
                   int16_t left, int16_t top,
                   int16_t right, int16_t bottom)
{
    NEGdiDC *dc;
    uint8_t color;
    int cx, cy, rx, ry;
    int x, y;
    long rx2, ry2;
    long px, py, p;

    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return NE_GDI_ERR_NOT_FOUND;

    color = gdi_pen_color(ctx, dc);

    cx = (left + right) / 2;
    cy = (top + bottom) / 2;
    rx = (right - left) / 2;
    ry = (bottom - top) / 2;

    if (rx <= 0 || ry <= 0) {
        gdi_plot(ctx, dc, (int16_t)cx, (int16_t)cy, color);
        return NE_GDI_OK;
    }

    rx2 = (long)rx * rx;
    ry2 = (long)ry * ry;

    /* Region 1 */
    x = 0;
    y = ry;
    px = 0;
    py = 2 * rx2 * y;
    p = ry2 - rx2 * ry + rx2 / 4;

    while (px < py) {
        gdi_plot(ctx, dc, (int16_t)(cx + x), (int16_t)(cy + y), color);
        gdi_plot(ctx, dc, (int16_t)(cx - x), (int16_t)(cy + y), color);
        gdi_plot(ctx, dc, (int16_t)(cx + x), (int16_t)(cy - y), color);
        gdi_plot(ctx, dc, (int16_t)(cx - x), (int16_t)(cy - y), color);
        x++;
        px += 2 * ry2;
        if (p < 0) {
            p += ry2 + px;
        } else {
            y--;
            py -= 2 * rx2;
            p += ry2 + px - py;
        }
    }

    /* Region 2 */
    p = ry2 * (long)(x * x + x) + rx2 * (long)(y - 1) * (y - 1)
        - rx2 * ry2;

    while (y >= 0) {
        gdi_plot(ctx, dc, (int16_t)(cx + x), (int16_t)(cy + y), color);
        gdi_plot(ctx, dc, (int16_t)(cx - x), (int16_t)(cy + y), color);
        gdi_plot(ctx, dc, (int16_t)(cx + x), (int16_t)(cy - y), color);
        gdi_plot(ctx, dc, (int16_t)(cx - x), (int16_t)(cy - y), color);
        y--;
        py -= 2 * rx2;
        if (p > 0) {
            p += rx2 - py;
        } else {
            x++;
            px += 2 * ry2;
            p += rx2 - py + px;
        }
    }

    return NE_GDI_OK;
}

int ne_gdi_polygon(NEGdiContext *ctx, NEGdiHDC hdc,
                   const NEGdiPoint *pts, int16_t count)
{
    NEGdiDC *dc;
    uint8_t color;
    int16_t i;

    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    if (!pts || count < 2)
        return NE_GDI_ERR_NULL;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return NE_GDI_ERR_NOT_FOUND;

    color = gdi_pen_color(ctx, dc);

    for (i = 0; i < count - 1; i++)
        gdi_draw_line(ctx, dc, pts[i].x, pts[i].y,
                      pts[i + 1].x, pts[i + 1].y, color);

    /* Close the polygon */
    gdi_draw_line(ctx, dc, pts[count - 1].x, pts[count - 1].y,
                  pts[0].x, pts[0].y, color);

    return NE_GDI_OK;
}

int ne_gdi_polyline(NEGdiContext *ctx, NEGdiHDC hdc,
                    const NEGdiPoint *pts, int16_t count)
{
    NEGdiDC *dc;
    uint8_t color;
    int16_t i;

    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    if (!pts || count < 2)
        return NE_GDI_ERR_NULL;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return NE_GDI_ERR_NOT_FOUND;

    color = gdi_pen_color(ctx, dc);

    for (i = 0; i < count - 1; i++)
        gdi_draw_line(ctx, dc, pts[i].x, pts[i].y,
                      pts[i + 1].x, pts[i + 1].y, color);

    return NE_GDI_OK;
}

/* =========================================================================
 * GDI object management
 * ===================================================================== */

NEGdiHGDIOBJ ne_gdi_create_pen(NEGdiContext *ctx, int16_t style,
                               int16_t width, COLORREF color)
{
    NEGdiObject *obj;

    if (!ctx || !ctx->initialized)
        return NE_GDI_HGDIOBJ_INVALID;

    obj = gdi_alloc_obj(ctx);
    if (!obj)
        return NE_GDI_HGDIOBJ_INVALID;

    obj->type          = NE_GDI_OBJ_PEN;
    obj->u.pen.style   = style;
    obj->u.pen.width   = width;
    obj->u.pen.color   = color;

    return obj->handle;
}

NEGdiHGDIOBJ ne_gdi_create_brush(NEGdiContext *ctx, int16_t style,
                                 COLORREF color)
{
    NEGdiObject *obj;

    if (!ctx || !ctx->initialized)
        return NE_GDI_HGDIOBJ_INVALID;

    obj = gdi_alloc_obj(ctx);
    if (!obj)
        return NE_GDI_HGDIOBJ_INVALID;

    obj->type            = NE_GDI_OBJ_BRUSH;
    obj->u.brush.style   = style;
    obj->u.brush.color   = color;

    return obj->handle;
}

NEGdiHGDIOBJ ne_gdi_create_font(NEGdiContext *ctx, int16_t height,
                                int16_t width, int16_t weight,
                                const char *face_name)
{
    NEGdiObject *obj;

    if (!ctx || !ctx->initialized)
        return NE_GDI_HGDIOBJ_INVALID;

    obj = gdi_alloc_obj(ctx);
    if (!obj)
        return NE_GDI_HGDIOBJ_INVALID;

    obj->type            = NE_GDI_OBJ_FONT;
    obj->u.font.height   = height;
    obj->u.font.width    = width;
    obj->u.font.weight   = weight;
    obj->u.font.char_set = 0;
    memset(obj->u.font.face_name, 0, NE_GDI_FONT_FACE_MAX);
    if (face_name) {
        strncpy(obj->u.font.face_name, face_name,
                NE_GDI_FONT_FACE_MAX - 1);
    }

    return obj->handle;
}

NEGdiHGDIOBJ ne_gdi_select_object(NEGdiContext *ctx, NEGdiHDC hdc,
                                  NEGdiHGDIOBJ obj_handle)
{
    NEGdiDC *dc;
    NEGdiObject *obj;
    NEGdiHGDIOBJ prev;

    if (!ctx || !ctx->initialized)
        return NE_GDI_HGDIOBJ_INVALID;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_HGDIOBJ_INVALID;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return NE_GDI_HGDIOBJ_INVALID;

    obj = gdi_find_obj(ctx, obj_handle);
    if (!obj)
        return NE_GDI_HGDIOBJ_INVALID;

    switch (obj->type) {
    case NE_GDI_OBJ_PEN:
        prev = dc->sel_pen;
        dc->sel_pen = obj_handle;
        return prev;
    case NE_GDI_OBJ_BRUSH:
        prev = dc->sel_brush;
        dc->sel_brush = obj_handle;
        return prev;
    case NE_GDI_OBJ_FONT:
        prev = dc->sel_font;
        dc->sel_font = obj_handle;
        return prev;
    case NE_GDI_OBJ_BITMAP:
        prev = dc->sel_bitmap;
        dc->sel_bitmap = obj_handle;
        return prev;
    default:
        return NE_GDI_HGDIOBJ_INVALID;
    }
}

int ne_gdi_delete_object(NEGdiContext *ctx, NEGdiHGDIOBJ obj_handle)
{
    NEGdiObject *obj;

    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (obj_handle == NE_GDI_HGDIOBJ_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    obj = gdi_find_obj(ctx, obj_handle);
    if (!obj)
        return NE_GDI_ERR_NOT_FOUND;

    if (obj->type == NE_GDI_OBJ_BITMAP && obj->u.bitmap.bits) {
        free(obj->u.bitmap.bits);
        obj->u.bitmap.bits = NULL;
    }

    memset(obj, 0, sizeof(*obj));
    ctx->obj_count--;
    return NE_GDI_OK;
}

/* =========================================================================
 * Colour and text attributes
 * ===================================================================== */

COLORREF ne_gdi_set_text_color(NEGdiContext *ctx, NEGdiHDC hdc,
                               COLORREF color)
{
    NEGdiDC *dc;
    COLORREF prev;

    if (!ctx || !ctx->initialized)
        return 0;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return 0;

    prev = dc->text_color;
    dc->text_color = color;
    return prev;
}

COLORREF ne_gdi_set_bk_color(NEGdiContext *ctx, NEGdiHDC hdc,
                             COLORREF color)
{
    NEGdiDC *dc;
    COLORREF prev;

    if (!ctx || !ctx->initialized)
        return 0;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return 0;

    prev = dc->bk_color;
    dc->bk_color = color;
    return prev;
}

int ne_gdi_set_bk_mode(NEGdiContext *ctx, NEGdiHDC hdc, int mode)
{
    NEGdiDC *dc;
    int prev;

    if (!ctx || !ctx->initialized)
        return 0;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return 0;

    prev = dc->bk_mode;
    dc->bk_mode = mode;
    return prev;
}

int ne_gdi_get_text_metrics(NEGdiContext *ctx, NEGdiHDC hdc,
                            NEGdiTextMetrics *tm)
{
    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    if (!tm)
        return NE_GDI_ERR_NULL;

    if (!gdi_find_dc(ctx, hdc))
        return NE_GDI_ERR_NOT_FOUND;

    /* Fixed metrics based on 8x8 built-in font */
    tm->height         = 8;
    tm->ascent         = 7;
    tm->descent        = 1;
    tm->avg_char_width = 8;
    tm->max_char_width = 8;

    return NE_GDI_OK;
}

int ne_gdi_get_text_extent(NEGdiContext *ctx, NEGdiHDC hdc,
                           const char *text, int16_t len,
                           int16_t *cx, int16_t *cy)
{
    int16_t n;

    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    if (!text || !cx || !cy)
        return NE_GDI_ERR_NULL;

    if (!gdi_find_dc(ctx, hdc))
        return NE_GDI_ERR_NOT_FOUND;

    n = len;
    if (n < 0)
        n = (int16_t)strlen(text);

    *cx = (int16_t)(n * 8);
    *cy = 8;

    return NE_GDI_OK;
}

/* =========================================================================
 * Bit-block transfer
 * ===================================================================== */

/* Static helper – apply ternary raster operation to a single byte */
static uint8_t gdi_apply_rop(uint8_t dst, uint8_t src, uint8_t pat,
                             uint32_t rop)
{
    switch (rop) {
    case NE_GDI_SRCCOPY:   return src;
    case NE_GDI_SRCPAINT:  return dst | src;
    case NE_GDI_SRCAND:    return dst & src;
    case NE_GDI_SRCINVERT: return dst ^ src;
    case NE_GDI_BLACKNESS: return 0x00;
    case NE_GDI_WHITENESS: return 0xFF;
    case NE_GDI_PATCOPY:   return pat;
    default:               return src;
    }
}

int ne_gdi_bitblt(NEGdiContext *ctx, NEGdiHDC dest_hdc,
                  int16_t dx, int16_t dy, int16_t w, int16_t h,
                  NEGdiHDC src_hdc, int16_t sx, int16_t sy, uint32_t rop)
{
    NEGdiDC *ddc, *sdc;
    uint8_t *dfb, *sfb;
    int16_t dw, dh, sw, sh;
    int16_t ix, iy;
    uint8_t sv, dv, brush_c;

    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (dest_hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    ddc = gdi_find_dc(ctx, dest_hdc);
    if (!ddc)
        return NE_GDI_ERR_NOT_FOUND;

    dfb = gdi_dc_fb(ctx, ddc, &dw, &dh);

    /* Source DC is optional for pattern-only ROPs */
    sdc = NULL;
    sfb = NULL;
    sw  = 0;
    sh  = 0;
    if (src_hdc != NE_GDI_HDC_INVALID) {
        sdc = gdi_find_dc(ctx, src_hdc);
        if (sdc)
            sfb = gdi_dc_fb(ctx, sdc, &sw, &sh);
    }

    brush_c = gdi_brush_color(ctx, ddc);

    for (iy = 0; iy < h; iy++) {
        for (ix = 0; ix < w; ix++) {
            int16_t dst_x = (int16_t)(dx + ix);
            int16_t dst_y = (int16_t)(dy + iy);
            int16_t src_x = (int16_t)(sx + ix);
            int16_t src_y = (int16_t)(sy + iy);

            if (dst_x < 0 || dst_y < 0 || dst_x >= dw || dst_y >= dh)
                continue;

            sv = 0;
            if (sfb && src_x >= 0 && src_y >= 0 &&
                src_x < sw && src_y < sh)
                sv = sfb[src_y * sw + src_x];

            dv = dfb[dst_y * dw + dst_x];
            dfb[dst_y * dw + dst_x] = gdi_apply_rop(dv, sv, brush_c, rop);
        }
    }

    return NE_GDI_OK;
}

int ne_gdi_stretchblt(NEGdiContext *ctx, NEGdiHDC dest_hdc,
                      int16_t dx, int16_t dy, int16_t dw_blt, int16_t dh_blt,
                      NEGdiHDC src_hdc,
                      int16_t sx, int16_t sy, int16_t sw_blt, int16_t sh_blt,
                      uint32_t rop)
{
    NEGdiDC *ddc, *sdc;
    uint8_t *dfb, *sfb;
    int16_t dw, dh, sw, sh;
    int16_t ix, iy;
    uint8_t sv, dv, brush_c;

    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (dest_hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    ddc = gdi_find_dc(ctx, dest_hdc);
    if (!ddc)
        return NE_GDI_ERR_NOT_FOUND;

    dfb = gdi_dc_fb(ctx, ddc, &dw, &dh);

    sdc = NULL;
    sfb = NULL;
    sw  = 0;
    sh  = 0;
    if (src_hdc != NE_GDI_HDC_INVALID) {
        sdc = gdi_find_dc(ctx, src_hdc);
        if (sdc)
            sfb = gdi_dc_fb(ctx, sdc, &sw, &sh);
    }

    brush_c = gdi_brush_color(ctx, ddc);

    if (dw_blt <= 0 || dh_blt <= 0)
        return NE_GDI_OK;

    for (iy = 0; iy < dh_blt; iy++) {
        for (ix = 0; ix < dw_blt; ix++) {
            int16_t dst_x = (int16_t)(dx + ix);
            int16_t dst_y = (int16_t)(dy + iy);
            int16_t src_x, src_y;

            if (dst_x < 0 || dst_y < 0 || dst_x >= dw || dst_y >= dh)
                continue;

            /* Nearest-neighbour sampling */
            src_x = (int16_t)(sx + (sw_blt > 0 ? ix * sw_blt / dw_blt : 0));
            src_y = (int16_t)(sy + (sh_blt > 0 ? iy * sh_blt / dh_blt : 0));

            sv = 0;
            if (sfb && src_x >= 0 && src_y >= 0 &&
                src_x < sw && src_y < sh)
                sv = sfb[src_y * sw + src_x];

            dv = dfb[dst_y * dw + dst_x];
            dfb[dst_y * dw + dst_x] = gdi_apply_rop(dv, sv, brush_c, rop);
        }
    }

    return NE_GDI_OK;
}

int ne_gdi_patblt(NEGdiContext *ctx, NEGdiHDC hdc,
                  int16_t x, int16_t y, int16_t w, int16_t h,
                  uint32_t rop)
{
    NEGdiDC *dc;
    uint8_t *fb;
    int16_t fw, fh;
    int16_t ix, iy;
    uint8_t brush_c, dv;

    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return NE_GDI_ERR_NOT_FOUND;

    fb = gdi_dc_fb(ctx, dc, &fw, &fh);
    brush_c = gdi_brush_color(ctx, dc);

    for (iy = y; iy < y + h; iy++) {
        for (ix = x; ix < x + w; ix++) {
            if (ix < 0 || iy < 0 || ix >= fw || iy >= fh)
                continue;
            dv = fb[iy * fw + ix];
            fb[iy * fw + ix] = gdi_apply_rop(dv, 0, brush_c, rop);
        }
    }

    return NE_GDI_OK;
}

/* =========================================================================
 * Compatible DCs and bitmaps
 * ===================================================================== */

NEGdiHDC ne_gdi_create_compatible_dc(NEGdiContext *ctx, NEGdiHDC hdc)
{
    NEGdiDC *src_dc;
    NEGdiHDC new_hdc;
    NEGdiDC *new_dc;
    int16_t w, h;
    size_t sz;

    if (!ctx || !ctx->initialized)
        return NE_GDI_HDC_INVALID;

    /* Determine source dimensions */
    if (hdc != NE_GDI_HDC_INVALID) {
        src_dc = gdi_find_dc(ctx, hdc);
        if (!src_dc)
            return NE_GDI_HDC_INVALID;
        if (src_dc->own_fb) {
            w = src_dc->own_fb_w;
            h = src_dc->own_fb_h;
        } else {
            w = NE_GDI_FB_WIDTH;
            h = NE_GDI_FB_HEIGHT;
        }
    } else {
        w = NE_GDI_FB_WIDTH;
        h = NE_GDI_FB_HEIGHT;
    }

    new_hdc = ne_gdi_get_dc(ctx, 0);
    if (new_hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_HDC_INVALID;

    new_dc = gdi_find_dc(ctx, new_hdc);
    if (!new_dc)
        return NE_GDI_HDC_INVALID;

    sz = (size_t)w * (size_t)h;
    new_dc->own_fb = (uint8_t *)malloc(sz);
    if (!new_dc->own_fb) {
        ne_gdi_release_dc(ctx, 0, new_hdc);
        return NE_GDI_HDC_INVALID;
    }
    memset(new_dc->own_fb, 0, sz);
    new_dc->own_fb_w = w;
    new_dc->own_fb_h = h;

    return new_hdc;
}

NEGdiHGDIOBJ ne_gdi_create_compatible_bitmap(NEGdiContext *ctx,
                                             NEGdiHDC hdc,
                                             int16_t width, int16_t height)
{
    NEGdiObject *obj;
    size_t sz;

    if (!ctx || !ctx->initialized)
        return NE_GDI_HGDIOBJ_INVALID;

    (void)hdc; /* compatibility info not needed for 8-bit mode */

    if (width <= 0 || height <= 0)
        return NE_GDI_HGDIOBJ_INVALID;

    obj = gdi_alloc_obj(ctx);
    if (!obj)
        return NE_GDI_HGDIOBJ_INVALID;

    sz = (size_t)width * (size_t)height;
    obj->type              = NE_GDI_OBJ_BITMAP;
    obj->u.bitmap.width    = width;
    obj->u.bitmap.height   = height;
    obj->u.bitmap.planes   = 1;
    obj->u.bitmap.bpp      = 8;
    obj->u.bitmap.bits     = (uint8_t *)malloc(sz);
    if (!obj->u.bitmap.bits) {
        obj->type = NE_GDI_OBJ_UNUSED;
        ctx->obj_count--;
        return NE_GDI_HGDIOBJ_INVALID;
    }
    memset(obj->u.bitmap.bits, 0, sz);

    return obj->handle;
}

NEGdiHGDIOBJ ne_gdi_create_bitmap(NEGdiContext *ctx,
                                  int16_t width, int16_t height,
                                  uint8_t planes, uint8_t bpp,
                                  const void *bits)
{
    NEGdiObject *obj;
    size_t sz;

    if (!ctx || !ctx->initialized)
        return NE_GDI_HGDIOBJ_INVALID;

    if (width <= 0 || height <= 0)
        return NE_GDI_HGDIOBJ_INVALID;

    obj = gdi_alloc_obj(ctx);
    if (!obj)
        return NE_GDI_HGDIOBJ_INVALID;

    sz = (size_t)width * (size_t)height;
    obj->type              = NE_GDI_OBJ_BITMAP;
    obj->u.bitmap.width    = width;
    obj->u.bitmap.height   = height;
    obj->u.bitmap.planes   = planes;
    obj->u.bitmap.bpp      = bpp;
    obj->u.bitmap.bits     = (uint8_t *)malloc(sz);
    if (!obj->u.bitmap.bits) {
        obj->type = NE_GDI_OBJ_UNUSED;
        ctx->obj_count--;
        return NE_GDI_HGDIOBJ_INVALID;
    }

    if (bits)
        memcpy(obj->u.bitmap.bits, bits, sz);
    else
        memset(obj->u.bitmap.bits, 0, sz);

    return obj->handle;
}

NEGdiHGDIOBJ ne_gdi_create_dib_bitmap(NEGdiContext *ctx, NEGdiHDC hdc,
                                      int16_t width, int16_t height,
                                      const void *bits)
{
    (void)hdc; /* DDB conversion not needed for 8-bit mode */
    return ne_gdi_create_bitmap(ctx, width, height, 1, 8, bits);
}

/* =========================================================================
 * ne_gdi_strerror
 * ===================================================================== */

const char *ne_gdi_strerror(int err)
{
    switch (err) {
    case NE_GDI_OK:              return "success";
    case NE_GDI_ERR_NULL:        return "NULL pointer argument";
    case NE_GDI_ERR_INIT:        return "GDI context not initialised";
    case NE_GDI_ERR_FULL:        return "DC table at capacity";
    case NE_GDI_ERR_NOT_FOUND:   return "device context not found";
    case NE_GDI_ERR_BAD_HANDLE:  return "invalid device context handle";
    case NE_GDI_ERR_OBJ_FULL:    return "GDI object table at capacity";
    default:                     return "unknown error";
    }
}

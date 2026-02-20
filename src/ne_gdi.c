/*
 * ne_gdi.c - Phase 3 GDI.EXE subsystem implementation
 *
 * Provides device context management, paint session handling, and stub
 * drawing primitives for the Windows 3.1 GDI layer.  Drawing functions
 * validate their arguments and update the current position where
 * applicable but do not produce visible output in this phase.
 *
 * Reference: Microsoft Windows 3.1 SDK – Graphics Device Interface.
 */

#include "ne_gdi.h"

#include <string.h>

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

/* =========================================================================
 * ne_gdi_init / ne_gdi_free
 * ===================================================================== */

int ne_gdi_init(NEGdiContext *ctx)
{
    if (!ctx)
        return NE_GDI_ERR_NULL;

    memset(ctx, 0, sizeof(*ctx));

    ctx->next_hdc    = 1;
    ctx->initialized = 1;
    return NE_GDI_OK;
}

void ne_gdi_free(NEGdiContext *ctx)
{
    if (!ctx)
        return;

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
            ctx->dcs[i].hdc      = ctx->next_hdc++;
            ctx->dcs[i].hwnd     = hwnd;
            ctx->dcs[i].cur_x    = 0;
            ctx->dcs[i].cur_y    = 0;
            ctx->dcs[i].active   = 1;
            ctx->dcs[i].in_paint = 0;
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
            ctx->dcs[i].hdc      = hdc;
            ctx->dcs[i].hwnd     = hwnd;
            ctx->dcs[i].cur_x    = 0;
            ctx->dcs[i].cur_y    = 0;
            ctx->dcs[i].active   = 1;
            ctx->dcs[i].in_paint = 1;
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
    memset(dc, 0, sizeof(*dc));
    ctx->dc_count--;
    return NE_GDI_OK;
}

/* =========================================================================
 * Drawing primitives (stubs)
 * ===================================================================== */

int ne_gdi_text_out(NEGdiContext *ctx, NEGdiHDC hdc,
                    int16_t x, int16_t y,
                    const char *text, int16_t len)
{
    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    if (!text)
        return NE_GDI_ERR_NULL;

    if (!gdi_find_dc(ctx, hdc))
        return NE_GDI_ERR_NOT_FOUND;

    /* Stub: validate only, no visible output */
    (void)x;
    (void)y;
    (void)len;
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

    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    dc = gdi_find_dc(ctx, hdc);
    if (!dc)
        return NE_GDI_ERR_NOT_FOUND;

    /* Stub: update current position to simulate pen movement */
    dc->cur_x = x;
    dc->cur_y = y;
    return NE_GDI_OK;
}

int ne_gdi_rectangle(NEGdiContext *ctx, NEGdiHDC hdc,
                     int16_t left, int16_t top,
                     int16_t right, int16_t bottom)
{
    if (!ctx || !ctx->initialized)
        return NE_GDI_ERR_INIT;

    if (hdc == NE_GDI_HDC_INVALID)
        return NE_GDI_ERR_BAD_HANDLE;

    if (!gdi_find_dc(ctx, hdc))
        return NE_GDI_ERR_NOT_FOUND;

    /* Stub: validate only, no visible output */
    (void)left;
    (void)top;
    (void)right;
    (void)bottom;
    return NE_GDI_OK;
}

uint32_t ne_gdi_set_pixel(NEGdiContext *ctx, NEGdiHDC hdc,
                          int16_t x, int16_t y, uint32_t color)
{
    if (!ctx || !ctx->initialized)
        return 0xFFFFFFFF;

    if (hdc == NE_GDI_HDC_INVALID)
        return 0xFFFFFFFF;

    if (!gdi_find_dc(ctx, hdc))
        return 0xFFFFFFFF;

    /* Stub: validate only, no visible output */
    (void)x;
    (void)y;
    return color;
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
    default:                     return "unknown error";
    }
}

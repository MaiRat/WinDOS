/*
 * ne_driver.c - Phase 4 Device Driver Integration implementation
 *
 * Provides keyboard, timer, display, and mouse driver subsystems for
 * the WinDOS kernel-replacement layer.  On the POSIX host all interrupt
 * hooks are simulated with in-memory state; on the Watcom/DOS 16-bit
 * target the ISR stubs use _dos_setvect() / _dos_getvect() to hook
 * the real interrupt vectors.
 *
 * Reference: IBM PC Technical Reference, Intel 8086 Family Manual,
 *            Microsoft Windows 3.1 SDK – Virtual Key Codes.
 */

#include "ne_driver.h"

#include <stdlib.h>
#include <string.h>

#ifdef __WATCOMC__
#include <dos.h>
#include <i86.h>
#include <conio.h>
#endif

/* =========================================================================
 * Scan code to virtual key code translation table
 *
 * Indexed by the 7-bit make code (0x00–0x7F) from the AT keyboard
 * controller.  The high bit of the raw scan code indicates key release
 * and is stripped before lookup.
 *
 * Only commonly used keys are mapped; unmapped scan codes return 0.
 * ===================================================================== */

static const uint8_t g_default_scancode_map[NE_DRV_SCANCODE_MAX] = {
    /* 0x00 */ 0,
    /* 0x01 */ VK_ESCAPE,
    /* 0x02 */ VK_1,
    /* 0x03 */ VK_2,
    /* 0x04 */ VK_3,
    /* 0x05 */ VK_4,
    /* 0x06 */ VK_5,
    /* 0x07 */ VK_6,
    /* 0x08 */ VK_7,
    /* 0x09 */ VK_8,
    /* 0x0A */ VK_9,
    /* 0x0B */ VK_0,
    /* 0x0C */ VK_OEM_MINUS, /* minus/underscore */
    /* 0x0D */ VK_OEM_PLUS, /* equals/plus */
    /* 0x0E */ VK_BACK,
    /* 0x0F */ VK_TAB,
    /* 0x10 */ VK_Q,
    /* 0x11 */ VK_W,
    /* 0x12 */ VK_E,
    /* 0x13 */ VK_R,
    /* 0x14 */ VK_T,
    /* 0x15 */ VK_Y,
    /* 0x16 */ VK_U,
    /* 0x17 */ VK_I,
    /* 0x18 */ VK_O,
    /* 0x19 */ VK_P,
    /* 0x1A */ VK_OEM_4,   /* left bracket */
    /* 0x1B */ VK_OEM_6,   /* right bracket */
    /* 0x1C */ VK_RETURN,
    /* 0x1D */ VK_CONTROL,
    /* 0x1E */ VK_A,
    /* 0x1F */ VK_S,
    /* 0x20 */ VK_D,
    /* 0x21 */ VK_F,
    /* 0x22 */ VK_G,
    /* 0x23 */ VK_H,
    /* 0x24 */ VK_J,
    /* 0x25 */ VK_K,
    /* 0x26 */ VK_L,
    /* 0x27 */ VK_OEM_1,   /* semicolon */
    /* 0x28 */ VK_OEM_7,   /* apostrophe */
    /* 0x29 */ VK_OEM_3,   /* grave accent */
    /* 0x2A */ VK_SHIFT,
    /* 0x2B */ VK_OEM_5,   /* backslash */
    /* 0x2C */ VK_Z,
    /* 0x2D */ VK_X,
    /* 0x2E */ VK_C,
    /* 0x2F */ VK_V,
    /* 0x30 */ VK_B,
    /* 0x31 */ VK_N,
    /* 0x32 */ VK_M,
    /* 0x33 */ VK_OEM_COMMA, /* comma */
    /* 0x34 */ VK_OEM_PERIOD, /* period */
    /* 0x35 */ VK_OEM_2,   /* forward slash */
    /* 0x36 */ VK_SHIFT,   /* right shift */
    /* 0x37 */ VK_MULTIPLY, /* keypad * */
    /* 0x38 */ VK_MENU,    /* ALT */
    /* 0x39 */ VK_SPACE,
    /* 0x3A */ VK_CAPITAL,  /* caps lock */
    /* 0x3B */ VK_F1,
    /* 0x3C */ VK_F2,
    /* 0x3D */ VK_F3,
    /* 0x3E */ VK_F4,
    /* 0x3F */ VK_F5,
    /* 0x40 */ VK_F6,
    /* 0x41 */ VK_F7,
    /* 0x42 */ VK_F8,
    /* 0x43 */ VK_F9,
    /* 0x44 */ VK_F10,
    /* 0x45 */ VK_NUMLOCK,  /* num lock */
    /* 0x46 */ VK_SCROLL,   /* scroll lock */
    /* 0x47 */ VK_HOME,     /* keypad 7 / home */
    /* 0x48 */ VK_UP,
    /* 0x49 */ VK_PRIOR,    /* keypad 9 / page up */
    /* 0x4A */ VK_SUBTRACT, /* keypad minus */
    /* 0x4B */ VK_LEFT,
    /* 0x4C */ VK_NUMPAD5,  /* keypad 5 */
    /* 0x4D */ VK_RIGHT,
    /* 0x4E */ VK_ADD,      /* keypad plus */
    /* 0x4F */ VK_END,      /* keypad 1 / end */
    /* 0x50 */ VK_DOWN,
    /* 0x51 */ VK_NEXT,     /* keypad 3 / page down */
    /* 0x52 */ VK_INSERT,   /* keypad 0 / insert */
    /* 0x53 */ VK_DELETE,
    /* 0x54 */ 0,
    /* 0x55 */ 0,
    /* 0x56 */ 0,
    /* 0x57 */ VK_F11,
    /* 0x58 */ VK_F12,
    /* 0x59..0x7F: unmapped – zero-filled by static initialisation */
};

/* =========================================================================
 * ne_drv_init / ne_drv_free
 * ===================================================================== */

int ne_drv_init(NEDrvContext *ctx)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;

    memset(ctx, 0, sizeof(*ctx));
    ctx->disp.default_attr  = 0x07; /* light grey on black */
    ctx->disp.video_mode    = NE_DRV_VMODE_TEXT;
    ctx->tmr.next_id        = 1;
    ctx->printer.next_job_id = 1;
    ctx->initialized        = 1;
    return NE_DRV_OK;
}

void ne_drv_free(NEDrvContext *ctx)
{
    if (!ctx)
        return;

    /* Uninstall drivers if still active */
    if (ctx->kbd.installed)
        ne_drv_kbd_uninstall(ctx);
    if (ctx->tmr.installed)
        ne_drv_tmr_uninstall(ctx);
    if (ctx->disp.installed)
        ne_drv_disp_uninstall(ctx);
    if (ctx->mouse.installed)
        ne_drv_mouse_uninstall(ctx);
    if (ctx->printer.installed)
        ne_drv_printer_uninstall(ctx);

    /* Free graphics framebuffer if allocated */
    if (ctx->disp.framebuffer) {
        free(ctx->disp.framebuffer);
        ctx->disp.framebuffer = NULL;
    }

    memset(ctx, 0, sizeof(*ctx));
}

/* =========================================================================
 * Keyboard driver
 * ===================================================================== */

#ifdef __WATCOMC__

static void (__interrupt __far *g_saved_kbd_isr)(void) = NULL;
static NEDrvContext *g_kbd_ctx = NULL;

static void __interrupt __far ne_drv_kbd_isr(void)
{
    uint8_t scancode;

    scancode = inp(0x60); /* read scan code from keyboard controller */

    if (g_kbd_ctx && g_kbd_ctx->kbd.installed) {
        if (g_kbd_ctx->kbd.count < NE_DRV_KEY_EVENT_CAP) {
            uint8_t make = scancode & 0x7Fu;
            uint8_t vk   = g_kbd_ctx->kbd.scancode_to_vk[make];
            NEDrvKeyEvent *slot =
                &g_kbd_ctx->kbd.events[g_kbd_ctx->kbd.tail];

            slot->scan_code = scancode;
            slot->vk        = vk;
            slot->message   = (scancode & 0x80u) ? WM_KEYUP : WM_KEYDOWN;

            g_kbd_ctx->kbd.tail =
                (uint16_t)((g_kbd_ctx->kbd.tail + 1u) % NE_DRV_KEY_EVENT_CAP);
            g_kbd_ctx->kbd.count++;
        }
    }

    /* Acknowledge the interrupt to the PIC. */
    outp(0x20, 0x20);
}

#endif /* __WATCOMC__ */

int ne_drv_kbd_install(NEDrvContext *ctx)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    memcpy(ctx->kbd.scancode_to_vk, g_default_scancode_map,
           sizeof(g_default_scancode_map));

    ctx->kbd.head      = 0;
    ctx->kbd.tail      = 0;
    ctx->kbd.count     = 0;

#ifdef __WATCOMC__
    g_kbd_ctx = ctx;
    g_saved_kbd_isr = _dos_getvect(0x09);
    _dos_setvect(0x09, ne_drv_kbd_isr);
#endif

    ctx->kbd.installed = 1;
    return NE_DRV_OK;
}

int ne_drv_kbd_uninstall(NEDrvContext *ctx)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

#ifdef __WATCOMC__
    if (g_saved_kbd_isr) {
        _dos_setvect(0x09, g_saved_kbd_isr);
        g_saved_kbd_isr = NULL;
    }
    g_kbd_ctx = NULL;
#endif

    ctx->kbd.installed = 0;
    return NE_DRV_OK;
}

int ne_drv_kbd_push_scancode(NEDrvContext *ctx, uint8_t scancode)
{
    uint8_t        make;
    uint8_t        vk;
    NEDrvKeyEvent *slot;

    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    if (ctx->kbd.count >= NE_DRV_KEY_EVENT_CAP)
        return NE_DRV_ERR_FULL;

    make = scancode & 0x7Fu;
    vk   = ctx->kbd.scancode_to_vk[make];

    slot = &ctx->kbd.events[ctx->kbd.tail];
    slot->scan_code = scancode;
    slot->vk        = vk;
    slot->message   = (scancode & 0x80u) ? WM_KEYUP : WM_KEYDOWN;

    ctx->kbd.tail = (uint16_t)((ctx->kbd.tail + 1u) % NE_DRV_KEY_EVENT_CAP);
    ctx->kbd.count++;

    return NE_DRV_OK;
}

int ne_drv_kbd_pop_event(NEDrvContext *ctx, NEDrvKeyEvent *evt)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (!evt)
        return NE_DRV_ERR_NULL;

    if (ctx->kbd.count == 0)
        return NE_DRV_ERR_NOT_FOUND;

    *evt = ctx->kbd.events[ctx->kbd.head];
    ctx->kbd.head = (uint16_t)((ctx->kbd.head + 1u) % NE_DRV_KEY_EVENT_CAP);
    ctx->kbd.count--;

    return NE_DRV_OK;
}

uint16_t ne_drv_kbd_pending(const NEDrvContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;
    return ctx->kbd.count;
}

uint8_t ne_drv_scancode_to_vk(const NEDrvContext *ctx, uint8_t scancode)
{
    uint8_t make;

    if (!ctx || !ctx->initialized)
        return 0;

    make = scancode & 0x7Fu;
    if (make >= NE_DRV_SCANCODE_MAX)
        return 0;

    return ctx->kbd.scancode_to_vk[make];
}

/* =========================================================================
 * Timer driver
 * ===================================================================== */

#ifdef __WATCOMC__

static void (__interrupt __far *g_saved_tmr_isr)(void) = NULL;
static NEDrvContext *g_tmr_ctx = NULL;

/*
 * Hook INT 1Ch (user timer tick) instead of INT 08h so the BIOS
 * time-of-day counter continues to be updated.  INT 1Ch is called by
 * the BIOS INT 08h handler at each PIT tick (~18.2 Hz = ~55 ms).
 */
static void __interrupt __far ne_drv_tmr_isr(void)
{
    if (g_tmr_ctx && g_tmr_ctx->tmr.installed)
        g_tmr_ctx->tmr.tick_count += 55u; /* ~55 ms per PIT tick */
}

#endif /* __WATCOMC__ */

int ne_drv_tmr_install(NEDrvContext *ctx)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    ctx->tmr.tick_count  = 0;
    ctx->tmr.timer_count = 0;
    ctx->tmr.next_id     = 1;
    memset(ctx->tmr.timers, 0, sizeof(ctx->tmr.timers));

#ifdef __WATCOMC__
    g_tmr_ctx = ctx;
    g_saved_tmr_isr = _dos_getvect(0x1C);
    _dos_setvect(0x1C, ne_drv_tmr_isr);
#endif

    ctx->tmr.installed = 1;
    return NE_DRV_OK;
}

int ne_drv_tmr_uninstall(NEDrvContext *ctx)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

#ifdef __WATCOMC__
    if (g_saved_tmr_isr) {
        _dos_setvect(0x1C, g_saved_tmr_isr);
        g_saved_tmr_isr = NULL;
    }
    g_tmr_ctx = NULL;
#endif

    ctx->tmr.installed = 0;
    return NE_DRV_OK;
}

uint32_t ne_drv_get_tick_count(const NEDrvContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;
    return ctx->tmr.tick_count;
}

void ne_drv_tmr_tick(NEDrvContext *ctx, uint32_t ms)
{
    if (!ctx || !ctx->initialized)
        return;
    ctx->tmr.tick_count += ms;
}

uint16_t ne_drv_set_timer(NEDrvContext *ctx, uint16_t hwnd,
                          uint32_t interval_ms)
{
    uint16_t i;

    if (!ctx || !ctx->initialized)
        return 0;
    if (interval_ms == 0)
        return 0;
    if (ctx->tmr.timer_count >= NE_DRV_TIMER_CAP)
        return 0;

    for (i = 0; i < NE_DRV_TIMER_CAP; i++) {
        if (!ctx->tmr.timers[i].active) {
            ctx->tmr.timers[i].id          = ctx->tmr.next_id++;
            ctx->tmr.timers[i].hwnd        = hwnd;
            ctx->tmr.timers[i].interval_ms = interval_ms;
            ctx->tmr.timers[i].next_fire   =
                ctx->tmr.tick_count + interval_ms;
            ctx->tmr.timers[i].active      = 1;
            ctx->tmr.timer_count++;
            return ctx->tmr.timers[i].id;
        }
    }

    return 0;
}

int ne_drv_kill_timer(NEDrvContext *ctx, uint16_t timer_id)
{
    uint16_t i;

    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (timer_id == 0)
        return NE_DRV_ERR_BAD_ID;

    for (i = 0; i < NE_DRV_TIMER_CAP; i++) {
        if (ctx->tmr.timers[i].active &&
            ctx->tmr.timers[i].id == timer_id) {
            memset(&ctx->tmr.timers[i], 0, sizeof(ctx->tmr.timers[i]));
            ctx->tmr.timer_count--;
            return NE_DRV_OK;
        }
    }

    return NE_DRV_ERR_NOT_FOUND;
}

uint16_t ne_drv_tmr_check_expired(NEDrvContext *ctx,
                                  uint16_t *out_ids,
                                  uint16_t *out_hwnds,
                                  uint16_t max_out)
{
    uint16_t i, n = 0;

    if (!ctx || !ctx->initialized || !out_ids || !out_hwnds || max_out == 0)
        return 0;

    for (i = 0; i < NE_DRV_TIMER_CAP && n < max_out; i++) {
        if (ctx->tmr.timers[i].active &&
            ctx->tmr.tick_count >= ctx->tmr.timers[i].next_fire) {
            out_ids[n]   = ctx->tmr.timers[i].id;
            out_hwnds[n] = ctx->tmr.timers[i].hwnd;
            n++;
            /* Advance next_fire for periodic behaviour. */
            ctx->tmr.timers[i].next_fire +=
                ctx->tmr.timers[i].interval_ms;
        }
    }

    return n;
}

/* =========================================================================
 * Display driver (text mode)
 * ===================================================================== */

int ne_drv_disp_install(NEDrvContext *ctx)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    ctx->disp.default_attr = 0x07;
    ctx->disp.cursor_row   = 0;
    ctx->disp.cursor_col   = 0;

    ne_drv_disp_clear(ctx);

    ctx->disp.installed = 1;
    return NE_DRV_OK;
}

int ne_drv_disp_uninstall(NEDrvContext *ctx)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    if (ctx->disp.framebuffer) {
        free(ctx->disp.framebuffer);
        ctx->disp.framebuffer = NULL;
    }
    ctx->disp.fb_width  = 0;
    ctx->disp.fb_height = 0;
    ctx->disp.fb_size   = 0;
    ctx->disp.video_mode = NE_DRV_VMODE_TEXT;
    ctx->disp.installed = 0;
    return NE_DRV_OK;
}

int ne_drv_disp_clear(NEDrvContext *ctx)
{
    uint8_t r, c;

    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    for (r = 0; r < NE_DRV_DISPLAY_ROWS; r++) {
        for (c = 0; c < NE_DRV_DISPLAY_COLS; c++) {
            ctx->disp.buffer[r][c].ch   = ' ';
            ctx->disp.buffer[r][c].attr = ctx->disp.default_attr;
        }
    }

    ctx->disp.cursor_row = 0;
    ctx->disp.cursor_col = 0;

    return NE_DRV_OK;
}

int ne_drv_disp_putchar(NEDrvContext *ctx, uint8_t row, uint8_t col,
                        char ch, uint8_t attr)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    if (row >= NE_DRV_DISPLAY_ROWS || col >= NE_DRV_DISPLAY_COLS)
        return NE_DRV_ERR_BAD_ID;

    ctx->disp.buffer[row][col].ch   = ch;
    ctx->disp.buffer[row][col].attr = attr;

    return NE_DRV_OK;
}

int ne_drv_disp_getchar(const NEDrvContext *ctx, uint8_t row, uint8_t col,
                        char *ch, uint8_t *attr)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (!ch || !attr)
        return NE_DRV_ERR_NULL;

    if (row >= NE_DRV_DISPLAY_ROWS || col >= NE_DRV_DISPLAY_COLS)
        return NE_DRV_ERR_BAD_ID;

    *ch   = ctx->disp.buffer[row][col].ch;
    *attr = ctx->disp.buffer[row][col].attr;

    return NE_DRV_OK;
}

int ne_drv_disp_set_cursor(NEDrvContext *ctx, uint8_t row, uint8_t col)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    if (row >= NE_DRV_DISPLAY_ROWS || col >= NE_DRV_DISPLAY_COLS)
        return NE_DRV_ERR_BAD_ID;

    ctx->disp.cursor_row = row;
    ctx->disp.cursor_col = col;

    return NE_DRV_OK;
}

int ne_drv_disp_get_cursor(const NEDrvContext *ctx,
                           uint8_t *row, uint8_t *col)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (!row || !col)
        return NE_DRV_ERR_NULL;

    *row = ctx->disp.cursor_row;
    *col = ctx->disp.cursor_col;

    return NE_DRV_OK;
}

int ne_drv_disp_write_string(NEDrvContext *ctx, const char *text,
                             uint8_t attr)
{
    uint8_t r, c;

    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (!text)
        return NE_DRV_ERR_NULL;

    r = ctx->disp.cursor_row;
    c = ctx->disp.cursor_col;

    while (*text) {
        if (r >= NE_DRV_DISPLAY_ROWS)
            break;

        ctx->disp.buffer[r][c].ch   = *text;
        ctx->disp.buffer[r][c].attr = attr;

        c++;
        if (c >= NE_DRV_DISPLAY_COLS) {
            c = 0;
            r++;
        }

        text++;
    }

    ctx->disp.cursor_row = r;
    ctx->disp.cursor_col = c;

    return NE_DRV_OK;
}

/* =========================================================================
 * Display driver – graphics mode
 * ===================================================================== */

int ne_drv_disp_set_mode(NEDrvContext *ctx, int mode)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    /* Free existing framebuffer */
    if (ctx->disp.framebuffer) {
        free(ctx->disp.framebuffer);
        ctx->disp.framebuffer = NULL;
    }
    ctx->disp.fb_width  = 0;
    ctx->disp.fb_height = 0;
    ctx->disp.fb_size   = 0;

    switch (mode) {
    case NE_DRV_VMODE_TEXT:
        ctx->disp.video_mode = NE_DRV_VMODE_TEXT;
        break;
    case NE_DRV_VMODE_640x480:
        ctx->disp.fb_width  = NE_DRV_GFX_WIDTH_HI;
        ctx->disp.fb_height = NE_DRV_GFX_HEIGHT_HI;
        ctx->disp.fb_size   = NE_DRV_GFX_FB_SIZE_HI;
        ctx->disp.framebuffer = (uint8_t *)calloc(1, ctx->disp.fb_size);
        if (!ctx->disp.framebuffer)
            return NE_DRV_ERR_FULL;
        ctx->disp.video_mode = NE_DRV_VMODE_640x480;
        break;
    case NE_DRV_VMODE_320x200:
        ctx->disp.fb_width  = NE_DRV_GFX_WIDTH_LO;
        ctx->disp.fb_height = NE_DRV_GFX_HEIGHT_LO;
        ctx->disp.fb_size   = NE_DRV_GFX_FB_SIZE_LO;
        ctx->disp.framebuffer = (uint8_t *)calloc(1, ctx->disp.fb_size);
        if (!ctx->disp.framebuffer)
            return NE_DRV_ERR_FULL;
        ctx->disp.video_mode = NE_DRV_VMODE_320x200;
        break;
    default:
        return NE_DRV_ERR_BAD_ID;
    }

    return NE_DRV_OK;
}

int ne_drv_disp_get_mode(const NEDrvContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return NE_DRV_VMODE_TEXT;
    return ctx->disp.video_mode;
}

int ne_drv_disp_gfx_set_pixel(NEDrvContext *ctx, uint16_t x, uint16_t y,
                               uint8_t color)
{
    uint32_t offset;

    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (!ctx->disp.framebuffer)
        return NE_DRV_ERR_INIT;
    if (x >= ctx->disp.fb_width || y >= ctx->disp.fb_height)
        return NE_DRV_ERR_BAD_ID;

    offset = (uint32_t)y * ctx->disp.fb_width + x;
    ctx->disp.framebuffer[offset] = color;
    return NE_DRV_OK;
}

int ne_drv_disp_gfx_get_pixel(const NEDrvContext *ctx, uint16_t x,
                               uint16_t y, uint8_t *color)
{
    uint32_t offset;

    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (!color)
        return NE_DRV_ERR_NULL;
    if (!ctx->disp.framebuffer)
        return NE_DRV_ERR_INIT;
    if (x >= ctx->disp.fb_width || y >= ctx->disp.fb_height)
        return NE_DRV_ERR_BAD_ID;

    offset = (uint32_t)y * ctx->disp.fb_width + x;
    *color = ctx->disp.framebuffer[offset];
    return NE_DRV_OK;
}

int ne_drv_disp_gfx_fill_rect(NEDrvContext *ctx, uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h, uint8_t color)
{
    uint16_t row, col;
    uint16_t x_end, y_end;

    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (!ctx->disp.framebuffer)
        return NE_DRV_ERR_INIT;

    x_end = (x + w > ctx->disp.fb_width)  ? ctx->disp.fb_width  : (x + w);
    y_end = (y + h > ctx->disp.fb_height) ? ctx->disp.fb_height : (y + h);

    for (row = y; row < y_end; row++) {
        for (col = x; col < x_end; col++) {
            ctx->disp.framebuffer[(uint32_t)row * ctx->disp.fb_width + col] =
                color;
        }
    }
    return NE_DRV_OK;
}

int ne_drv_disp_gfx_clear(NEDrvContext *ctx, uint8_t color)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (!ctx->disp.framebuffer)
        return NE_DRV_ERR_INIT;

    memset(ctx->disp.framebuffer, color, ctx->disp.fb_size);
    return NE_DRV_OK;
}

/* =========================================================================
 * Mouse driver
 * ===================================================================== */

#ifdef __WATCOMC__

static NEDrvContext *g_mouse_ctx = NULL;

/*
 * DOS INT 33h mouse driver interaction.
 *
 * INT 33h / AX=0000h – Reset/detect mouse driver.
 * INT 33h / AX=0003h – Get button status and position.
 * INT 33h / AX=000Ch – Set user-defined event handler.
 *
 * For this phase we poll the mouse state rather than installing a
 * callback to keep the implementation simple.
 */

#endif /* __WATCOMC__ */

int ne_drv_mouse_install(NEDrvContext *ctx)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    ctx->mouse.head    = 0;
    ctx->mouse.tail    = 0;
    ctx->mouse.count   = 0;
    ctx->mouse.x       = 0;
    ctx->mouse.y       = 0;
    ctx->mouse.buttons = 0;

#ifdef __WATCOMC__
    g_mouse_ctx = ctx;
    /* INT 33h / AX=0000h – initialise/detect mouse driver */
    {
        union REGS r;
        r.x.ax = 0x0000;
        int86(0x33, &r, &r);
        /* AX=0xFFFF means mouse driver detected */
    }
#endif

    ctx->mouse.installed = 1;
    return NE_DRV_OK;
}

int ne_drv_mouse_uninstall(NEDrvContext *ctx)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

#ifdef __WATCOMC__
    g_mouse_ctx = NULL;
#endif

    ctx->mouse.installed = 0;
    return NE_DRV_OK;
}

int ne_drv_mouse_push_event(NEDrvContext *ctx, uint16_t message,
                            int16_t x, int16_t y, uint16_t buttons)
{
    NEDrvMouseEvent *slot;

    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    if (ctx->mouse.count >= NE_DRV_MOUSE_EVENT_CAP)
        return NE_DRV_ERR_FULL;

    slot = &ctx->mouse.events[ctx->mouse.tail];
    slot->message = message;
    slot->x       = x;
    slot->y       = y;
    slot->buttons = buttons;

    ctx->mouse.tail = (uint16_t)((ctx->mouse.tail + 1u) %
                                  NE_DRV_MOUSE_EVENT_CAP);
    ctx->mouse.count++;

    /* Update current position / button state */
    ctx->mouse.x       = x;
    ctx->mouse.y       = y;
    ctx->mouse.buttons = buttons;

    return NE_DRV_OK;
}

int ne_drv_mouse_pop_event(NEDrvContext *ctx, NEDrvMouseEvent *evt)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (!evt)
        return NE_DRV_ERR_NULL;

    if (ctx->mouse.count == 0)
        return NE_DRV_ERR_NOT_FOUND;

    *evt = ctx->mouse.events[ctx->mouse.head];
    ctx->mouse.head = (uint16_t)((ctx->mouse.head + 1u) %
                                  NE_DRV_MOUSE_EVENT_CAP);
    ctx->mouse.count--;

    return NE_DRV_OK;
}

uint16_t ne_drv_mouse_pending(const NEDrvContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;
    return ctx->mouse.count;
}

int ne_drv_mouse_get_position(const NEDrvContext *ctx,
                              int16_t *x, int16_t *y, uint16_t *buttons)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (!x || !y || !buttons)
        return NE_DRV_ERR_NULL;

    *x       = ctx->mouse.x;
    *y       = ctx->mouse.y;
    *buttons = ctx->mouse.buttons;

    return NE_DRV_OK;
}

int ne_drv_mouse_show_cursor(NEDrvContext *ctx, int show)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    ctx->mouse.cursor_visible = show ? 1 : 0;
    return NE_DRV_OK;
}

int ne_drv_mouse_set_cursor_bitmap(NEDrvContext *ctx,
    const uint8_t bitmap[NE_DRV_CURSOR_SIZE][NE_DRV_CURSOR_SIZE],
    int16_t hot_x, int16_t hot_y)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (!bitmap)
        return NE_DRV_ERR_NULL;

    memcpy(ctx->mouse.cursor_bitmap, bitmap,
           NE_DRV_CURSOR_SIZE * NE_DRV_CURSOR_SIZE);
    ctx->mouse.hot_x = hot_x;
    ctx->mouse.hot_y = hot_y;
    return NE_DRV_OK;
}

int ne_drv_mouse_get_cursor_visible(const NEDrvContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;
    return ctx->mouse.cursor_visible;
}

int ne_drv_mouse_coalesce_moves(NEDrvContext *ctx)
{
    NEDrvMouseEvent temp[NE_DRV_MOUSE_EVENT_CAP];
    uint16_t i, src, dst;

    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    if (ctx->mouse.count <= 1)
        return NE_DRV_OK;

    /* Copy events from the circular queue into a flat array */
    src = ctx->mouse.head;
    for (i = 0; i < ctx->mouse.count; i++) {
        temp[i] = ctx->mouse.events[src];
        src = (uint16_t)((src + 1u) % NE_DRV_MOUSE_EVENT_CAP);
    }

    /* Compact: skip WM_MOUSEMOVE if the next event is also WM_MOUSEMOVE */
    dst = 0;
    for (i = 0; i < ctx->mouse.count; i++) {
        if (temp[i].message == WM_MOUSEMOVE &&
            i + 1 < ctx->mouse.count &&
            temp[i + 1].message == WM_MOUSEMOVE) {
            continue; /* skip this intermediate move */
        }
        ctx->mouse.events[dst] = temp[i];
        dst++;
    }

    ctx->mouse.head  = 0;
    ctx->mouse.tail  = dst;
    ctx->mouse.count = dst;
    return NE_DRV_OK;
}

/* =========================================================================
 * Printer driver
 * ===================================================================== */

int ne_drv_printer_install(NEDrvContext *ctx)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    memset(ctx->printer.jobs, 0, sizeof(ctx->printer.jobs));
    ctx->printer.job_count   = 0;
    ctx->printer.next_job_id = 1;
    ctx->printer.installed   = 1;
    return NE_DRV_OK;
}

int ne_drv_printer_uninstall(NEDrvContext *ctx)
{
    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    memset(ctx->printer.jobs, 0, sizeof(ctx->printer.jobs));
    ctx->printer.job_count = 0;
    ctx->printer.installed = 0;
    return NE_DRV_OK;
}

uint16_t ne_drv_printer_start_doc(NEDrvContext *ctx, const char *doc_name)
{
    uint16_t i;

    if (!ctx || !ctx->initialized || !ctx->printer.installed)
        return 0;
    if (ctx->printer.job_count >= NE_DRV_PRINTER_CAP)
        return 0;

    for (i = 0; i < NE_DRV_PRINTER_CAP; i++) {
        if (ctx->printer.jobs[i].job_id == 0) {
            ctx->printer.jobs[i].job_id     = ctx->printer.next_job_id++;
            ctx->printer.jobs[i].page_count = 0;
            ctx->printer.jobs[i].in_page    = 0;
            if (doc_name) {
                strncpy(ctx->printer.jobs[i].doc_name, doc_name, 63);
                ctx->printer.jobs[i].doc_name[63] = '\0';
            } else {
                ctx->printer.jobs[i].doc_name[0] = '\0';
            }
            ctx->printer.job_count++;
            return ctx->printer.jobs[i].job_id;
        }
    }
    return 0;
}

int ne_drv_printer_end_doc(NEDrvContext *ctx, uint16_t job_id)
{
    uint16_t i;

    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (job_id == 0)
        return NE_DRV_ERR_BAD_ID;

    for (i = 0; i < NE_DRV_PRINTER_CAP; i++) {
        if (ctx->printer.jobs[i].job_id == job_id) {
            memset(&ctx->printer.jobs[i], 0, sizeof(ctx->printer.jobs[i]));
            ctx->printer.job_count--;
            return NE_DRV_OK;
        }
    }
    return NE_DRV_ERR_NOT_FOUND;
}

int ne_drv_printer_start_page(NEDrvContext *ctx, uint16_t job_id)
{
    uint16_t i;

    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (job_id == 0)
        return NE_DRV_ERR_BAD_ID;

    for (i = 0; i < NE_DRV_PRINTER_CAP; i++) {
        if (ctx->printer.jobs[i].job_id == job_id) {
            ctx->printer.jobs[i].in_page = 1;
            ctx->printer.jobs[i].page_count++;
            return NE_DRV_OK;
        }
    }
    return NE_DRV_ERR_NOT_FOUND;
}

int ne_drv_printer_end_page(NEDrvContext *ctx, uint16_t job_id)
{
    uint16_t i;

    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;
    if (job_id == 0)
        return NE_DRV_ERR_BAD_ID;

    for (i = 0; i < NE_DRV_PRINTER_CAP; i++) {
        if (ctx->printer.jobs[i].job_id == job_id) {
            ctx->printer.jobs[i].in_page = 0;
            return NE_DRV_OK;
        }
    }
    return NE_DRV_ERR_NOT_FOUND;
}

int ne_drv_printer_abort_doc(NEDrvContext *ctx, uint16_t job_id)
{
    return ne_drv_printer_end_doc(ctx, job_id);
}

uint16_t ne_drv_printer_get_job_count(const NEDrvContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;
    return ctx->printer.job_count;
}

/* =========================================================================
 * Driver coexistence verification
 * ===================================================================== */

int ne_drv_verify_coexistence(NEDrvContext *ctx)
{
    int drivers_installed = 0;

    if (!ctx)
        return NE_DRV_ERR_NULL;
    if (!ctx->initialized)
        return NE_DRV_ERR_INIT;

    /* Count installed drivers and verify each is functional. */
    if (ctx->kbd.installed)
        drivers_installed++;
    if (ctx->tmr.installed)
        drivers_installed++;
    if (ctx->disp.installed)
        drivers_installed++;
    if (ctx->mouse.installed)
        drivers_installed++;

    /* At least the keyboard, timer, and display must be installed. */
    if (!ctx->kbd.installed || !ctx->tmr.installed || !ctx->disp.installed)
        return NE_DRV_ERR_INIT;

    /*
     * Verify keyboard driver: push a test scan code, verify it can be
     * popped, and that the virtual key translation is correct.
     */
    {
        NEDrvKeyEvent evt;
        uint16_t saved_count = ctx->kbd.count;

        if (ne_drv_kbd_push_scancode(ctx, 0x1E) != NE_DRV_OK) /* 'A' key */
            return NE_DRV_ERR_INIT;

        if (ne_drv_kbd_pop_event(ctx, &evt) != NE_DRV_OK)
            return NE_DRV_ERR_INIT;

        if (evt.vk != VK_A || evt.message != WM_KEYDOWN)
            return NE_DRV_ERR_INIT;

        /* Ensure the queue depth returned to the pre-test state. */
        if (ctx->kbd.count != saved_count)
            return NE_DRV_ERR_INIT;
    }

    /*
     * Verify timer driver: advance tick count and check basic
     * monotonicity.
     */
    {
        uint32_t t0 = ne_drv_get_tick_count(ctx);
        ne_drv_tmr_tick(ctx, 10);
        if (ne_drv_get_tick_count(ctx) != t0 + 10u)
            return NE_DRV_ERR_INIT;
    }

    /*
     * Verify display driver: write a character and read it back.
     */
    {
        char     ch;
        uint8_t  attr;

        if (ne_drv_disp_putchar(ctx, 0, 0, 'X', 0x0F) != NE_DRV_OK)
            return NE_DRV_ERR_INIT;
        if (ne_drv_disp_getchar(ctx, 0, 0, &ch, &attr) != NE_DRV_OK)
            return NE_DRV_ERR_INIT;
        if (ch != 'X' || attr != 0x0F)
            return NE_DRV_ERR_INIT;
    }

    (void)drivers_installed;

    return NE_DRV_OK;
}

/* =========================================================================
 * ne_drv_strerror
 * ===================================================================== */

const char *ne_drv_strerror(int err)
{
    switch (err) {
    case NE_DRV_OK:             return "success";
    case NE_DRV_ERR_NULL:       return "NULL pointer argument";
    case NE_DRV_ERR_INIT:       return "driver context not initialised";
    case NE_DRV_ERR_FULL:       return "event queue or table at capacity";
    case NE_DRV_ERR_NOT_FOUND:  return "timer or resource not found";
    case NE_DRV_ERR_BAD_ID:     return "invalid identifier";
    default:                    return "unknown error";
    }
}

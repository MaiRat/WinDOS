/*
 * ne_driver.h - Phase 4 Device Driver Integration
 *
 * Implements Phase 4 of the WinDOS kernel-replacement roadmap:
 *   - Keyboard driver: hook INT 09h, translate scan codes to Windows
 *     virtual key codes, feed events into the message queue.
 *   - Timer driver: hook INT 08h / INT 1Ch, provide GetTickCount,
 *     SetTimer, KillTimer.
 *   - Display driver: interface with VGA/EGA text or graphics mode for
 *     basic output.
 *   - Mouse driver: hook INT 33h for mouse position and button state,
 *     feed events into the message queue.
 *   - Driver coexistence verification with the cooperative scheduler.
 *
 * Host-side (POSIX/GCC) implementation: interrupt hooks are simulated
 * with in-memory state.  Functions that would install ISRs on DOS are
 * no-ops on the host; the driver state tables are fully exercised so
 * that logic correctness can be verified in CI.
 *
 * Watcom/DOS 16-bit real-mode target: interrupt hooks use
 * _dos_setvect() / _dos_getvect() to install __interrupt stubs.  Timer
 * ticks arrive from the 8254 PIT via INT 08h; keyboard scan codes
 * arrive from the 8042 controller via INT 09h.
 *
 * Reference: IBM PC Technical Reference, Intel 8086 Family Manual,
 *            Microsoft Windows 3.1 SDK – Virtual Key Codes.
 */

#ifndef NE_DRIVER_H
#define NE_DRIVER_H

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_DRV_OK               0
#define NE_DRV_ERR_NULL        -1   /* NULL pointer argument               */
#define NE_DRV_ERR_INIT        -2   /* context not initialised             */
#define NE_DRV_ERR_FULL        -3   /* table at capacity                   */
#define NE_DRV_ERR_NOT_FOUND   -4   /* timer or resource not found         */
#define NE_DRV_ERR_BAD_ID      -5   /* zero or otherwise invalid ID        */

/* -------------------------------------------------------------------------
 * Windows 3.1 virtual key codes (subset used by the keyboard driver)
 * ---------------------------------------------------------------------- */
#define VK_BACK        0x08u
#define VK_TAB         0x09u
#define VK_RETURN      0x0Du
#define VK_SHIFT       0x10u
#define VK_CONTROL     0x11u
#define VK_MENU        0x12u   /* ALT key                                 */
#define VK_ESCAPE      0x1Bu
#define VK_SPACE       0x20u
#define VK_LEFT        0x25u
#define VK_UP          0x26u
#define VK_RIGHT       0x27u
#define VK_DOWN        0x28u
#define VK_DELETE      0x2Eu
#define VK_0           0x30u
#define VK_1           0x31u
#define VK_2           0x32u
#define VK_3           0x33u
#define VK_4           0x34u
#define VK_5           0x35u
#define VK_6           0x36u
#define VK_7           0x37u
#define VK_8           0x38u
#define VK_9           0x39u
#define VK_A           0x41u
#define VK_B           0x42u
#define VK_C           0x43u
#define VK_D           0x44u
#define VK_E           0x45u
#define VK_F           0x46u
#define VK_G           0x47u
#define VK_H           0x48u
#define VK_I           0x49u
#define VK_J           0x4Au
#define VK_K           0x4Bu
#define VK_L           0x4Cu
#define VK_M           0x4Du
#define VK_N           0x4Eu
#define VK_O           0x4Fu
#define VK_P           0x50u
#define VK_Q           0x51u
#define VK_R           0x52u
#define VK_S           0x53u
#define VK_T           0x54u
#define VK_U           0x55u
#define VK_V           0x56u
#define VK_W           0x57u
#define VK_X           0x58u
#define VK_Y           0x59u
#define VK_Z           0x5Au
#define VK_F1          0x70u
#define VK_F2          0x71u
#define VK_F3          0x72u
#define VK_F4          0x73u
#define VK_F5          0x74u
#define VK_F6          0x75u
#define VK_F7          0x76u
#define VK_F8          0x77u
#define VK_F9          0x78u
#define VK_F10         0x79u
#define VK_F11         0x7Au
#define VK_F12         0x7Bu
#define VK_CAPITAL     0x14u   /* Caps Lock                                */
#define VK_NUMLOCK     0x90u   /* Num Lock                                 */
#define VK_SCROLL      0x91u   /* Scroll Lock                              */
#define VK_PRIOR       0x21u   /* Page Up                                  */
#define VK_NEXT        0x22u   /* Page Down                                */
#define VK_END         0x23u
#define VK_HOME        0x24u
#define VK_INSERT      0x2Du
#define VK_NUMPAD5     0x65u
#define VK_MULTIPLY    0x6Au   /* Keypad *                                 */
#define VK_ADD         0x6Bu   /* Keypad +                                 */
#define VK_SUBTRACT    0x6Du   /* Keypad -                                 */
#define VK_OEM_1       0xBAu   /* ;:                                       */
#define VK_OEM_PLUS    0xBBu   /* =+                                       */
#define VK_OEM_COMMA   0xBCu   /* ,<                                       */
#define VK_OEM_MINUS   0xBDu   /* -_                                       */
#define VK_OEM_PERIOD  0xBEu   /* .>                                       */
#define VK_OEM_2       0xBFu   /* /?                                       */
#define VK_OEM_3       0xC0u   /* `~                                       */
#define VK_OEM_4       0xDBu   /* [{                                       */
#define VK_OEM_5       0xDCu   /* \|                                       */
#define VK_OEM_6       0xDDu   /* ]}                                       */
#define VK_OEM_7       0xDEu   /* '"                                       */

/* -------------------------------------------------------------------------
 * Keyboard message identifiers
 * ---------------------------------------------------------------------- */
#define WM_KEYDOWN     0x0100u
#define WM_KEYUP       0x0101u

/* -------------------------------------------------------------------------
 * Mouse message identifiers
 * ---------------------------------------------------------------------- */
#define WM_MOUSEMOVE     0x0200u
#define WM_LBUTTONDOWN   0x0201u
#define WM_LBUTTONUP     0x0202u
#define WM_RBUTTONDOWN   0x0204u
#define WM_RBUTTONUP     0x0205u

/* -------------------------------------------------------------------------
 * Timer message identifier
 * ---------------------------------------------------------------------- */
#define WM_TIMER       0x0113u

/* -------------------------------------------------------------------------
 * Configuration constants
 * ---------------------------------------------------------------------- */
#define NE_DRV_KEY_EVENT_CAP     64u  /* max queued keyboard events        */
#define NE_DRV_MOUSE_EVENT_CAP   64u  /* max queued mouse events           */
#define NE_DRV_TIMER_CAP         16u  /* max simultaneous timers           */
#define NE_DRV_DISPLAY_COLS      80u  /* text-mode column count            */
#define NE_DRV_DISPLAY_ROWS      25u  /* text-mode row count               */
#define NE_DRV_SCANCODE_MAX    0x80u  /* number of scan code entries       */

/* -------------------------------------------------------------------------
 * Graphics mode constants
 * ---------------------------------------------------------------------- */
#define NE_DRV_VMODE_TEXT      0     /* 80x25 text mode (existing)        */
#define NE_DRV_VMODE_640x480   1     /* VGA 640x480x16 colours            */
#define NE_DRV_VMODE_320x200   2     /* VGA 320x200x256 colours           */

#define NE_DRV_GFX_WIDTH_HI   640
#define NE_DRV_GFX_HEIGHT_HI  480
#define NE_DRV_GFX_WIDTH_LO   320
#define NE_DRV_GFX_HEIGHT_LO  200

#define NE_DRV_GFX_FB_SIZE_HI (NE_DRV_GFX_WIDTH_HI * NE_DRV_GFX_HEIGHT_HI)
#define NE_DRV_GFX_FB_SIZE_LO (NE_DRV_GFX_WIDTH_LO * NE_DRV_GFX_HEIGHT_LO)

/* -------------------------------------------------------------------------
 * Printer driver constants
 * ---------------------------------------------------------------------- */
#define NE_DRV_PRINTER_CAP     4u    /* max simultaneous print jobs       */

/* -------------------------------------------------------------------------
 * Mouse cursor constants
 * ---------------------------------------------------------------------- */
#define NE_DRV_CURSOR_SIZE    16u    /* cursor bitmap width/height        */

/* -------------------------------------------------------------------------
 * Keyboard event descriptor
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t  scan_code;    /* raw scan code from INT 09h                    */
    uint8_t  vk;           /* translated Windows virtual key code           */
    uint16_t message;      /* WM_KEYDOWN or WM_KEYUP                       */
} NEDrvKeyEvent;

/* -------------------------------------------------------------------------
 * Keyboard driver state
 * ---------------------------------------------------------------------- */
typedef struct {
    NEDrvKeyEvent events[NE_DRV_KEY_EVENT_CAP];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    uint8_t  scancode_to_vk[NE_DRV_SCANCODE_MAX];
    int      installed;    /* non-zero if INT 09h hook is active            */
} NEDrvKeyboard;

/* -------------------------------------------------------------------------
 * Timer entry descriptor
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t id;           /* non-zero timer identifier                     */
    uint16_t hwnd;         /* target window handle                          */
    uint32_t interval_ms;  /* timer interval in milliseconds                */
    uint32_t next_fire;    /* tick count at next fire                       */
    int      active;       /* non-zero if this timer slot is in use         */
} NEDrvTimerEntry;

/* -------------------------------------------------------------------------
 * Timer driver state
 * ---------------------------------------------------------------------- */
typedef struct {
    NEDrvTimerEntry timers[NE_DRV_TIMER_CAP];
    uint16_t timer_count;
    uint16_t next_id;      /* next timer ID to assign                      */
    uint32_t tick_count;   /* monotonic tick counter (ms)                   */
    int      installed;    /* non-zero if INT 08h/1Ch hook is active        */
} NEDrvTimer;

/* -------------------------------------------------------------------------
 * Display text cell
 * ---------------------------------------------------------------------- */
typedef struct {
    char    ch;            /* character value                               */
    uint8_t attr;          /* attribute byte (foreground/background colour) */
} NEDrvDisplayCell;

/* -------------------------------------------------------------------------
 * Display driver state (text mode)
 * ---------------------------------------------------------------------- */
typedef struct {
    NEDrvDisplayCell buffer[NE_DRV_DISPLAY_ROWS][NE_DRV_DISPLAY_COLS];
    uint8_t  cursor_row;
    uint8_t  cursor_col;
    uint8_t  default_attr; /* default text attribute (light grey on black) */
    int      installed;    /* non-zero if display driver is active          */
    int      video_mode;   /* NE_DRV_VMODE_TEXT / 640x480 / 320x200        */
    uint8_t *framebuffer;  /* graphics-mode framebuffer (heap-allocated)    */
    uint16_t fb_width;     /* current framebuffer width                     */
    uint16_t fb_height;    /* current framebuffer height                    */
    uint32_t fb_size;      /* total framebuffer byte count                  */
} NEDrvDisplay;

/* -------------------------------------------------------------------------
 * Mouse event descriptor
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t message;      /* WM_MOUSEMOVE / WM_LBUTTONDOWN / etc.        */
    int16_t  x;            /* mouse X position                             */
    int16_t  y;            /* mouse Y position                             */
    uint16_t buttons;      /* button state bitmask                         */
} NEDrvMouseEvent;

/* -------------------------------------------------------------------------
 * Mouse driver state
 * ---------------------------------------------------------------------- */
typedef struct {
    NEDrvMouseEvent events[NE_DRV_MOUSE_EVENT_CAP];
    uint16_t head;
    uint16_t tail;
    uint16_t count;
    int16_t  x;            /* current X position                           */
    int16_t  y;            /* current Y position                           */
    uint16_t buttons;      /* current button state                         */
    int      installed;    /* non-zero if INT 33h hook is active            */
    uint8_t  cursor_bitmap[NE_DRV_CURSOR_SIZE][NE_DRV_CURSOR_SIZE];
    int      cursor_visible;  /* non-zero if cursor should be rendered     */
    int16_t  hot_x, hot_y;    /* hotspot offset within cursor bitmap       */
} NEDrvMouse;

/* -------------------------------------------------------------------------
 * Printer job descriptor
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t job_id;       /* non-zero when active                         */
    uint16_t page_count;   /* pages started in this job                    */
    int      in_page;      /* non-zero if between StartPage/EndPage        */
    char     doc_name[64]; /* document name                                */
} NEDrvPrintJob;

/* -------------------------------------------------------------------------
 * Printer driver state
 * ---------------------------------------------------------------------- */
typedef struct {
    NEDrvPrintJob jobs[NE_DRV_PRINTER_CAP];
    uint16_t job_count;
    uint16_t next_job_id;
    int      installed;
} NEDrvPrinter;

/* -------------------------------------------------------------------------
 * Driver context
 *
 * Owns all driver subsystems.  Initialise with ne_drv_init();
 * release with ne_drv_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    NEDrvKeyboard  kbd;
    NEDrvTimer     tmr;
    NEDrvDisplay   disp;
    NEDrvMouse     mouse;
    NEDrvPrinter   printer;
    int            initialized;
} NEDrvContext;

/* =========================================================================
 * Public API – initialisation and teardown
 * ===================================================================== */

int  ne_drv_init(NEDrvContext *ctx);
void ne_drv_free(NEDrvContext *ctx);

/* =========================================================================
 * Public API – keyboard driver
 * ===================================================================== */

/*
 * ne_drv_kbd_install - install the keyboard ISR (INT 09h on DOS).
 * On the host this initialises the scan code translation table.
 */
int ne_drv_kbd_install(NEDrvContext *ctx);

/*
 * ne_drv_kbd_uninstall - remove the keyboard ISR and restore the
 * original interrupt vector.
 */
int ne_drv_kbd_uninstall(NEDrvContext *ctx);

/*
 * ne_drv_kbd_push_scancode - simulate a keyboard interrupt by
 * pushing a scan code into the keyboard event queue.
 *
 * Bit 7 set indicates a key release; clear indicates a key press.
 * The scan code is translated to a Windows virtual key code using
 * the internal translation table.
 *
 * Returns NE_DRV_OK or NE_DRV_ERR_FULL if the event queue is at
 * capacity.
 */
int ne_drv_kbd_push_scancode(NEDrvContext *ctx, uint8_t scancode);

/*
 * ne_drv_kbd_pop_event - dequeue the next keyboard event.
 *
 * Copies the event into *evt.  Returns NE_DRV_OK on success,
 * NE_DRV_ERR_NOT_FOUND if the queue is empty.
 */
int ne_drv_kbd_pop_event(NEDrvContext *ctx, NEDrvKeyEvent *evt);

/*
 * ne_drv_kbd_pending - return the number of queued keyboard events.
 */
uint16_t ne_drv_kbd_pending(const NEDrvContext *ctx);

/*
 * ne_drv_scancode_to_vk - translate a scan code to a virtual key code.
 *
 * Returns the VK code, or 0 if the scan code is unmapped.
 */
uint8_t ne_drv_scancode_to_vk(const NEDrvContext *ctx, uint8_t scancode);

/* =========================================================================
 * Public API – timer driver
 * ===================================================================== */

/*
 * ne_drv_tmr_install - install the timer ISR (INT 08h / INT 1Ch on DOS).
 * On the host this zeroes the tick counter.
 */
int ne_drv_tmr_install(NEDrvContext *ctx);

/*
 * ne_drv_tmr_uninstall - remove the timer ISR.
 */
int ne_drv_tmr_uninstall(NEDrvContext *ctx);

/*
 * ne_drv_get_tick_count - return the monotonic tick counter (ms).
 */
uint32_t ne_drv_get_tick_count(const NEDrvContext *ctx);

/*
 * ne_drv_tmr_tick - advance the tick counter by 'ms' milliseconds.
 *
 * On the DOS target this is called from the INT 08h/1Ch handler at
 * ~55 ms intervals.  On the host, tests call this directly to
 * simulate elapsed time.
 */
void ne_drv_tmr_tick(NEDrvContext *ctx, uint32_t ms);

/*
 * ne_drv_set_timer - create a periodic timer.
 *
 * 'hwnd'        : target window for WM_TIMER messages.
 * 'interval_ms' : timer period in milliseconds (must be > 0).
 *
 * Returns a non-zero timer ID on success or 0 on failure.
 */
uint16_t ne_drv_set_timer(NEDrvContext *ctx, uint16_t hwnd,
                          uint32_t interval_ms);

/*
 * ne_drv_kill_timer - remove a timer by ID.
 *
 * Returns NE_DRV_OK or NE_DRV_ERR_NOT_FOUND.
 */
int ne_drv_kill_timer(NEDrvContext *ctx, uint16_t timer_id);

/*
 * ne_drv_tmr_check_expired - check for expired timers and report
 * their IDs.
 *
 * Scans all active timers; any whose next_fire <= current tick count
 * are considered expired.  For each expired timer, its next_fire is
 * advanced by interval_ms (periodic behaviour).
 *
 * 'out_ids'    : caller-supplied array to receive expired timer IDs.
 * 'out_hwnds'  : caller-supplied array to receive target window handles.
 * 'max_out'    : capacity of both output arrays.
 *
 * Returns the number of expired timers written into the output arrays.
 */
uint16_t ne_drv_tmr_check_expired(NEDrvContext *ctx,
                                  uint16_t *out_ids,
                                  uint16_t *out_hwnds,
                                  uint16_t max_out);

/* =========================================================================
 * Public API – display driver (text mode)
 * ===================================================================== */

/*
 * ne_drv_disp_install - initialise the display driver.
 *
 * On DOS this would set VGA text mode 03h; on the host it clears the
 * internal text buffer.
 */
int ne_drv_disp_install(NEDrvContext *ctx);

/*
 * ne_drv_disp_uninstall - shut down the display driver.
 */
int ne_drv_disp_uninstall(NEDrvContext *ctx);

/*
 * ne_drv_disp_clear - clear the display buffer with spaces and the
 * default attribute.
 */
int ne_drv_disp_clear(NEDrvContext *ctx);

/*
 * ne_drv_disp_putchar - write a character at (row, col) with 'attr'.
 *
 * Returns NE_DRV_OK or NE_DRV_ERR_BAD_ID if row/col are out of range.
 */
int ne_drv_disp_putchar(NEDrvContext *ctx, uint8_t row, uint8_t col,
                        char ch, uint8_t attr);

/*
 * ne_drv_disp_getchar - read the character and attribute at (row, col).
 *
 * Sets *ch and *attr to the stored values.
 * Returns NE_DRV_OK or NE_DRV_ERR_BAD_ID if row/col are out of range.
 */
int ne_drv_disp_getchar(const NEDrvContext *ctx, uint8_t row, uint8_t col,
                        char *ch, uint8_t *attr);

/*
 * ne_drv_disp_set_cursor - move the text cursor to (row, col).
 *
 * Returns NE_DRV_OK or NE_DRV_ERR_BAD_ID if out of range.
 */
int ne_drv_disp_set_cursor(NEDrvContext *ctx, uint8_t row, uint8_t col);

/*
 * ne_drv_disp_get_cursor - read the current cursor position.
 */
int ne_drv_disp_get_cursor(const NEDrvContext *ctx,
                           uint8_t *row, uint8_t *col);

/*
 * ne_drv_disp_write_string - write a NUL-terminated string starting
 * at the current cursor position.  Advances the cursor.
 *
 * Returns NE_DRV_OK on success.
 */
int ne_drv_disp_write_string(NEDrvContext *ctx, const char *text,
                             uint8_t attr);

/*
 * ne_drv_disp_set_mode - switch video mode.
 *
 * mode: NE_DRV_VMODE_TEXT, NE_DRV_VMODE_640x480, NE_DRV_VMODE_320x200.
 * Allocates the graphics framebuffer for non-text modes.
 */
int ne_drv_disp_set_mode(NEDrvContext *ctx, int mode);

/*
 * ne_drv_disp_get_mode - return the current video mode.
 */
int ne_drv_disp_get_mode(const NEDrvContext *ctx);

/*
 * ne_drv_disp_gfx_set_pixel - set a pixel in the graphics framebuffer.
 */
int ne_drv_disp_gfx_set_pixel(NEDrvContext *ctx, uint16_t x, uint16_t y,
                               uint8_t color);

/*
 * ne_drv_disp_gfx_get_pixel - read a pixel from the graphics framebuffer.
 */
int ne_drv_disp_gfx_get_pixel(const NEDrvContext *ctx, uint16_t x,
                               uint16_t y, uint8_t *color);

/*
 * ne_drv_disp_gfx_fill_rect - fill a rectangle in the graphics framebuffer.
 */
int ne_drv_disp_gfx_fill_rect(NEDrvContext *ctx, uint16_t x, uint16_t y,
                               uint16_t w, uint16_t h, uint8_t color);

/*
 * ne_drv_disp_gfx_clear - clear the entire graphics framebuffer.
 */
int ne_drv_disp_gfx_clear(NEDrvContext *ctx, uint8_t color);

/* =========================================================================
 * Public API – mouse driver
 * ===================================================================== */

/*
 * ne_drv_mouse_install - install the mouse driver (INT 33h on DOS).
 * On the host this zeroes the mouse position and button state.
 */
int ne_drv_mouse_install(NEDrvContext *ctx);

/*
 * ne_drv_mouse_uninstall - remove the mouse driver.
 */
int ne_drv_mouse_uninstall(NEDrvContext *ctx);

/*
 * ne_drv_mouse_push_event - simulate a mouse event.
 *
 * 'message' : WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, etc.
 * 'x', 'y'  : mouse position.
 * 'buttons' : button state bitmask.
 *
 * Returns NE_DRV_OK or NE_DRV_ERR_FULL.
 */
int ne_drv_mouse_push_event(NEDrvContext *ctx, uint16_t message,
                            int16_t x, int16_t y, uint16_t buttons);

/*
 * ne_drv_mouse_pop_event - dequeue the next mouse event.
 *
 * Copies the event into *evt.  Returns NE_DRV_OK on success,
 * NE_DRV_ERR_NOT_FOUND if the queue is empty.
 */
int ne_drv_mouse_pop_event(NEDrvContext *ctx, NEDrvMouseEvent *evt);

/*
 * ne_drv_mouse_pending - return the number of queued mouse events.
 */
uint16_t ne_drv_mouse_pending(const NEDrvContext *ctx);

/*
 * ne_drv_mouse_get_position - read the current mouse position and
 * button state.
 */
int ne_drv_mouse_get_position(const NEDrvContext *ctx,
                              int16_t *x, int16_t *y, uint16_t *buttons);

/*
 * ne_drv_mouse_show_cursor - show or hide the mouse cursor.
 */
int ne_drv_mouse_show_cursor(NEDrvContext *ctx, int show);

/*
 * ne_drv_mouse_set_cursor_bitmap - set the cursor shape and hotspot.
 */
int ne_drv_mouse_set_cursor_bitmap(NEDrvContext *ctx,
    const uint8_t bitmap[NE_DRV_CURSOR_SIZE][NE_DRV_CURSOR_SIZE],
    int16_t hot_x, int16_t hot_y);

/*
 * ne_drv_mouse_get_cursor_visible - return cursor visibility state.
 */
int ne_drv_mouse_get_cursor_visible(const NEDrvContext *ctx);

/*
 * ne_drv_mouse_coalesce_moves - coalesce consecutive WM_MOUSEMOVE
 * events in the queue, keeping only the last of each run.
 */
int ne_drv_mouse_coalesce_moves(NEDrvContext *ctx);

/* =========================================================================
 * Public API – printer driver
 * ===================================================================== */

/*
 * ne_drv_printer_install - initialise the printer driver subsystem.
 */
int ne_drv_printer_install(NEDrvContext *ctx);

/*
 * ne_drv_printer_uninstall - shut down the printer driver.
 */
int ne_drv_printer_uninstall(NEDrvContext *ctx);

/*
 * ne_drv_printer_start_doc - begin a new print job.
 * Returns non-zero job ID on success, 0 on failure.
 */
uint16_t ne_drv_printer_start_doc(NEDrvContext *ctx, const char *doc_name);

/*
 * ne_drv_printer_end_doc - finish a print job normally.
 */
int ne_drv_printer_end_doc(NEDrvContext *ctx, uint16_t job_id);

/*
 * ne_drv_printer_start_page - begin a new page within a job.
 */
int ne_drv_printer_start_page(NEDrvContext *ctx, uint16_t job_id);

/*
 * ne_drv_printer_end_page - finish the current page within a job.
 */
int ne_drv_printer_end_page(NEDrvContext *ctx, uint16_t job_id);

/*
 * ne_drv_printer_abort_doc - cancel a print job.
 */
int ne_drv_printer_abort_doc(NEDrvContext *ctx, uint16_t job_id);

/*
 * ne_drv_printer_get_job_count - return the number of active print jobs.
 */
uint16_t ne_drv_printer_get_job_count(const NEDrvContext *ctx);

/* =========================================================================
 * Public API – driver coexistence verification
 * ===================================================================== */

/*
 * ne_drv_verify_coexistence - verify that all installed drivers can
 * coexist and service events without conflict.
 *
 * Returns NE_DRV_OK if all installed drivers are functional, or a
 * negative NE_DRV_ERR_* code if a problem is detected.
 */
int ne_drv_verify_coexistence(NEDrvContext *ctx);

/* =========================================================================
 * Public API – error reporting
 * ===================================================================== */

const char *ne_drv_strerror(int err);

#endif /* NE_DRIVER_H */

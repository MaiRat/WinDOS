/*
 * ne_gdi.h - GDI.EXE subsystem: device contexts, painting, and drawing
 *
 * Implements Phase 3 of the WinDOS kernel-replacement roadmap:
 *   - Device context (DC) allocation and release (GetDC / ReleaseDC).
 *   - Paint session management (BeginPaint / EndPaint).
 *   - Basic drawing primitives: text output, lines, rectangles, pixels.
 *
 * The GDI subsystem sits alongside the USER subsystem and provides the
 * graphical output layer used by Windows 3.1 applications.  Device context
 * handles (HDC) are 1-based uint16_t indices into the DC table; 0
 * (NE_GDI_HDC_INVALID) is the null sentinel, matching the NULL-handle
 * convention of the Windows 3.1 API.
 *
 * Drawing primitives are implemented as stubs for Phase 3; they validate
 * their arguments and update the current position where applicable but do
 * not produce visible output.  A rendering back-end can be connected in a
 * later phase.
 *
 * Reference: Microsoft Windows 3.1 SDK – Graphics Device Interface.
 */

#ifndef NE_GDI_H
#define NE_GDI_H

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_GDI_OK               0
#define NE_GDI_ERR_NULL        -1   /* NULL pointer argument               */
#define NE_GDI_ERR_INIT        -2   /* context not initialised             */
#define NE_GDI_ERR_FULL        -3   /* DC table at capacity                */
#define NE_GDI_ERR_NOT_FOUND   -4   /* DC not found                        */
#define NE_GDI_ERR_BAD_HANDLE  -5   /* zero or otherwise invalid handle    */

/* -------------------------------------------------------------------------
 * Configuration constants
 * ---------------------------------------------------------------------- */
#define NE_GDI_DC_CAP            32u /* max simultaneous device contexts    */
#define NE_GDI_PAINTSTRUCT_MAX   32u /* reserved size for paint struct      */

/* -------------------------------------------------------------------------
 * Device context handle type
 *
 * A non-zero uint16_t value that identifies an active device context.
 * NE_GDI_HDC_INVALID (0) is the null sentinel.
 * ---------------------------------------------------------------------- */
typedef uint16_t NEGdiHDC;

#define NE_GDI_HDC_INVALID ((NEGdiHDC)0)

/* -------------------------------------------------------------------------
 * Point structure
 *
 * Mirrors the Windows 3.1 POINT structure.  Coordinates are signed 16-bit
 * values to match the Win16 coordinate space.
 * ---------------------------------------------------------------------- */
typedef struct {
    int16_t x;                   /* horizontal coordinate                   */
    int16_t y;                   /* vertical coordinate                     */
} NEGdiPoint;

/* -------------------------------------------------------------------------
 * Rectangle structure
 *
 * Mirrors the Windows 3.1 RECT structure.  The rectangle is defined by
 * its upper-left (left, top) and lower-right (right, bottom) corners.
 * ---------------------------------------------------------------------- */
typedef struct {
    int16_t left;                /* left edge                               */
    int16_t top;                 /* top edge                                */
    int16_t right;               /* right edge                              */
    int16_t bottom;              /* bottom edge                             */
} NEGdiRect;

/* -------------------------------------------------------------------------
 * Paint structure
 *
 * Populated by ne_gdi_begin_paint() and consumed by ne_gdi_end_paint().
 * Mirrors the Windows 3.1 PAINTSTRUCT layout.
 * ---------------------------------------------------------------------- */
typedef struct {
    NEGdiHDC hdc;                /* device context for the paint session    */
    int      erase_bg;           /* non-zero if background must be erased  */
    NEGdiRect paint_rect;        /* update rectangle                        */
} NEGdiPaintStruct;

/* -------------------------------------------------------------------------
 * Device context descriptor
 *
 * One entry per allocated DC.  The 'active' flag is non-zero for entries
 * that represent an allocated device context; zero marks a free slot.
 * ---------------------------------------------------------------------- */
typedef struct {
    NEGdiHDC hdc;                /* unique 1-based handle                   */
    uint16_t hwnd;               /* owning window handle (0 = screen DC)   */
    int16_t  cur_x;              /* current position X                      */
    int16_t  cur_y;              /* current position Y                      */
    int      active;             /* non-zero if slot is in use              */
    int      in_paint;           /* non-zero if inside BeginPaint/EndPaint  */
} NEGdiDC;

/* -------------------------------------------------------------------------
 * GDI subsystem context
 *
 * Owns the device context table.  Initialise with ne_gdi_init(); release
 * with ne_gdi_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    NEGdiDC  dcs[NE_GDI_DC_CAP]; /* device context table                   */
    uint16_t dc_count;           /* number of active device contexts        */
    NEGdiHDC next_hdc;           /* next handle value to assign (starts 1)  */
    int      initialized;        /* non-zero after successful init          */
} NEGdiContext;

/* =========================================================================
 * Public API – initialisation and teardown
 * ===================================================================== */

/*
 * ne_gdi_init - initialise the GDI subsystem context *ctx.
 *
 * Zeroes all internal tables and sets the initial handle counter.
 * Must be called before any other ne_gdi_* function on *ctx.
 * Returns NE_GDI_OK on success or NE_GDI_ERR_NULL.
 */
int ne_gdi_init(NEGdiContext *ctx);

/*
 * ne_gdi_free - release all resources held by *ctx.
 *
 * Resets the context to its uninitialised state.  Safe to call on a zeroed
 * or partially-initialised context and on NULL.
 */
void ne_gdi_free(NEGdiContext *ctx);

/* =========================================================================
 * Public API – device context management
 * ===================================================================== */

/*
 * ne_gdi_get_dc - allocate a device context for the window 'hwnd'.
 *
 * Allocates a free slot in the DC table and binds it to the given window.
 * The caller must release the DC with ne_gdi_release_dc() when finished.
 *
 * Returns a non-zero NEGdiHDC on success or NE_GDI_HDC_INVALID on
 * failure (table full, NULL context, or context not initialised).
 */
NEGdiHDC ne_gdi_get_dc(NEGdiContext *ctx, uint16_t hwnd);

/*
 * ne_gdi_release_dc - release a device context previously obtained with
 * ne_gdi_get_dc().
 *
 * Marks the DC slot as free and decrements the active DC count.
 *
 * Returns NE_GDI_OK on success, NE_GDI_ERR_BAD_HANDLE if hdc is
 * NE_GDI_HDC_INVALID, or NE_GDI_ERR_NOT_FOUND if the handle is not
 * in the DC table.
 */
int ne_gdi_release_dc(NEGdiContext *ctx, uint16_t hwnd, NEGdiHDC hdc);

/* =========================================================================
 * Public API – paint session management
 * ===================================================================== */

/*
 * ne_gdi_begin_paint - begin a paint session for the window 'hwnd'.
 *
 * Allocates a device context, populates the paint structure *ps, and
 * marks the DC as being inside a paint session.  Must be paired with a
 * subsequent call to ne_gdi_end_paint().
 *
 * Returns a non-zero NEGdiHDC on success or NE_GDI_HDC_INVALID on
 * failure.
 */
NEGdiHDC ne_gdi_begin_paint(NEGdiContext *ctx, uint16_t hwnd,
                            NEGdiPaintStruct *ps);

/*
 * ne_gdi_end_paint - end a paint session started by ne_gdi_begin_paint().
 *
 * Releases the device context associated with the paint structure and
 * clears the in_paint flag.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_end_paint(NEGdiContext *ctx, uint16_t hwnd,
                     const NEGdiPaintStruct *ps);

/* =========================================================================
 * Public API – drawing primitives (stubs)
 * ===================================================================== */

/*
 * ne_gdi_text_out - output a string at the specified position.
 *
 * Stub implementation for Phase 3; validates arguments but does not
 * produce visible output.  'len' is the number of characters to draw;
 * if negative, 'text' is assumed to be NUL-terminated.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_text_out(NEGdiContext *ctx, NEGdiHDC hdc,
                    int16_t x, int16_t y,
                    const char *text, int16_t len);

/*
 * ne_gdi_move_to - move the current position to (x, y).
 *
 * Updates the DC's cur_x and cur_y fields.  Does not draw anything.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_move_to(NEGdiContext *ctx, NEGdiHDC hdc,
                   int16_t x, int16_t y);

/*
 * ne_gdi_line_to - draw a line from the current position to (x, y).
 *
 * Stub implementation for Phase 3; updates the current position but does
 * not produce visible output.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_line_to(NEGdiContext *ctx, NEGdiHDC hdc,
                   int16_t x, int16_t y);

/*
 * ne_gdi_rectangle - draw a rectangle with the given corners.
 *
 * Stub implementation for Phase 3; validates arguments but does not
 * produce visible output.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_rectangle(NEGdiContext *ctx, NEGdiHDC hdc,
                     int16_t left, int16_t top,
                     int16_t right, int16_t bottom);

/*
 * ne_gdi_set_pixel - set the pixel at (x, y) to the specified colour.
 *
 * Stub implementation for Phase 3; validates arguments but does not
 * produce visible output.
 *
 * Returns the colour value that was set (same as 'color' on success) or
 * 0xFFFFFFFF on failure, matching the Windows 3.1 SetPixel convention.
 */
uint32_t ne_gdi_set_pixel(NEGdiContext *ctx, NEGdiHDC hdc,
                          int16_t x, int16_t y, uint32_t color);

/* =========================================================================
 * Public API – error reporting
 * ===================================================================== */

/*
 * ne_gdi_strerror - return a static string describing error code 'err'.
 */
const char *ne_gdi_strerror(int err);

#endif /* NE_GDI_H */

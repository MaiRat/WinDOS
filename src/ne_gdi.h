/*
 * ne_gdi.h - GDI.EXE subsystem: device contexts, painting, and drawing
 *
 * Implements Phases 3 and E of the WinDOS kernel-replacement roadmap:
 *   - Device context (DC) allocation and release (GetDC / ReleaseDC).
 *   - Paint session management (BeginPaint / EndPaint).
 *   - Drawing primitives with VGA framebuffer rendering.
 *   - GDI object management (pens, brushes, fonts, bitmaps).
 *   - Bit-block transfer operations (BitBlt, StretchBlt, PatBlt).
 *   - Text metrics and colour attribute management.
 *
 * The GDI subsystem sits alongside the USER subsystem and provides the
 * graphical output layer used by Windows 3.1 applications.  Device context
 * handles (HDC) are 1-based uint16_t indices into the DC table; 0
 * (NE_GDI_HDC_INVALID) is the null sentinel, matching the NULL-handle
 * convention of the Windows 3.1 API.
 *
 * Phase E adds a memory-backed VGA framebuffer (640x480, 8-bit indexed),
 * real rendering for all drawing primitives, GDI object creation and
 * selection, and bitmap transfer operations.
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
#define NE_GDI_ERR_OBJ_FULL   -6   /* GDI object table at capacity        */

/* -------------------------------------------------------------------------
 * Configuration constants
 * ---------------------------------------------------------------------- */
#define NE_GDI_DC_CAP            32u /* max simultaneous device contexts    */
#define NE_GDI_PAINTSTRUCT_MAX   32u /* reserved size for paint struct      */
#define NE_GDI_OBJ_CAP          64u  /* max GDI objects                     */

/* -------------------------------------------------------------------------
 * Framebuffer constants (VGA mode 640x480, 8-bit indexed colour)
 * ---------------------------------------------------------------------- */
#define NE_GDI_FB_WIDTH         640
#define NE_GDI_FB_HEIGHT        480

/* -------------------------------------------------------------------------
 * COLORREF type and RGB macro
 *
 * COLORREF is a 32-bit value encoding (0x00BBGGRR).  The RGB macro
 * constructs a COLORREF from red, green, and blue components.
 * ---------------------------------------------------------------------- */
typedef uint32_t COLORREF;

#define RGB(r,g,b) ((COLORREF)(((uint8_t)(r)) | \
                    ((uint16_t)((uint8_t)(g)) << 8) | \
                    ((uint32_t)((uint8_t)(b)) << 16)))

/* -------------------------------------------------------------------------
 * Background mode constants
 * ---------------------------------------------------------------------- */
#define NE_GDI_TRANSPARENT      1
#define NE_GDI_OPAQUE           2

/* -------------------------------------------------------------------------
 * Ternary raster operation constants
 * ---------------------------------------------------------------------- */
#define NE_GDI_SRCCOPY      0x00CC0020u
#define NE_GDI_SRCPAINT     0x00EE0086u
#define NE_GDI_SRCAND       0x008800C6u
#define NE_GDI_SRCINVERT    0x00660046u
#define NE_GDI_BLACKNESS    0x00000042u
#define NE_GDI_WHITENESS    0x00FF0062u
#define NE_GDI_PATCOPY      0x00F00021u

/* -------------------------------------------------------------------------
 * GDI object types
 * ---------------------------------------------------------------------- */
#define NE_GDI_OBJ_UNUSED   0
#define NE_GDI_OBJ_PEN      1
#define NE_GDI_OBJ_BRUSH    2
#define NE_GDI_OBJ_FONT     3
#define NE_GDI_OBJ_BITMAP   4

/* -------------------------------------------------------------------------
 * GDI object handle type
 * ---------------------------------------------------------------------- */
typedef uint16_t NEGdiHGDIOBJ;

#define NE_GDI_HGDIOBJ_INVALID ((NEGdiHGDIOBJ)0)

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
 * GDI object structures
 * ---------------------------------------------------------------------- */

typedef struct {
    int16_t  style;              /* pen style (PS_SOLID, etc.)              */
    int16_t  width;              /* pen width in pixels                     */
    COLORREF color;              /* pen colour                              */
} NEGdiPen;

typedef struct {
    int16_t  style;              /* brush style                             */
    COLORREF color;              /* brush colour                            */
} NEGdiBrush;

#define NE_GDI_FONT_FACE_MAX 32

typedef struct {
    int16_t  height;             /* character cell height                   */
    int16_t  width;              /* average character width                 */
    int16_t  weight;             /* font weight (400 = normal, 700 = bold)  */
    uint8_t  char_set;           /* character set identifier                */
    char     face_name[NE_GDI_FONT_FACE_MAX]; /* typeface name             */
} NEGdiFont;

typedef struct {
    int16_t  width;              /* bitmap width in pixels                  */
    int16_t  height;             /* bitmap height in pixels                 */
    uint8_t  planes;             /* number of colour planes                 */
    uint8_t  bpp;                /* bits per pixel                          */
    uint8_t *bits;               /* pixel data (heap-allocated)             */
} NEGdiBitmap;

typedef struct {
    int            type;         /* NE_GDI_OBJ_* type tag                   */
    NEGdiHGDIOBJ   handle;       /* 1-based handle value                    */
    union {
        NEGdiPen    pen;
        NEGdiBrush  brush;
        NEGdiFont   font;
        NEGdiBitmap bitmap;
    } u;
} NEGdiObject;

/* -------------------------------------------------------------------------
 * Text metrics structure
 *
 * Mirrors the commonly-used fields of the Windows 3.1 TEXTMETRIC struct.
 * ---------------------------------------------------------------------- */
typedef struct {
    int16_t height;              /* character cell height                   */
    int16_t ascent;              /* units above baseline                    */
    int16_t descent;             /* units below baseline                    */
    int16_t avg_char_width;      /* average character width                 */
    int16_t max_char_width;      /* maximum character width                 */
} NEGdiTextMetrics;

/* -------------------------------------------------------------------------
 * Device context descriptor
 *
 * One entry per allocated DC.  The 'active' flag is non-zero for entries
 * that represent an allocated device context; zero marks a free slot.
 * ---------------------------------------------------------------------- */
typedef struct {
    NEGdiHDC     hdc;            /* unique 1-based handle                   */
    uint16_t     hwnd;           /* owning window handle (0 = screen DC)   */
    int16_t      cur_x;          /* current position X                      */
    int16_t      cur_y;          /* current position Y                      */
    int          active;         /* non-zero if slot is in use              */
    int          in_paint;       /* non-zero if inside BeginPaint/EndPaint  */
    NEGdiHGDIOBJ sel_pen;        /* currently selected pen object           */
    NEGdiHGDIOBJ sel_brush;      /* currently selected brush object         */
    NEGdiHGDIOBJ sel_font;       /* currently selected font object          */
    NEGdiHGDIOBJ sel_bitmap;     /* currently selected bitmap object        */
    COLORREF     text_color;     /* current text colour                     */
    COLORREF     bk_color;       /* current background colour               */
    int          bk_mode;        /* NE_GDI_TRANSPARENT or NE_GDI_OPAQUE    */
    uint8_t     *own_fb;         /* own framebuffer (compatible DCs)        */
    int16_t      own_fb_w;       /* own framebuffer width                   */
    int16_t      own_fb_h;       /* own framebuffer height                  */
} NEGdiDC;

/* -------------------------------------------------------------------------
 * GDI subsystem context
 *
 * Owns the device context table, the GDI object table, and the screen
 * framebuffer.  Initialise with ne_gdi_init(); release with ne_gdi_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    NEGdiDC      dcs[NE_GDI_DC_CAP];       /* device context table         */
    uint16_t     dc_count;                  /* number of active DCs         */
    NEGdiHDC     next_hdc;                  /* next handle value (starts 1) */
    int          initialized;               /* non-zero after init          */
    NEGdiObject  objs[NE_GDI_OBJ_CAP];     /* GDI object table             */
    uint16_t     obj_count;                 /* number of active objects     */
    NEGdiHGDIOBJ next_obj;                  /* next object handle value     */
    uint8_t     *framebuffer;               /* screen framebuffer (heap)    */
} NEGdiContext;

/* =========================================================================
 * Public API – initialisation and teardown
 * ===================================================================== */

/*
 * ne_gdi_init - initialise the GDI subsystem context *ctx.
 *
 * Zeroes all internal tables, sets the initial handle counter, and
 * allocates the screen framebuffer.
 * Must be called before any other ne_gdi_* function on *ctx.
 * Returns NE_GDI_OK on success or NE_GDI_ERR_NULL.
 */
int ne_gdi_init(NEGdiContext *ctx);

/*
 * ne_gdi_free - release all resources held by *ctx.
 *
 * Frees the screen framebuffer, all bitmap pixel data, and resets the
 * context to its uninitialised state.  Safe to call on a zeroed or
 * partially-initialised context and on NULL.
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
 * Public API – drawing primitives
 * ===================================================================== */

/*
 * ne_gdi_text_out - output a string at the specified position.
 *
 * Renders characters using the built-in 8x8 bitmap font to the DC's
 * framebuffer at (x, y) using the DC's current text colour.  'len' is
 * the number of characters to draw; if negative, 'text' is assumed to
 * be NUL-terminated.
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
 * Uses Bresenham's line algorithm to render pixels to the framebuffer
 * with the DC's selected pen colour.  Updates the current position.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_line_to(NEGdiContext *ctx, NEGdiHDC hdc,
                   int16_t x, int16_t y);

/*
 * ne_gdi_rectangle - draw a rectangle with the given corners.
 *
 * Outlines the rectangle using the selected pen and fills the interior
 * with the selected brush colour.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_rectangle(NEGdiContext *ctx, NEGdiHDC hdc,
                     int16_t left, int16_t top,
                     int16_t right, int16_t bottom);

/*
 * ne_gdi_set_pixel - set the pixel at (x, y) to the specified colour.
 *
 * Writes to the DC's framebuffer with bounds checking.
 *
 * Returns the colour value that was set (same as 'color' on success) or
 * 0xFFFFFFFF on failure, matching the Windows 3.1 SetPixel convention.
 */
uint32_t ne_gdi_set_pixel(NEGdiContext *ctx, NEGdiHDC hdc,
                          int16_t x, int16_t y, uint32_t color);

/*
 * ne_gdi_get_pixel - get the pixel colour at (x, y).
 *
 * Reads from the DC's framebuffer with bounds checking.
 *
 * Returns the pixel colour or 0xFFFFFFFF on failure.
 */
uint32_t ne_gdi_get_pixel(NEGdiContext *ctx, NEGdiHDC hdc,
                          int16_t x, int16_t y);

/*
 * ne_gdi_ellipse - draw an ellipse bounded by the rectangle
 * (left, top, right, bottom).
 *
 * Uses the midpoint ellipse algorithm.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_ellipse(NEGdiContext *ctx, NEGdiHDC hdc,
                   int16_t left, int16_t top,
                   int16_t right, int16_t bottom);

/*
 * ne_gdi_polygon - draw a closed polygon defined by 'pts'.
 *
 * Connects each point to the next and closes the figure by connecting
 * the last point back to the first.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_polygon(NEGdiContext *ctx, NEGdiHDC hdc,
                   const NEGdiPoint *pts, int16_t count);

/*
 * ne_gdi_polyline - draw a series of connected line segments.
 *
 * Does not close the figure (unlike polygon).
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_polyline(NEGdiContext *ctx, NEGdiHDC hdc,
                    const NEGdiPoint *pts, int16_t count);

/* =========================================================================
 * Public API – GDI object management
 * ===================================================================== */

/*
 * ne_gdi_create_pen - create a logical pen.
 *
 * Returns a valid object handle or NE_GDI_HGDIOBJ_INVALID on failure.
 */
NEGdiHGDIOBJ ne_gdi_create_pen(NEGdiContext *ctx, int16_t style,
                               int16_t width, COLORREF color);

/*
 * ne_gdi_create_brush - create a logical brush.
 *
 * Returns a valid object handle or NE_GDI_HGDIOBJ_INVALID on failure.
 */
NEGdiHGDIOBJ ne_gdi_create_brush(NEGdiContext *ctx, int16_t style,
                                 COLORREF color);

/*
 * ne_gdi_create_font - create a logical font.
 *
 * Returns a valid object handle or NE_GDI_HGDIOBJ_INVALID on failure.
 */
NEGdiHGDIOBJ ne_gdi_create_font(NEGdiContext *ctx, int16_t height,
                                int16_t width, int16_t weight,
                                const char *face_name);

/*
 * ne_gdi_select_object - select a GDI object into a device context.
 *
 * Returns the handle of the previously selected object of the same type,
 * or NE_GDI_HGDIOBJ_INVALID on failure.
 */
NEGdiHGDIOBJ ne_gdi_select_object(NEGdiContext *ctx, NEGdiHDC hdc,
                                  NEGdiHGDIOBJ obj);

/*
 * ne_gdi_delete_object - delete a GDI object.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_delete_object(NEGdiContext *ctx, NEGdiHGDIOBJ obj);

/* =========================================================================
 * Public API – colour and text attributes
 * ===================================================================== */

/*
 * ne_gdi_set_text_color - set the text foreground colour for a DC.
 *
 * Returns the previous text colour.
 */
COLORREF ne_gdi_set_text_color(NEGdiContext *ctx, NEGdiHDC hdc,
                               COLORREF color);

/*
 * ne_gdi_set_bk_color - set the background colour for a DC.
 *
 * Returns the previous background colour.
 */
COLORREF ne_gdi_set_bk_color(NEGdiContext *ctx, NEGdiHDC hdc,
                             COLORREF color);

/*
 * ne_gdi_set_bk_mode - set the background mix mode for a DC.
 *
 * Returns the previous background mode, or 0 on failure.
 */
int ne_gdi_set_bk_mode(NEGdiContext *ctx, NEGdiHDC hdc, int mode);

/*
 * ne_gdi_get_text_metrics - fill *tm with metrics for the current font.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_get_text_metrics(NEGdiContext *ctx, NEGdiHDC hdc,
                            NEGdiTextMetrics *tm);

/*
 * ne_gdi_get_text_extent - compute the width and height of a string.
 *
 * Writes width to *cx and height to *cy.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_get_text_extent(NEGdiContext *ctx, NEGdiHDC hdc,
                           const char *text, int16_t len,
                           int16_t *cx, int16_t *cy);

/* =========================================================================
 * Public API – bit-block transfer
 * ===================================================================== */

/*
 * ne_gdi_bitblt - copy a rectangular region from src DC to dest DC.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_bitblt(NEGdiContext *ctx, NEGdiHDC dest,
                  int16_t dx, int16_t dy, int16_t w, int16_t h,
                  NEGdiHDC src, int16_t sx, int16_t sy, uint32_t rop);

/*
 * ne_gdi_stretchblt - copy and scale a rectangular region.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_stretchblt(NEGdiContext *ctx, NEGdiHDC dest,
                      int16_t dx, int16_t dy, int16_t dw, int16_t dh,
                      NEGdiHDC src,
                      int16_t sx, int16_t sy, int16_t sw, int16_t sh,
                      uint32_t rop);

/*
 * ne_gdi_patblt - fill a rectangle with the selected brush/pattern.
 *
 * Returns NE_GDI_OK on success or a negative NE_GDI_ERR_* code.
 */
int ne_gdi_patblt(NEGdiContext *ctx, NEGdiHDC hdc,
                  int16_t x, int16_t y, int16_t w, int16_t h,
                  uint32_t rop);

/* =========================================================================
 * Public API – compatible DCs and bitmaps
 * ===================================================================== */

/*
 * ne_gdi_create_compatible_dc - create a memory DC compatible with hdc.
 *
 * The new DC has its own framebuffer of the same dimensions.
 *
 * Returns a valid HDC or NE_GDI_HDC_INVALID on failure.
 */
NEGdiHDC ne_gdi_create_compatible_dc(NEGdiContext *ctx, NEGdiHDC hdc);

/*
 * ne_gdi_create_compatible_bitmap - create a bitmap compatible with hdc.
 *
 * Returns a valid object handle or NE_GDI_HGDIOBJ_INVALID on failure.
 */
NEGdiHGDIOBJ ne_gdi_create_compatible_bitmap(NEGdiContext *ctx,
                                             NEGdiHDC hdc,
                                             int16_t width, int16_t height);

/*
 * ne_gdi_create_bitmap - create a bitmap with specified parameters.
 *
 * Returns a valid object handle or NE_GDI_HGDIOBJ_INVALID on failure.
 */
NEGdiHGDIOBJ ne_gdi_create_bitmap(NEGdiContext *ctx,
                                  int16_t width, int16_t height,
                                  uint8_t planes, uint8_t bpp,
                                  const void *bits);

/*
 * ne_gdi_create_dib_bitmap - create a device-independent bitmap.
 *
 * Returns a valid object handle or NE_GDI_HGDIOBJ_INVALID on failure.
 */
NEGdiHGDIOBJ ne_gdi_create_dib_bitmap(NEGdiContext *ctx, NEGdiHDC hdc,
                                      int16_t width, int16_t height,
                                      const void *bits);

/* =========================================================================
 * Public API – error reporting
 * ===================================================================== */

/*
 * ne_gdi_strerror - return a static string describing error code 'err'.
 */
const char *ne_gdi_strerror(int err);

#endif /* NE_GDI_H */

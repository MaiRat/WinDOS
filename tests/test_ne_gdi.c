/*
 * test_ne_gdi.c - Tests for GDI.EXE subsystem (Phase 3 + Phase E)
 *
 * Verifies:
 *   - GDI context initialisation and teardown
 *   - Device context allocation and release
 *   - Paint session management (BeginPaint / EndPaint)
 *   - Drawing primitives: TextOut, MoveTo, LineTo, Rectangle, SetPixel
 *   - Error string reporting
 *   - Phase E: framebuffer rendering, GDI objects, text metrics,
 *     bit-block transfer, compatible DCs and bitmaps
 */

#include "../src/ne_gdi.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Minimal test framework (same macros as the other test files)
 * ---------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_BEGIN(name) \
    do { \
        g_tests_run++; \
        printf("  %-62s ", (name)); \
        fflush(stdout); \
    } while (0)

#define TEST_PASS() \
    do { \
        g_tests_passed++; \
        printf("PASS\n"); \
    } while (0)

#define TEST_FAIL(msg) \
    do { \
        g_tests_failed++; \
        printf("FAIL - %s (line %d)\n", (msg), __LINE__); \
        return; \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            g_tests_failed++; \
            printf("FAIL - expected %ld got %ld (line %d)\n", \
                   (long)(b), (long)(a), __LINE__); \
            return; \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            g_tests_failed++; \
            printf("FAIL - unexpected equal value %ld (line %d)\n", \
                   (long)(a), __LINE__); \
            return; \
        } \
    } while (0)

#define ASSERT_NOT_NULL(p) \
    do { \
        if ((p) == NULL) { \
            g_tests_failed++; \
            printf("FAIL - unexpected NULL pointer (line %d)\n", __LINE__); \
            return; \
        } \
    } while (0)

#define ASSERT_NULL(p) \
    do { \
        if ((p) != NULL) { \
            g_tests_failed++; \
            printf("FAIL - expected NULL pointer (line %d)\n", __LINE__); \
            return; \
        } \
    } while (0)

/* =========================================================================
 * Context tests
 * ===================================================================== */

static void test_gdi_init_free(void)
{
    NEGdiContext ctx;
    TEST_BEGIN("init sets initialized=1, free sets it to 0");

    ASSERT_EQ(ne_gdi_init(&ctx), NE_GDI_OK);
    ASSERT_EQ(ctx.initialized, 1);

    ne_gdi_free(&ctx);
    ASSERT_EQ(ctx.initialized, 0);

    TEST_PASS();
}

static void test_gdi_init_null(void)
{
    TEST_BEGIN("init with NULL returns ERR_NULL");

    ASSERT_EQ(ne_gdi_init(NULL), NE_GDI_ERR_NULL);

    TEST_PASS();
}

static void test_gdi_free_null_safe(void)
{
    TEST_BEGIN("free(NULL) does not crash");

    ne_gdi_free(NULL);

    TEST_PASS();
}

/* =========================================================================
 * Device context tests
 * ===================================================================== */

static void test_get_release_dc(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    int rc;

    TEST_BEGIN("get_dc returns valid HDC, release_dc succeeds");

    ne_gdi_init(&ctx);

    hdc = ne_gdi_get_dc(&ctx, 1);
    ASSERT_NE(hdc, NE_GDI_HDC_INVALID);
    ASSERT_EQ(ctx.dc_count, 1);

    rc = ne_gdi_release_dc(&ctx, 1, hdc);
    ASSERT_EQ(rc, NE_GDI_OK);
    ASSERT_EQ(ctx.dc_count, 0);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_get_dc_multiple(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc1, hdc2;

    TEST_BEGIN("get two DCs for different windows");

    ne_gdi_init(&ctx);

    hdc1 = ne_gdi_get_dc(&ctx, 1);
    hdc2 = ne_gdi_get_dc(&ctx, 2);
    ASSERT_NE(hdc1, NE_GDI_HDC_INVALID);
    ASSERT_NE(hdc2, NE_GDI_HDC_INVALID);
    ASSERT_NE(hdc1, hdc2);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_release_dc_invalid(void)
{
    NEGdiContext ctx;
    int rc;

    TEST_BEGIN("release with HDC_INVALID returns ERR_BAD_HANDLE");

    ne_gdi_init(&ctx);

    rc = ne_gdi_release_dc(&ctx, 1, NE_GDI_HDC_INVALID);
    ASSERT_EQ(rc, NE_GDI_ERR_BAD_HANDLE);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_release_dc_not_found(void)
{
    NEGdiContext ctx;
    int rc;

    TEST_BEGIN("release with unknown hdc returns ERR_NOT_FOUND");

    ne_gdi_init(&ctx);

    rc = ne_gdi_release_dc(&ctx, 1, 9999);
    ASSERT_EQ(rc, NE_GDI_ERR_NOT_FOUND);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_get_dc_full(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    unsigned i;

    TEST_BEGIN("fill DC table to NE_GDI_DC_CAP, next returns INVALID");

    ne_gdi_init(&ctx);

    for (i = 0; i < NE_GDI_DC_CAP; i++) {
        hdc = ne_gdi_get_dc(&ctx, (uint16_t)(i + 1));
        ASSERT_NE(hdc, NE_GDI_HDC_INVALID);
    }

    hdc = ne_gdi_get_dc(&ctx, 0);
    ASSERT_EQ(hdc, NE_GDI_HDC_INVALID);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Paint session tests
 * ===================================================================== */

static void test_begin_end_paint(void)
{
    NEGdiContext ctx;
    NEGdiPaintStruct ps;
    NEGdiHDC hdc;
    int rc;

    TEST_BEGIN("begin_paint returns valid HDC, end_paint releases");

    ne_gdi_init(&ctx);

    hdc = ne_gdi_begin_paint(&ctx, 1, &ps);
    ASSERT_NE(hdc, NE_GDI_HDC_INVALID);
    ASSERT_EQ(ps.hdc, hdc);
    ASSERT_EQ(ctx.dc_count, 1);

    rc = ne_gdi_end_paint(&ctx, 1, &ps);
    ASSERT_EQ(rc, NE_GDI_OK);
    ASSERT_EQ(ctx.dc_count, 0);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_begin_paint_sets_erase(void)
{
    NEGdiContext ctx;
    NEGdiPaintStruct ps;

    TEST_BEGIN("begin_paint sets erase_bg == 1");

    ne_gdi_init(&ctx);

    ne_gdi_begin_paint(&ctx, 1, &ps);
    ASSERT_EQ(ps.erase_bg, 1);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_begin_paint_null_ps(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;

    TEST_BEGIN("begin_paint with NULL ps returns HDC_INVALID");

    ne_gdi_init(&ctx);

    hdc = ne_gdi_begin_paint(&ctx, 1, NULL);
    ASSERT_EQ(hdc, NE_GDI_HDC_INVALID);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_end_paint_not_found(void)
{
    NEGdiContext ctx;
    NEGdiPaintStruct ps;
    int rc;

    TEST_BEGIN("end_paint with invalid ps->hdc returns ERR_NOT_FOUND");

    ne_gdi_init(&ctx);

    memset(&ps, 0, sizeof(ps));
    ps.hdc = 9999;

    rc = ne_gdi_end_paint(&ctx, 1, &ps);
    ASSERT_EQ(rc, NE_GDI_ERR_NOT_FOUND);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Drawing primitive tests
 * ===================================================================== */

static void test_text_out(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    int rc;

    TEST_BEGIN("text_out on valid DC returns OK");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    rc = ne_gdi_text_out(&ctx, hdc, 10, 20, "Hello", 5);
    ASSERT_EQ(rc, NE_GDI_OK);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_text_out_null_text(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    int rc;

    TEST_BEGIN("text_out with NULL text returns ERR_NULL");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    rc = ne_gdi_text_out(&ctx, hdc, 10, 20, NULL, 0);
    ASSERT_EQ(rc, NE_GDI_ERR_NULL);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_text_out_invalid_dc(void)
{
    NEGdiContext ctx;
    int rc;

    TEST_BEGIN("text_out with HDC_INVALID returns ERR_BAD_HANDLE");

    ne_gdi_init(&ctx);

    rc = ne_gdi_text_out(&ctx, NE_GDI_HDC_INVALID, 10, 20, "Hi", 2);
    ASSERT_EQ(rc, NE_GDI_ERR_BAD_HANDLE);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_move_to(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    NEGdiDC *dc = NULL;
    unsigned j;
    int rc;

    TEST_BEGIN("move_to updates cur_x and cur_y");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    rc = ne_gdi_move_to(&ctx, hdc, 42, 84);
    ASSERT_EQ(rc, NE_GDI_OK);

    for (j = 0; j < NE_GDI_DC_CAP; j++) {
        if (ctx.dcs[j].active && ctx.dcs[j].hdc == hdc) {
            dc = &ctx.dcs[j];
            break;
        }
    }
    ASSERT_NOT_NULL(dc);
    ASSERT_EQ(dc->cur_x, 42);
    ASSERT_EQ(dc->cur_y, 84);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_line_to(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    NEGdiDC *dc = NULL;
    unsigned j;
    int rc;

    TEST_BEGIN("line_to updates cur_x and cur_y");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    rc = ne_gdi_line_to(&ctx, hdc, 100, 200);
    ASSERT_EQ(rc, NE_GDI_OK);

    for (j = 0; j < NE_GDI_DC_CAP; j++) {
        if (ctx.dcs[j].active && ctx.dcs[j].hdc == hdc) {
            dc = &ctx.dcs[j];
            break;
        }
    }
    ASSERT_NOT_NULL(dc);
    ASSERT_EQ(dc->cur_x, 100);
    ASSERT_EQ(dc->cur_y, 200);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_rectangle(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    int rc;

    TEST_BEGIN("rectangle on valid DC returns OK");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    rc = ne_gdi_rectangle(&ctx, hdc, 0, 0, 100, 50);
    ASSERT_EQ(rc, NE_GDI_OK);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_rectangle_invalid_dc(void)
{
    NEGdiContext ctx;
    int rc;

    TEST_BEGIN("rectangle with unknown DC returns ERR_NOT_FOUND");

    ne_gdi_init(&ctx);

    rc = ne_gdi_rectangle(&ctx, 9999, 0, 0, 100, 50);
    ASSERT_EQ(rc, NE_GDI_ERR_NOT_FOUND);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_set_pixel(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    uint32_t result;

    TEST_BEGIN("set_pixel on valid DC returns the color value");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    result = ne_gdi_set_pixel(&ctx, hdc, 5, 10, 0x00FF00u);
    ASSERT_EQ(result, 0x00FF00u);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_set_pixel_invalid_dc(void)
{
    NEGdiContext ctx;
    uint32_t result;

    TEST_BEGIN("set_pixel with HDC_INVALID returns 0xFFFFFFFF");

    ne_gdi_init(&ctx);

    result = ne_gdi_set_pixel(&ctx, NE_GDI_HDC_INVALID, 5, 10, 0x00FF00u);
    ASSERT_EQ(result, 0xFFFFFFFFu);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Error string test
 * ===================================================================== */

static void test_gdi_strerror(void)
{
    TEST_BEGIN("strerror returns non-NULL for all known codes");

    ASSERT_NOT_NULL(ne_gdi_strerror(NE_GDI_OK));
    ASSERT_NOT_NULL(ne_gdi_strerror(NE_GDI_ERR_NULL));
    ASSERT_NOT_NULL(ne_gdi_strerror(NE_GDI_ERR_INIT));
    ASSERT_NOT_NULL(ne_gdi_strerror(NE_GDI_ERR_FULL));
    ASSERT_NOT_NULL(ne_gdi_strerror(NE_GDI_ERR_NOT_FOUND));
    ASSERT_NOT_NULL(ne_gdi_strerror(NE_GDI_ERR_BAD_HANDLE));

    TEST_PASS();
}

/* =========================================================================
 * Phase E: GDI rendering tests
 * ===================================================================== */

static void test_set_get_pixel(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    uint32_t val;

    TEST_BEGIN("get_pixel returns value written by set_pixel");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    ne_gdi_set_pixel(&ctx, hdc, 10, 20, 0x42);
    val = ne_gdi_get_pixel(&ctx, hdc, 10, 20);
    ASSERT_EQ(val, 0x42u);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_set_pixel_bounds(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    uint32_t result;

    TEST_BEGIN("set_pixel out of bounds returns 0xFFFFFFFF");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    result = ne_gdi_set_pixel(&ctx, hdc, -1, 0, 0x01);
    ASSERT_EQ(result, 0xFFFFFFFFu);

    result = ne_gdi_set_pixel(&ctx, hdc, 0, -1, 0x01);
    ASSERT_EQ(result, 0xFFFFFFFFu);

    result = ne_gdi_set_pixel(&ctx, hdc, NE_GDI_FB_WIDTH, 0, 0x01);
    ASSERT_EQ(result, 0xFFFFFFFFu);

    result = ne_gdi_set_pixel(&ctx, hdc, 0, NE_GDI_FB_HEIGHT, 0x01);
    ASSERT_EQ(result, 0xFFFFFFFFu);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_get_pixel_bounds(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    uint32_t val;

    TEST_BEGIN("get_pixel out of bounds returns 0xFFFFFFFF");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    val = ne_gdi_get_pixel(&ctx, hdc, -1, 0);
    ASSERT_EQ(val, 0xFFFFFFFFu);

    val = ne_gdi_get_pixel(&ctx, hdc, 0, NE_GDI_FB_HEIGHT);
    ASSERT_EQ(val, 0xFFFFFFFFu);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_get_pixel_invalid_dc(void)
{
    NEGdiContext ctx;
    uint32_t val;

    TEST_BEGIN("get_pixel with HDC_INVALID returns 0xFFFFFFFF");

    ne_gdi_init(&ctx);

    val = ne_gdi_get_pixel(&ctx, NE_GDI_HDC_INVALID, 0, 0);
    ASSERT_EQ(val, 0xFFFFFFFFu);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_line_to_draws_pixels(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    uint32_t val;

    TEST_BEGIN("line_to actually draws pixels in framebuffer");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    /* Set pen colour via a pen object */
    {
        NEGdiHGDIOBJ pen = ne_gdi_create_pen(&ctx, 0, 1, 0xAA);
        ne_gdi_select_object(&ctx, hdc, pen);
    }

    ne_gdi_move_to(&ctx, hdc, 10, 10);
    ne_gdi_line_to(&ctx, hdc, 20, 10);

    /* The horizontal line should have drawn pixels */
    val = ne_gdi_get_pixel(&ctx, hdc, 10, 10);
    ASSERT_EQ(val, 0xAAu);
    val = ne_gdi_get_pixel(&ctx, hdc, 15, 10);
    ASSERT_EQ(val, 0xAAu);
    val = ne_gdi_get_pixel(&ctx, hdc, 20, 10);
    ASSERT_EQ(val, 0xAAu);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_rectangle_draws_pixels(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    uint32_t val;

    TEST_BEGIN("rectangle draws outline pixels");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    {
        NEGdiHGDIOBJ pen = ne_gdi_create_pen(&ctx, 0, 1, 0xBB);
        ne_gdi_select_object(&ctx, hdc, pen);
    }

    ne_gdi_rectangle(&ctx, hdc, 10, 10, 20, 20);

    /* Check a corner pixel on the outline */
    val = ne_gdi_get_pixel(&ctx, hdc, 10, 10);
    ASSERT_EQ(val, 0xBBu);

    /* Check top edge */
    val = ne_gdi_get_pixel(&ctx, hdc, 15, 10);
    ASSERT_EQ(val, 0xBBu);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_text_out_draws_pixels(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    uint32_t before, after;
    int found_pixel = 0;
    int16_t r, c;

    TEST_BEGIN("text_out renders pixels to framebuffer");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    /* Set text colour */
    ne_gdi_set_text_color(&ctx, hdc, 0x55);

    /* Check pixel before */
    before = ne_gdi_get_pixel(&ctx, hdc, 100, 100);

    /* Render letter 'A' at (100, 100) */
    ne_gdi_text_out(&ctx, hdc, 100, 100, "A", 1);

    /* Verify at least one pixel in the 8x8 area changed */
    for (r = 0; r < 8 && !found_pixel; r++) {
        for (c = 0; c < 8 && !found_pixel; c++) {
            after = ne_gdi_get_pixel(&ctx, hdc, (int16_t)(100 + c),
                                     (int16_t)(100 + r));
            if (after == 0x55)
                found_pixel = 1;
        }
    }

    (void)before;
    ASSERT_EQ(found_pixel, 1);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_ellipse(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    int rc;
    uint32_t val;

    TEST_BEGIN("ellipse draws pixels");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    {
        NEGdiHGDIOBJ pen = ne_gdi_create_pen(&ctx, 0, 1, 0xCC);
        ne_gdi_select_object(&ctx, hdc, pen);
    }

    rc = ne_gdi_ellipse(&ctx, hdc, 100, 100, 120, 110);
    ASSERT_EQ(rc, NE_GDI_OK);

    /* Top-centre of ellipse should be drawn */
    val = ne_gdi_get_pixel(&ctx, hdc, 110, 100);
    ASSERT_EQ(val, 0xCCu);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_ellipse_invalid_dc(void)
{
    NEGdiContext ctx;
    int rc;

    TEST_BEGIN("ellipse with HDC_INVALID returns ERR_BAD_HANDLE");

    ne_gdi_init(&ctx);

    rc = ne_gdi_ellipse(&ctx, NE_GDI_HDC_INVALID, 0, 0, 10, 10);
    ASSERT_EQ(rc, NE_GDI_ERR_BAD_HANDLE);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_polygon(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    NEGdiPoint pts[3];
    int rc;
    uint32_t val;

    TEST_BEGIN("polygon draws triangle and closes figure");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    {
        NEGdiHGDIOBJ pen = ne_gdi_create_pen(&ctx, 0, 1, 0xDD);
        ne_gdi_select_object(&ctx, hdc, pen);
    }

    pts[0].x = 50; pts[0].y = 50;
    pts[1].x = 60; pts[1].y = 50;
    pts[2].x = 55; pts[2].y = 60;

    rc = ne_gdi_polygon(&ctx, hdc, pts, 3);
    ASSERT_EQ(rc, NE_GDI_OK);

    /* First vertex should have a pixel */
    val = ne_gdi_get_pixel(&ctx, hdc, 50, 50);
    ASSERT_EQ(val, 0xDDu);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_polygon_null_pts(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    int rc;

    TEST_BEGIN("polygon with NULL pts returns ERR_NULL");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    rc = ne_gdi_polygon(&ctx, hdc, NULL, 3);
    ASSERT_EQ(rc, NE_GDI_ERR_NULL);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_polyline(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    NEGdiPoint pts[3];
    int rc;
    uint32_t val;

    TEST_BEGIN("polyline draws connected segments");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    {
        NEGdiHGDIOBJ pen = ne_gdi_create_pen(&ctx, 0, 1, 0xEE);
        ne_gdi_select_object(&ctx, hdc, pen);
    }

    pts[0].x = 30; pts[0].y = 30;
    pts[1].x = 40; pts[1].y = 30;
    pts[2].x = 40; pts[2].y = 40;

    rc = ne_gdi_polyline(&ctx, hdc, pts, 3);
    ASSERT_EQ(rc, NE_GDI_OK);

    val = ne_gdi_get_pixel(&ctx, hdc, 35, 30);
    ASSERT_EQ(val, 0xEEu);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_polyline_null_pts(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    int rc;

    TEST_BEGIN("polyline with NULL pts returns ERR_NULL");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    rc = ne_gdi_polyline(&ctx, hdc, NULL, 3);
    ASSERT_EQ(rc, NE_GDI_ERR_NULL);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_create_pen(void)
{
    NEGdiContext ctx;
    NEGdiHGDIOBJ pen;

    TEST_BEGIN("create_pen returns valid handle");

    ne_gdi_init(&ctx);

    pen = ne_gdi_create_pen(&ctx, 0, 1, RGB(255, 0, 0));
    ASSERT_NE(pen, NE_GDI_HGDIOBJ_INVALID);
    ASSERT_EQ(ctx.obj_count, 1);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_create_brush(void)
{
    NEGdiContext ctx;
    NEGdiHGDIOBJ brush;

    TEST_BEGIN("create_brush returns valid handle");

    ne_gdi_init(&ctx);

    brush = ne_gdi_create_brush(&ctx, 0, RGB(0, 255, 0));
    ASSERT_NE(brush, NE_GDI_HGDIOBJ_INVALID);
    ASSERT_EQ(ctx.obj_count, 1);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_create_font(void)
{
    NEGdiContext ctx;
    NEGdiHGDIOBJ font;

    TEST_BEGIN("create_font returns valid handle");

    ne_gdi_init(&ctx);

    font = ne_gdi_create_font(&ctx, 8, 8, 400, "System");
    ASSERT_NE(font, NE_GDI_HGDIOBJ_INVALID);
    ASSERT_EQ(ctx.obj_count, 1);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_create_font_null_face(void)
{
    NEGdiContext ctx;
    NEGdiHGDIOBJ font;

    TEST_BEGIN("create_font with NULL face_name succeeds");

    ne_gdi_init(&ctx);

    font = ne_gdi_create_font(&ctx, 8, 8, 400, NULL);
    ASSERT_NE(font, NE_GDI_HGDIOBJ_INVALID);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_create_pen_null_ctx(void)
{
    NEGdiHGDIOBJ pen;

    TEST_BEGIN("create_pen with NULL ctx returns INVALID");

    pen = ne_gdi_create_pen(NULL, 0, 1, 0);
    ASSERT_EQ(pen, NE_GDI_HGDIOBJ_INVALID);

    TEST_PASS();
}

static void test_select_object(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    NEGdiHGDIOBJ pen1, pen2, prev;

    TEST_BEGIN("select_object swaps pen and returns previous");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    pen1 = ne_gdi_create_pen(&ctx, 0, 1, 0x01);
    pen2 = ne_gdi_create_pen(&ctx, 0, 1, 0x02);

    prev = ne_gdi_select_object(&ctx, hdc, pen1);
    ASSERT_EQ(prev, NE_GDI_HGDIOBJ_INVALID); /* no previous */

    prev = ne_gdi_select_object(&ctx, hdc, pen2);
    ASSERT_EQ(prev, pen1);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_select_object_invalid(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    NEGdiHGDIOBJ prev;

    TEST_BEGIN("select_object with invalid obj returns INVALID");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    prev = ne_gdi_select_object(&ctx, hdc, 9999);
    ASSERT_EQ(prev, NE_GDI_HGDIOBJ_INVALID);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_delete_object(void)
{
    NEGdiContext ctx;
    NEGdiHGDIOBJ pen;
    int rc;

    TEST_BEGIN("delete_object frees a pen");

    ne_gdi_init(&ctx);

    pen = ne_gdi_create_pen(&ctx, 0, 1, 0x01);
    ASSERT_EQ(ctx.obj_count, 1);

    rc = ne_gdi_delete_object(&ctx, pen);
    ASSERT_EQ(rc, NE_GDI_OK);
    ASSERT_EQ(ctx.obj_count, 0);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_delete_object_invalid(void)
{
    NEGdiContext ctx;
    int rc;

    TEST_BEGIN("delete_object with INVALID handle returns ERR_BAD_HANDLE");

    ne_gdi_init(&ctx);

    rc = ne_gdi_delete_object(&ctx, NE_GDI_HGDIOBJ_INVALID);
    ASSERT_EQ(rc, NE_GDI_ERR_BAD_HANDLE);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_delete_object_not_found(void)
{
    NEGdiContext ctx;
    int rc;

    TEST_BEGIN("delete_object with unknown handle returns ERR_NOT_FOUND");

    ne_gdi_init(&ctx);

    rc = ne_gdi_delete_object(&ctx, 9999);
    ASSERT_EQ(rc, NE_GDI_ERR_NOT_FOUND);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_set_text_color(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    COLORREF prev;

    TEST_BEGIN("set_text_color changes color and returns previous");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    prev = ne_gdi_set_text_color(&ctx, hdc, 0x0000FF);
    /* Default text colour is white */
    ASSERT_EQ(prev, 0x00FFFFFFu);

    prev = ne_gdi_set_text_color(&ctx, hdc, 0x00FF00);
    ASSERT_EQ(prev, 0x0000FFu);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_set_bk_color(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    COLORREF prev;

    TEST_BEGIN("set_bk_color changes color and returns previous");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    prev = ne_gdi_set_bk_color(&ctx, hdc, 0x0000FF);
    ASSERT_EQ(prev, 0x00000000u); /* default black */

    prev = ne_gdi_set_bk_color(&ctx, hdc, 0x00FF00);
    ASSERT_EQ(prev, 0x0000FFu);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_set_bk_mode(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    int prev;

    TEST_BEGIN("set_bk_mode changes mode and returns previous");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    prev = ne_gdi_set_bk_mode(&ctx, hdc, NE_GDI_TRANSPARENT);
    ASSERT_EQ(prev, NE_GDI_OPAQUE); /* default */

    prev = ne_gdi_set_bk_mode(&ctx, hdc, NE_GDI_OPAQUE);
    ASSERT_EQ(prev, NE_GDI_TRANSPARENT);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_get_text_metrics(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    NEGdiTextMetrics tm;
    int rc;

    TEST_BEGIN("get_text_metrics returns 8x8 font metrics");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    rc = ne_gdi_get_text_metrics(&ctx, hdc, &tm);
    ASSERT_EQ(rc, NE_GDI_OK);
    ASSERT_EQ(tm.height, 8);
    ASSERT_EQ(tm.avg_char_width, 8);
    ASSERT_EQ(tm.max_char_width, 8);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_get_text_metrics_null_tm(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    int rc;

    TEST_BEGIN("get_text_metrics with NULL tm returns ERR_NULL");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    rc = ne_gdi_get_text_metrics(&ctx, hdc, NULL);
    ASSERT_EQ(rc, NE_GDI_ERR_NULL);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_get_text_extent(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    int16_t cx, cy;
    int rc;

    TEST_BEGIN("get_text_extent returns width=len*8, height=8");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    rc = ne_gdi_get_text_extent(&ctx, hdc, "Hello", 5, &cx, &cy);
    ASSERT_EQ(rc, NE_GDI_OK);
    ASSERT_EQ(cx, 40);
    ASSERT_EQ(cy, 8);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_get_text_extent_null(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    int rc;

    TEST_BEGIN("get_text_extent with NULL params returns ERR_NULL");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    rc = ne_gdi_get_text_extent(&ctx, hdc, NULL, 5, NULL, NULL);
    ASSERT_EQ(rc, NE_GDI_ERR_NULL);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_bitblt_copies_pixels(void)
{
    NEGdiContext ctx;
    NEGdiHDC src_hdc, dst_hdc;
    uint32_t val;
    int rc;

    TEST_BEGIN("bitblt copies pixels from src to dst DC");

    ne_gdi_init(&ctx);

    src_hdc = ne_gdi_create_compatible_dc(&ctx, NE_GDI_HDC_INVALID);
    dst_hdc = ne_gdi_create_compatible_dc(&ctx, NE_GDI_HDC_INVALID);
    ASSERT_NE(src_hdc, NE_GDI_HDC_INVALID);
    ASSERT_NE(dst_hdc, NE_GDI_HDC_INVALID);

    /* Write some pixels to source */
    ne_gdi_set_pixel(&ctx, src_hdc, 5, 5, 0x77);
    ne_gdi_set_pixel(&ctx, src_hdc, 6, 5, 0x88);

    /* BitBlt */
    rc = ne_gdi_bitblt(&ctx, dst_hdc, 10, 10, 10, 10,
                       src_hdc, 0, 0, NE_GDI_SRCCOPY);
    ASSERT_EQ(rc, NE_GDI_OK);

    /* Verify pixels were copied */
    val = ne_gdi_get_pixel(&ctx, dst_hdc, 15, 15);
    ASSERT_EQ(val, 0x77u);
    val = ne_gdi_get_pixel(&ctx, dst_hdc, 16, 15);
    ASSERT_EQ(val, 0x88u);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_bitblt_blackness(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    uint32_t val;
    int rc;

    TEST_BEGIN("bitblt with BLACKNESS clears area to 0");

    ne_gdi_init(&ctx);

    hdc = ne_gdi_create_compatible_dc(&ctx, NE_GDI_HDC_INVALID);
    ne_gdi_set_pixel(&ctx, hdc, 5, 5, 0xFF);

    rc = ne_gdi_bitblt(&ctx, hdc, 0, 0, 20, 20,
                       NE_GDI_HDC_INVALID, 0, 0, NE_GDI_BLACKNESS);
    ASSERT_EQ(rc, NE_GDI_OK);

    val = ne_gdi_get_pixel(&ctx, hdc, 5, 5);
    ASSERT_EQ(val, 0x00u);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_bitblt_invalid_dc(void)
{
    NEGdiContext ctx;
    int rc;

    TEST_BEGIN("bitblt with invalid dest returns ERR_BAD_HANDLE");

    ne_gdi_init(&ctx);

    rc = ne_gdi_bitblt(&ctx, NE_GDI_HDC_INVALID, 0, 0, 10, 10,
                       NE_GDI_HDC_INVALID, 0, 0, NE_GDI_SRCCOPY);
    ASSERT_EQ(rc, NE_GDI_ERR_BAD_HANDLE);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_stretchblt(void)
{
    NEGdiContext ctx;
    NEGdiHDC src_hdc, dst_hdc;
    uint32_t val;
    int rc;

    TEST_BEGIN("stretchblt scales pixels (2x)");

    ne_gdi_init(&ctx);

    src_hdc = ne_gdi_create_compatible_dc(&ctx, NE_GDI_HDC_INVALID);
    dst_hdc = ne_gdi_create_compatible_dc(&ctx, NE_GDI_HDC_INVALID);

    /* Set a 2x2 block in source */
    ne_gdi_set_pixel(&ctx, src_hdc, 0, 0, 0x11);
    ne_gdi_set_pixel(&ctx, src_hdc, 1, 0, 0x22);
    ne_gdi_set_pixel(&ctx, src_hdc, 0, 1, 0x33);
    ne_gdi_set_pixel(&ctx, src_hdc, 1, 1, 0x44);

    /* Stretch 2x2 to 4x4 */
    rc = ne_gdi_stretchblt(&ctx, dst_hdc, 0, 0, 4, 4,
                           src_hdc, 0, 0, 2, 2, NE_GDI_SRCCOPY);
    ASSERT_EQ(rc, NE_GDI_OK);

    /* Nearest-neighbour: (0,0) and (1,0) should both be 0x11 */
    val = ne_gdi_get_pixel(&ctx, dst_hdc, 0, 0);
    ASSERT_EQ(val, 0x11u);
    val = ne_gdi_get_pixel(&ctx, dst_hdc, 1, 0);
    ASSERT_EQ(val, 0x11u);

    /* (2,0) and (3,0) should be 0x22 */
    val = ne_gdi_get_pixel(&ctx, dst_hdc, 2, 0);
    ASSERT_EQ(val, 0x22u);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_patblt(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    uint32_t val;
    int rc;

    TEST_BEGIN("patblt fills area with brush pattern");

    ne_gdi_init(&ctx);

    hdc = ne_gdi_create_compatible_dc(&ctx, NE_GDI_HDC_INVALID);

    {
        NEGdiHGDIOBJ brush = ne_gdi_create_brush(&ctx, 0, 0x99);
        ne_gdi_select_object(&ctx, hdc, brush);
    }

    rc = ne_gdi_patblt(&ctx, hdc, 5, 5, 10, 10, NE_GDI_PATCOPY);
    ASSERT_EQ(rc, NE_GDI_OK);

    val = ne_gdi_get_pixel(&ctx, hdc, 10, 10);
    ASSERT_EQ(val, 0x99u);

    /* Outside filled region should be 0 */
    val = ne_gdi_get_pixel(&ctx, hdc, 0, 0);
    ASSERT_EQ(val, 0x00u);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_patblt_whiteness(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    uint32_t val;
    int rc;

    TEST_BEGIN("patblt WHITENESS fills with 0xFF");

    ne_gdi_init(&ctx);

    hdc = ne_gdi_create_compatible_dc(&ctx, NE_GDI_HDC_INVALID);

    rc = ne_gdi_patblt(&ctx, hdc, 0, 0, 5, 5, NE_GDI_WHITENESS);
    ASSERT_EQ(rc, NE_GDI_OK);

    val = ne_gdi_get_pixel(&ctx, hdc, 2, 2);
    ASSERT_EQ(val, 0xFFu);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_create_compatible_dc(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;

    TEST_BEGIN("create_compatible_dc returns valid HDC with own fb");

    ne_gdi_init(&ctx);

    hdc = ne_gdi_create_compatible_dc(&ctx, NE_GDI_HDC_INVALID);
    ASSERT_NE(hdc, NE_GDI_HDC_INVALID);

    /* Writing to compatible DC should not affect screen fb */
    ne_gdi_set_pixel(&ctx, hdc, 0, 0, 0xAA);
    ASSERT_EQ(ctx.framebuffer[0], 0x00);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_create_compatible_dc_null_ctx(void)
{
    NEGdiHDC hdc;

    TEST_BEGIN("create_compatible_dc with NULL ctx returns INVALID");

    hdc = ne_gdi_create_compatible_dc(NULL, NE_GDI_HDC_INVALID);
    ASSERT_EQ(hdc, NE_GDI_HDC_INVALID);

    TEST_PASS();
}

static void test_create_compatible_bitmap(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    NEGdiHGDIOBJ bmp;

    TEST_BEGIN("create_compatible_bitmap returns valid handle");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    bmp = ne_gdi_create_compatible_bitmap(&ctx, hdc, 32, 32);
    ASSERT_NE(bmp, NE_GDI_HGDIOBJ_INVALID);
    ASSERT_EQ(ctx.obj_count, 1);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_create_compatible_bitmap_invalid_size(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    NEGdiHGDIOBJ bmp;

    TEST_BEGIN("create_compatible_bitmap with zero size returns INVALID");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    bmp = ne_gdi_create_compatible_bitmap(&ctx, hdc, 0, 10);
    ASSERT_EQ(bmp, NE_GDI_HGDIOBJ_INVALID);

    bmp = ne_gdi_create_compatible_bitmap(&ctx, hdc, 10, 0);
    ASSERT_EQ(bmp, NE_GDI_HGDIOBJ_INVALID);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_create_bitmap(void)
{
    NEGdiContext ctx;
    NEGdiHGDIOBJ bmp;
    uint8_t data[4] = {0x11, 0x22, 0x33, 0x44};

    TEST_BEGIN("create_bitmap with initial data");

    ne_gdi_init(&ctx);

    bmp = ne_gdi_create_bitmap(&ctx, 2, 2, 1, 8, data);
    ASSERT_NE(bmp, NE_GDI_HGDIOBJ_INVALID);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_create_bitmap_null_bits(void)
{
    NEGdiContext ctx;
    NEGdiHGDIOBJ bmp;

    TEST_BEGIN("create_bitmap with NULL bits zeroes the buffer");

    ne_gdi_init(&ctx);

    bmp = ne_gdi_create_bitmap(&ctx, 4, 4, 1, 8, NULL);
    ASSERT_NE(bmp, NE_GDI_HGDIOBJ_INVALID);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_create_dib_bitmap(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    NEGdiHGDIOBJ bmp;
    uint8_t data[9] = {0};

    TEST_BEGIN("create_dib_bitmap returns valid handle");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    bmp = ne_gdi_create_dib_bitmap(&ctx, hdc, 3, 3, data);
    ASSERT_NE(bmp, NE_GDI_HGDIOBJ_INVALID);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_init_allocates_framebuffer(void)
{
    NEGdiContext ctx;

    TEST_BEGIN("init allocates framebuffer, free releases it");

    ne_gdi_init(&ctx);
    ASSERT_NOT_NULL(ctx.framebuffer);

    ne_gdi_free(&ctx);
    /* After free, framebuffer is NULL (zeroed) */
    ASSERT_NULL(ctx.framebuffer);

    TEST_PASS();
}

static void test_rgb_macro(void)
{
    COLORREF c;

    TEST_BEGIN("RGB macro encodes 0x00BBGGRR");

    c = RGB(0xFF, 0x00, 0x00);
    ASSERT_EQ(c, 0x000000FFu);

    c = RGB(0x00, 0xFF, 0x00);
    ASSERT_EQ(c, 0x0000FF00u);

    c = RGB(0x00, 0x00, 0xFF);
    ASSERT_EQ(c, 0x00FF0000u);

    TEST_PASS();
}

static void test_delete_bitmap_frees_bits(void)
{
    NEGdiContext ctx;
    NEGdiHGDIOBJ bmp;
    int rc;

    TEST_BEGIN("delete_object on bitmap frees pixel data");

    ne_gdi_init(&ctx);

    bmp = ne_gdi_create_bitmap(&ctx, 16, 16, 1, 8, NULL);
    ASSERT_NE(bmp, NE_GDI_HGDIOBJ_INVALID);

    rc = ne_gdi_delete_object(&ctx, bmp);
    ASSERT_EQ(rc, NE_GDI_OK);
    ASSERT_EQ(ctx.obj_count, 0);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_select_brush(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    NEGdiHGDIOBJ brush, prev;

    TEST_BEGIN("select_object works for brush type");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);

    brush = ne_gdi_create_brush(&ctx, 0, 0x42);
    prev = ne_gdi_select_object(&ctx, hdc, brush);
    ASSERT_EQ(prev, NE_GDI_HGDIOBJ_INVALID);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_compatible_dc_drawing(void)
{
    NEGdiContext ctx;
    NEGdiHDC mem_hdc;
    uint32_t val;

    TEST_BEGIN("drawing on compatible DC uses its own fb");

    ne_gdi_init(&ctx);

    mem_hdc = ne_gdi_create_compatible_dc(&ctx, NE_GDI_HDC_INVALID);

    ne_gdi_set_pixel(&ctx, mem_hdc, 100, 100, 0x77);
    val = ne_gdi_get_pixel(&ctx, mem_hdc, 100, 100);
    ASSERT_EQ(val, 0x77u);

    /* Screen fb should be unaffected */
    ASSERT_EQ(ctx.framebuffer[100 * NE_GDI_FB_WIDTH + 100], 0x00);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_text_out_nul_terminated(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    int found = 0;
    int16_t r, c;
    int rc;

    TEST_BEGIN("text_out with len=-1 uses NUL-terminated string");

    ne_gdi_init(&ctx);
    hdc = ne_gdi_get_dc(&ctx, 1);
    ne_gdi_set_text_color(&ctx, hdc, 0x33);

    rc = ne_gdi_text_out(&ctx, hdc, 200, 200, "AB", -1);
    ASSERT_EQ(rc, NE_GDI_OK);

    /* Verify some pixels were drawn in the second character area */
    for (r = 0; r < 8 && !found; r++)
        for (c = 0; c < 8 && !found; c++)
            if (ne_gdi_get_pixel(&ctx, hdc, (int16_t)(208 + c),
                                 (int16_t)(200 + r)) == 0x33)
                found = 1;

    ASSERT_EQ(found, 1);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_bitblt_srcinvert(void)
{
    NEGdiContext ctx;
    NEGdiHDC hdc;
    uint32_t val;
    int rc;

    TEST_BEGIN("bitblt SRCINVERT XORs source into dest");

    ne_gdi_init(&ctx);

    hdc = ne_gdi_create_compatible_dc(&ctx, NE_GDI_HDC_INVALID);

    ne_gdi_set_pixel(&ctx, hdc, 0, 0, 0xFF);

    /* XOR with self (src=dest), pixel at (0,0) is 0xFF, XOR with 0xFF = 0 */
    rc = ne_gdi_bitblt(&ctx, hdc, 0, 0, 1, 1,
                       hdc, 0, 0, NE_GDI_SRCINVERT);
    ASSERT_EQ(rc, NE_GDI_OK);

    val = ne_gdi_get_pixel(&ctx, hdc, 0, 0);
    ASSERT_EQ(val, 0x00u);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_stretchblt_invalid_dc(void)
{
    NEGdiContext ctx;
    int rc;

    TEST_BEGIN("stretchblt with invalid dest returns ERR_BAD_HANDLE");

    ne_gdi_init(&ctx);

    rc = ne_gdi_stretchblt(&ctx, NE_GDI_HDC_INVALID, 0, 0, 10, 10,
                           NE_GDI_HDC_INVALID, 0, 0, 10, 10,
                           NE_GDI_SRCCOPY);
    ASSERT_EQ(rc, NE_GDI_ERR_BAD_HANDLE);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

static void test_patblt_invalid_dc(void)
{
    NEGdiContext ctx;
    int rc;

    TEST_BEGIN("patblt with invalid DC returns ERR_BAD_HANDLE");

    ne_gdi_init(&ctx);

    rc = ne_gdi_patblt(&ctx, NE_GDI_HDC_INVALID, 0, 0, 10, 10,
                       NE_GDI_PATCOPY);
    ASSERT_EQ(rc, NE_GDI_ERR_BAD_HANDLE);

    ne_gdi_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== WinDOS GDI.EXE Subsystem Tests (Phase 3 + Phase E) ===\n");

    /* --- Context --- */
    printf("\n--- Context ---\n");
    test_gdi_init_free();
    test_gdi_init_null();
    test_gdi_free_null_safe();

    /* --- Device contexts --- */
    printf("\n--- Device contexts ---\n");
    test_get_release_dc();
    test_get_dc_multiple();
    test_release_dc_invalid();
    test_release_dc_not_found();
    test_get_dc_full();

    /* --- Paint sessions --- */
    printf("\n--- Paint sessions ---\n");
    test_begin_end_paint();
    test_begin_paint_sets_erase();
    test_begin_paint_null_ps();
    test_end_paint_not_found();

    /* --- Drawing primitives --- */
    printf("\n--- Drawing primitives ---\n");
    test_text_out();
    test_text_out_null_text();
    test_text_out_invalid_dc();
    test_move_to();
    test_line_to();
    test_rectangle();
    test_rectangle_invalid_dc();
    test_set_pixel();
    test_set_pixel_invalid_dc();

    /* --- Error strings --- */
    printf("\n--- Error strings ---\n");
    test_gdi_strerror();

    /* --- Phase E: GDI rendering --- */
    printf("\n--- Phase E: GDI rendering ---\n");
    test_init_allocates_framebuffer();
    test_rgb_macro();
    test_set_get_pixel();
    test_set_pixel_bounds();
    test_get_pixel_bounds();
    test_get_pixel_invalid_dc();
    test_line_to_draws_pixels();
    test_rectangle_draws_pixels();
    test_text_out_draws_pixels();
    test_text_out_nul_terminated();
    test_ellipse();
    test_ellipse_invalid_dc();
    test_polygon();
    test_polygon_null_pts();
    test_polyline();
    test_polyline_null_pts();
    test_create_pen();
    test_create_brush();
    test_create_font();
    test_create_font_null_face();
    test_create_pen_null_ctx();
    test_select_object();
    test_select_object_invalid();
    test_select_brush();
    test_delete_object();
    test_delete_object_invalid();
    test_delete_object_not_found();
    test_delete_bitmap_frees_bits();
    test_set_text_color();
    test_set_bk_color();
    test_set_bk_mode();
    test_get_text_metrics();
    test_get_text_metrics_null_tm();
    test_get_text_extent();
    test_get_text_extent_null();
    test_bitblt_copies_pixels();
    test_bitblt_blackness();
    test_bitblt_srcinvert();
    test_bitblt_invalid_dc();
    test_stretchblt();
    test_stretchblt_invalid_dc();
    test_patblt();
    test_patblt_whiteness();
    test_patblt_invalid_dc();
    test_create_compatible_dc();
    test_create_compatible_dc_null_ctx();
    test_compatible_dc_drawing();
    test_create_compatible_bitmap();
    test_create_compatible_bitmap_invalid_size();
    test_create_bitmap();
    test_create_bitmap_null_bits();
    test_create_dib_bitmap();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

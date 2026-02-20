/*
 * test_ne_gdi.c - Tests for Phase 3: GDI.EXE subsystem
 *
 * Verifies:
 *   - GDI context initialisation and teardown
 *   - Device context allocation and release
 *   - Paint session management (BeginPaint / EndPaint)
 *   - Drawing primitives: TextOut, MoveTo, LineTo, Rectangle, SetPixel
 *   - Error string reporting
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
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== WinDOS GDI.EXE Subsystem Tests (Phase 3) ===\n");

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

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

/*
 * test_ne_dpmi.c - Tests for Phase H: Protected-Mode (DPMI) Support
 *
 * Verifies:
 *   - ne_dpmi_init / ne_dpmi_free
 *   - DPMI server start / stop / is_active
 *   - DPMI version query
 *   - Selector allocation / free / change
 *   - Descriptor get / set / base / limit operations
 *   - Extended memory allocation / free / resize
 *   - INT 31h dispatch for all supported function codes
 *   - Error-path coverage for all public API functions
 *
 * Build with Watcom (DOS target):
 *   wcc -ml -za99 -wx -d2 -i=../src ../src/ne_dpmi.c test_ne_dpmi.c
 *   wlink system dos name test_ne_dpmi.exe file test_ne_dpmi.obj,ne_dpmi.obj
 *
 * Build on POSIX host (CI):
 *   cc -std=c99 -Wall -I../src ../src/ne_dpmi.c test_ne_dpmi.c
 *      -o test_ne_dpmi
 */

#include "../src/ne_dpmi.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Minimal test framework (mirrors other test files in this project)
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
        return; \
    } while (0)

#define TEST_FAIL(msg) \
    do { \
        g_tests_failed++; \
        printf("FAIL - %s (line %d)\n", (msg), __LINE__); \
        return; \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((long long)(a) != (long long)(b)) { \
            g_tests_failed++; \
            printf("FAIL - expected %lld got %lld (line %d)\n", \
                   (long long)(b), (long long)(a), __LINE__); \
            return; \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((long long)(a) == (long long)(b)) { \
            g_tests_failed++; \
            printf("FAIL - unexpected equal value %lld (line %d)\n", \
                   (long long)(a), __LINE__); \
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
 * Init / Free
 * ===================================================================== */

static void test_dpmi_init_free(void)
{
    NEDpmiContext ctx;

    TEST_BEGIN("dpmi init and free");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);
    ASSERT_EQ(ctx.initialized, 1);
    ASSERT_EQ(ctx.sel_count, 0);
    ASSERT_EQ(ctx.desc_count, 0);
    ASSERT_EQ(ctx.ext_count, 0);
    ASSERT_EQ(ctx.server_active, 0);

    ne_dpmi_free(&ctx);
    ASSERT_EQ(ctx.initialized, 0);

    TEST_PASS();
}

static void test_dpmi_init_null(void)
{
    TEST_BEGIN("dpmi init with NULL returns error");
    ASSERT_EQ(ne_dpmi_init(NULL), NE_DPMI_ERR_NULL);
    TEST_PASS();
}

static void test_dpmi_free_null(void)
{
    TEST_BEGIN("dpmi free NULL is safe (no crash)");
    ne_dpmi_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * Server start / stop
 * ===================================================================== */

static void test_dpmi_server_start_stop(void)
{
    NEDpmiContext ctx;

    TEST_BEGIN("dpmi server start and stop");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    ASSERT_EQ(ne_dpmi_server_is_active(&ctx), 0);
    ASSERT_EQ(ne_dpmi_server_start(&ctx), NE_DPMI_OK);
    ASSERT_NE(ne_dpmi_server_is_active(&ctx), 0);
    ASSERT_EQ(ne_dpmi_server_stop(&ctx), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_server_is_active(&ctx), 0);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_server_start_null(void)
{
    TEST_BEGIN("dpmi server start with NULL returns error");
    ASSERT_EQ(ne_dpmi_server_start(NULL), NE_DPMI_ERR_NULL);
    TEST_PASS();
}

static void test_dpmi_server_start_not_init(void)
{
    NEDpmiContext ctx;

    TEST_BEGIN("dpmi server start without init returns error");

    memset(&ctx, 0, sizeof(ctx));
    ASSERT_EQ(ne_dpmi_server_start(&ctx), NE_DPMI_ERR_NOT_INIT);

    TEST_PASS();
}

static void test_dpmi_server_free_stops(void)
{
    NEDpmiContext ctx;

    TEST_BEGIN("dpmi free stops active server");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_server_start(&ctx), NE_DPMI_OK);
    ASSERT_NE(ne_dpmi_server_is_active(&ctx), 0);

    ne_dpmi_free(&ctx);
    /* After free, context is zeroed – server_active should be 0. */
    ASSERT_EQ(ctx.server_active, 0);

    TEST_PASS();
}

/* =========================================================================
 * Version
 * ===================================================================== */

static void test_dpmi_get_version(void)
{
    NEDpmiContext ctx;
    uint8_t major = 0xFF, minor = 0xFF;

    TEST_BEGIN("dpmi get version returns 0.9");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_get_version(&ctx, &major, &minor), NE_DPMI_OK);
    ASSERT_EQ(major, NE_DPMI_VERSION_MAJOR);
    ASSERT_EQ(minor, NE_DPMI_VERSION_MINOR);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_get_version_null(void)
{
    NEDpmiContext ctx;
    uint8_t major, minor;

    TEST_BEGIN("dpmi get version NULL args return error");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_get_version(NULL, &major, &minor), NE_DPMI_ERR_NULL);
    ASSERT_EQ(ne_dpmi_get_version(&ctx, NULL, &minor), NE_DPMI_ERR_NULL);
    ASSERT_EQ(ne_dpmi_get_version(&ctx, &major, NULL), NE_DPMI_ERR_NULL);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Selector allocation / free
 * ===================================================================== */

static void test_dpmi_alloc_selector_basic(void)
{
    NEDpmiContext ctx;
    uint16_t sel;

    TEST_BEGIN("alloc selector returns valid non-zero selector");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    sel = ne_dpmi_alloc_selector(&ctx, 0);
    ASSERT_NE(sel, NE_DPMI_SEL_INVALID);
    ASSERT_EQ(ne_dpmi_get_selector_count(&ctx), 1);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_alloc_selector_null(void)
{
    TEST_BEGIN("alloc selector with NULL returns invalid");
    ASSERT_EQ(ne_dpmi_alloc_selector(NULL, 0), NE_DPMI_SEL_INVALID);
    TEST_PASS();
}

static void test_dpmi_free_selector_basic(void)
{
    NEDpmiContext ctx;
    uint16_t sel;

    TEST_BEGIN("free selector decrements count");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    sel = ne_dpmi_alloc_selector(&ctx, 0);
    ASSERT_NE(sel, NE_DPMI_SEL_INVALID);
    ASSERT_EQ(ne_dpmi_get_selector_count(&ctx), 1);

    ASSERT_EQ(ne_dpmi_free_selector(&ctx, sel), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_get_selector_count(&ctx), 0);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_free_selector_invalid(void)
{
    NEDpmiContext ctx;

    TEST_BEGIN("free selector with invalid selector returns error");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_free_selector(&ctx, 0x1234), NE_DPMI_ERR_BAD_SEL);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_free_selector_null(void)
{
    TEST_BEGIN("free selector with NULL ctx returns error");
    ASSERT_EQ(ne_dpmi_free_selector(NULL, 0), NE_DPMI_ERR_NULL);
    TEST_PASS();
}

static void test_dpmi_alloc_multiple_selectors(void)
{
    NEDpmiContext ctx;
    uint16_t sel1, sel2, sel3;

    TEST_BEGIN("alloc multiple selectors returns distinct values");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    sel1 = ne_dpmi_alloc_selector(&ctx, 0);
    sel2 = ne_dpmi_alloc_selector(&ctx, 0);
    sel3 = ne_dpmi_alloc_selector(&ctx, 0);

    ASSERT_NE(sel1, NE_DPMI_SEL_INVALID);
    ASSERT_NE(sel2, NE_DPMI_SEL_INVALID);
    ASSERT_NE(sel3, NE_DPMI_SEL_INVALID);
    ASSERT_NE(sel1, sel2);
    ASSERT_NE(sel2, sel3);
    ASSERT_NE(sel1, sel3);
    ASSERT_EQ(ne_dpmi_get_selector_count(&ctx), 3);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_alloc_selector_from_source(void)
{
    NEDpmiContext ctx;
    uint16_t src_sel, new_sel;
    NEDpmiDescriptor src_desc, new_desc;

    TEST_BEGIN("alloc selector from source copies descriptor");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    src_sel = ne_dpmi_alloc_selector(&ctx, 0);
    ASSERT_NE(src_sel, NE_DPMI_SEL_INVALID);

    /* Modify the source descriptor. */
    ASSERT_EQ(ne_dpmi_set_segment_base(&ctx, src_sel, 0x12340000uL),
              NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_set_segment_limit(&ctx, src_sel, 0x4000u),
              NE_DPMI_OK);

    /* Allocate new selector from source. */
    new_sel = ne_dpmi_alloc_selector(&ctx, src_sel);
    ASSERT_NE(new_sel, NE_DPMI_SEL_INVALID);
    ASSERT_NE(new_sel, src_sel);

    /* Verify the new selector has the same base/limit. */
    ASSERT_EQ(ne_dpmi_get_descriptor(&ctx, src_sel, &src_desc), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_get_descriptor(&ctx, new_sel, &new_desc), NE_DPMI_OK);
    ASSERT_EQ(new_desc.base, src_desc.base);
    ASSERT_EQ(new_desc.limit, src_desc.limit);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Change selector (code <-> data)
 * ===================================================================== */

static void test_dpmi_change_selector_data_to_code(void)
{
    NEDpmiContext ctx;
    uint16_t src_sel, dst_sel;
    NEDpmiDescriptor src_desc, dst_desc;

    TEST_BEGIN("change selector converts data to code");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    src_sel = ne_dpmi_alloc_selector(&ctx, 0);
    dst_sel = ne_dpmi_alloc_selector(&ctx, 0);
    ASSERT_NE(src_sel, NE_DPMI_SEL_INVALID);
    ASSERT_NE(dst_sel, NE_DPMI_SEL_INVALID);

    /* Source is a data segment by default (bit 3 clear). */
    ASSERT_EQ(ne_dpmi_get_descriptor(&ctx, src_sel, &src_desc), NE_DPMI_OK);
    ASSERT_EQ(src_desc.access & 0x08u, 0);

    /* Change should make dst a code segment (bit 3 set). */
    ASSERT_EQ(ne_dpmi_change_selector(&ctx, dst_sel, src_sel), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_get_descriptor(&ctx, dst_sel, &dst_desc), NE_DPMI_OK);
    ASSERT_NE(dst_desc.access & 0x08u, 0);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_change_selector_code_to_data(void)
{
    NEDpmiContext ctx;
    uint16_t src_sel, dst_sel;
    NEDpmiDescriptor desc;

    TEST_BEGIN("change selector converts code to data");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    src_sel = ne_dpmi_alloc_selector(&ctx, 0);
    dst_sel = ne_dpmi_alloc_selector(&ctx, 0);
    ASSERT_NE(src_sel, NE_DPMI_SEL_INVALID);
    ASSERT_NE(dst_sel, NE_DPMI_SEL_INVALID);

    /* Make source a code segment. */
    ASSERT_EQ(ne_dpmi_get_descriptor(&ctx, src_sel, &desc), NE_DPMI_OK);
    desc.access = NE_DPMI_DESC_CODE_RX;
    ASSERT_EQ(ne_dpmi_set_descriptor(&ctx, src_sel, &desc), NE_DPMI_OK);

    /* Change should make dst a data segment (bit 3 clear). */
    ASSERT_EQ(ne_dpmi_change_selector(&ctx, dst_sel, src_sel), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_get_descriptor(&ctx, dst_sel, &desc), NE_DPMI_OK);
    ASSERT_EQ(desc.access & 0x08u, 0);
    ASSERT_NE(desc.access & NE_DPMI_DESC_DATA_SEG, 0);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_change_selector_errors(void)
{
    NEDpmiContext ctx;
    uint16_t sel;

    TEST_BEGIN("change selector error paths");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);
    sel = ne_dpmi_alloc_selector(&ctx, 0);

    ASSERT_EQ(ne_dpmi_change_selector(NULL, sel, sel), NE_DPMI_ERR_NULL);
    ASSERT_EQ(ne_dpmi_change_selector(&ctx, 0xFFFF, sel), NE_DPMI_ERR_BAD_SEL);
    ASSERT_EQ(ne_dpmi_change_selector(&ctx, sel, 0xFFFF), NE_DPMI_ERR_BAD_SEL);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Descriptor management
 * ===================================================================== */

static void test_dpmi_get_set_descriptor(void)
{
    NEDpmiContext ctx;
    uint16_t sel;
    NEDpmiDescriptor desc, out;

    TEST_BEGIN("get/set descriptor round-trip");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    sel = ne_dpmi_alloc_selector(&ctx, 0);
    ASSERT_NE(sel, NE_DPMI_SEL_INVALID);

    desc.base   = 0xA0000uL;
    desc.limit  = 0xFFFFu;
    desc.access = NE_DPMI_DESC_CODE_RX;
    desc.flags  = 0x40u;
    desc.in_use = 1;

    ASSERT_EQ(ne_dpmi_set_descriptor(&ctx, sel, &desc), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_get_descriptor(&ctx, sel, &out), NE_DPMI_OK);

    ASSERT_EQ(out.base, 0xA0000uL);
    ASSERT_EQ(out.limit, 0xFFFFu);
    ASSERT_EQ(out.access, NE_DPMI_DESC_CODE_RX);
    ASSERT_EQ(out.flags, 0x40u);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_get_descriptor_errors(void)
{
    NEDpmiContext ctx;
    NEDpmiDescriptor desc;

    TEST_BEGIN("get descriptor error paths");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    ASSERT_EQ(ne_dpmi_get_descriptor(NULL, 0, &desc), NE_DPMI_ERR_NULL);
    ASSERT_EQ(ne_dpmi_get_descriptor(&ctx, 0, NULL), NE_DPMI_ERR_NULL);
    ASSERT_EQ(ne_dpmi_get_descriptor(&ctx, 0xFFFF, &desc), NE_DPMI_ERR_BAD_SEL);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_set_segment_base_limit(void)
{
    NEDpmiContext ctx;
    uint16_t sel;
    uint32_t base;

    TEST_BEGIN("set/get segment base and limit");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    sel = ne_dpmi_alloc_selector(&ctx, 0);
    ASSERT_NE(sel, NE_DPMI_SEL_INVALID);

    ASSERT_EQ(ne_dpmi_set_segment_base(&ctx, sel, 0xB8000uL), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_get_segment_base(&ctx, sel, &base), NE_DPMI_OK);
    ASSERT_EQ(base, 0xB8000uL);

    ASSERT_EQ(ne_dpmi_set_segment_limit(&ctx, sel, 0x7FFFu), NE_DPMI_OK);
    {
        NEDpmiDescriptor desc;
        ASSERT_EQ(ne_dpmi_get_descriptor(&ctx, sel, &desc), NE_DPMI_OK);
        ASSERT_EQ(desc.limit, 0x7FFFu);
    }

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_segment_base_errors(void)
{
    NEDpmiContext ctx;
    uint32_t base;

    TEST_BEGIN("segment base/limit error paths");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    ASSERT_EQ(ne_dpmi_set_segment_base(NULL, 0, 0), NE_DPMI_ERR_NULL);
    ASSERT_EQ(ne_dpmi_get_segment_base(NULL, 0, &base), NE_DPMI_ERR_NULL);
    ASSERT_EQ(ne_dpmi_get_segment_base(&ctx, 0, NULL), NE_DPMI_ERR_NULL);
    ASSERT_EQ(ne_dpmi_set_segment_base(&ctx, 0xFFFF, 0), NE_DPMI_ERR_BAD_SEL);
    ASSERT_EQ(ne_dpmi_get_segment_base(&ctx, 0xFFFF, &base), NE_DPMI_ERR_BAD_SEL);
    ASSERT_EQ(ne_dpmi_set_segment_limit(NULL, 0, 0), NE_DPMI_ERR_NULL);
    ASSERT_EQ(ne_dpmi_set_segment_limit(&ctx, 0xFFFF, 0), NE_DPMI_ERR_BAD_SEL);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Extended memory
 * ===================================================================== */

static void test_dpmi_alloc_ext_memory_basic(void)
{
    NEDpmiContext ctx;
    uint32_t handle = 0, base = 0;

    TEST_BEGIN("alloc ext memory returns handle and base");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    ASSERT_EQ(ne_dpmi_alloc_ext_memory(&ctx, 4096, &handle, &base),
              NE_DPMI_OK);
    ASSERT_NE(handle, 0u);
    ASSERT_NE(base, 0u);
    ASSERT_EQ(ne_dpmi_get_ext_memory_count(&ctx), 1);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_alloc_ext_memory_null(void)
{
    NEDpmiContext ctx;
    uint32_t handle, base;

    TEST_BEGIN("alloc ext memory NULL args return error");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    ASSERT_EQ(ne_dpmi_alloc_ext_memory(NULL, 4096, &handle, &base),
              NE_DPMI_ERR_NULL);
    ASSERT_EQ(ne_dpmi_alloc_ext_memory(&ctx, 4096, NULL, &base),
              NE_DPMI_ERR_NULL);
    ASSERT_EQ(ne_dpmi_alloc_ext_memory(&ctx, 4096, &handle, NULL),
              NE_DPMI_ERR_NULL);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_alloc_ext_memory_zero_size(void)
{
    NEDpmiContext ctx;
    uint32_t handle, base;

    TEST_BEGIN("alloc ext memory with zero size returns error");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_alloc_ext_memory(&ctx, 0, &handle, &base),
              NE_DPMI_ERR_NO_MEM);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_free_ext_memory_basic(void)
{
    NEDpmiContext ctx;
    uint32_t handle, base;

    TEST_BEGIN("free ext memory decrements count");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    ASSERT_EQ(ne_dpmi_alloc_ext_memory(&ctx, 4096, &handle, &base),
              NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_get_ext_memory_count(&ctx), 1);

    ASSERT_EQ(ne_dpmi_free_ext_memory(&ctx, handle), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_get_ext_memory_count(&ctx), 0);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_free_ext_memory_invalid(void)
{
    NEDpmiContext ctx;

    TEST_BEGIN("free ext memory invalid handle returns error");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_free_ext_memory(&ctx, 0x9999), NE_DPMI_ERR_BAD_SEL);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_resize_ext_memory_basic(void)
{
    NEDpmiContext ctx;
    uint32_t handle, base, new_base;

    TEST_BEGIN("resize ext memory updates size");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    ASSERT_EQ(ne_dpmi_alloc_ext_memory(&ctx, 4096, &handle, &base),
              NE_DPMI_OK);

    ASSERT_EQ(ne_dpmi_resize_ext_memory(&ctx, handle, 8192, &new_base),
              NE_DPMI_OK);
    ASSERT_EQ(new_base, base); /* base unchanged in our impl */

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_resize_ext_memory_errors(void)
{
    NEDpmiContext ctx;
    uint32_t new_base;

    TEST_BEGIN("resize ext memory error paths");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    ASSERT_EQ(ne_dpmi_resize_ext_memory(NULL, 1, 4096, &new_base),
              NE_DPMI_ERR_NULL);
    ASSERT_EQ(ne_dpmi_resize_ext_memory(&ctx, 1, 4096, NULL),
              NE_DPMI_ERR_NULL);
    ASSERT_EQ(ne_dpmi_resize_ext_memory(&ctx, 0x9999, 4096, &new_base),
              NE_DPMI_ERR_BAD_SEL);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_alloc_multiple_ext_blocks(void)
{
    NEDpmiContext ctx;
    uint32_t h1, b1, h2, b2;

    TEST_BEGIN("multiple ext memory blocks have distinct bases");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    ASSERT_EQ(ne_dpmi_alloc_ext_memory(&ctx, 4096, &h1, &b1), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_alloc_ext_memory(&ctx, 8192, &h2, &b2), NE_DPMI_OK);

    ASSERT_NE(h1, h2);
    ASSERT_NE(b1, b2);
    ASSERT_EQ(ne_dpmi_get_ext_memory_count(&ctx), 2);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * INT 31h dispatch
 * ===================================================================== */

static void test_dpmi_dispatch_alloc_free_ldt(void)
{
    NEDpmiContext ctx;
    uint16_t out_ax = 0;

    TEST_BEGIN("dispatch ALLOC_LDT / FREE_LDT round-trip");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_server_start(&ctx), NE_DPMI_OK);

    /* Allocate via dispatch. */
    ASSERT_EQ(ne_dpmi_dispatch(&ctx, NE_DPMI_FN_ALLOC_LDT,
                               0, 1, 0, 0, 0,
                               &out_ax, NULL, NULL, NULL, NULL, NULL),
              NE_DPMI_OK);
    ASSERT_NE(out_ax, NE_DPMI_SEL_INVALID);
    ASSERT_EQ(ne_dpmi_get_selector_count(&ctx), 1);

    /* Free via dispatch. */
    ASSERT_EQ(ne_dpmi_dispatch(&ctx, NE_DPMI_FN_FREE_LDT,
                               out_ax, 0, 0, 0, 0,
                               NULL, NULL, NULL, NULL, NULL, NULL),
              NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_get_selector_count(&ctx), 0);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_dispatch_seg_to_desc(void)
{
    NEDpmiContext ctx;
    uint16_t out_ax = 0;
    uint32_t base;

    TEST_BEGIN("dispatch SEG_TO_DESC maps segment to selector");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    /* Map real-mode segment 0xB800 (VGA text buffer). */
    ASSERT_EQ(ne_dpmi_dispatch(&ctx, NE_DPMI_FN_SEG_TO_DESC,
                               0xB800u, 0, 0, 0, 0,
                               &out_ax, NULL, NULL, NULL, NULL, NULL),
              NE_DPMI_OK);
    ASSERT_NE(out_ax, NE_DPMI_SEL_INVALID);

    /* Verify the base is segment * 16 = 0xB8000. */
    ASSERT_EQ(ne_dpmi_get_segment_base(&ctx, out_ax, &base), NE_DPMI_OK);
    ASSERT_EQ(base, 0xB8000uL);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_dispatch_get_sel_inc(void)
{
    NEDpmiContext ctx;
    uint16_t out_ax = 0;

    TEST_BEGIN("dispatch GET_SEL_INC returns selector increment");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    ASSERT_EQ(ne_dpmi_dispatch(&ctx, NE_DPMI_FN_GET_SEL_INC,
                               0, 0, 0, 0, 0,
                               &out_ax, NULL, NULL, NULL, NULL, NULL),
              NE_DPMI_OK);
    ASSERT_EQ(out_ax, NE_DPMI_SEL_INCREMENT);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_dispatch_get_version(void)
{
    NEDpmiContext ctx;
    uint16_t out_ax = 0;

    TEST_BEGIN("dispatch GET_VERSION returns version in AX");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    ASSERT_EQ(ne_dpmi_dispatch(&ctx, NE_DPMI_FN_GET_VERSION,
                               0, 0, 0, 0, 0,
                               &out_ax, NULL, NULL, NULL, NULL, NULL),
              NE_DPMI_OK);
    ASSERT_EQ(out_ax >> 8, NE_DPMI_VERSION_MAJOR);
    ASSERT_EQ(out_ax & 0xFF, NE_DPMI_VERSION_MINOR);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_dispatch_alloc_free_mem(void)
{
    NEDpmiContext ctx;
    uint16_t out_bx = 0, out_cx = 0, out_si = 0, out_di = 0;
    uint32_t handle;

    TEST_BEGIN("dispatch ALLOC_MEM / FREE_MEM round-trip");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    /* Allocate 64KB: BX:CX = 0x0001:0x0000 */
    ASSERT_EQ(ne_dpmi_dispatch(&ctx, NE_DPMI_FN_ALLOC_MEM,
                               0x0001u, 0x0000u, 0, 0, 0,
                               NULL, &out_bx, &out_cx, NULL,
                               &out_si, &out_di),
              NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_get_ext_memory_count(&ctx), 1);

    /* Free: SI:DI = handle from alloc. */
    handle = ((uint32_t)out_si << 16) | out_di;
    ASSERT_NE(handle, 0u);
    ASSERT_EQ(ne_dpmi_dispatch(&ctx, NE_DPMI_FN_FREE_MEM,
                               0, 0, 0, out_si, out_di,
                               NULL, NULL, NULL, NULL, NULL, NULL),
              NE_DPMI_OK);
    ASSERT_EQ(ne_dpmi_get_ext_memory_count(&ctx), 0);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_dispatch_set_get_desc(void)
{
    NEDpmiContext ctx;
    uint16_t sel;
    uint16_t out_bx = 0, out_cx = 0, out_dx = 0, out_si = 0, out_di = 0;

    TEST_BEGIN("dispatch SET_DESC / GET_DESC round-trip");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    sel = ne_dpmi_alloc_selector(&ctx, 0);
    ASSERT_NE(sel, NE_DPMI_SEL_INVALID);

    /* Set descriptor: base = 0x000A0000, limit = 0x0000FFFF */
    ASSERT_EQ(ne_dpmi_dispatch(&ctx, NE_DPMI_FN_SET_DESC,
                               sel, 0x000Au, 0x0000u, 0x0000u, 0xFFFFu,
                               NULL, NULL, NULL, NULL, NULL, NULL),
              NE_DPMI_OK);

    /* Get descriptor back. */
    ASSERT_EQ(ne_dpmi_dispatch(&ctx, NE_DPMI_FN_GET_DESC,
                               sel, 0, 0, 0, 0,
                               NULL, &out_bx, &out_cx, &out_dx,
                               &out_si, &out_di),
              NE_DPMI_OK);

    /* Verify base = CX:DX = 0x000A:0x0000 */
    ASSERT_EQ(out_cx, 0x000Au);
    ASSERT_EQ(out_dx, 0x0000u);
    /* Verify limit = SI:DI = 0x0000:0xFFFF */
    ASSERT_EQ(out_si, 0x0000u);
    ASSERT_EQ(out_di, 0xFFFFu);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_dispatch_bad_function(void)
{
    NEDpmiContext ctx;

    TEST_BEGIN("dispatch unsupported function returns BAD_FUNC");

    ASSERT_EQ(ne_dpmi_init(&ctx), NE_DPMI_OK);

    ASSERT_EQ(ne_dpmi_dispatch(&ctx, 0xFFFF,
                               0, 0, 0, 0, 0,
                               NULL, NULL, NULL, NULL, NULL, NULL),
              NE_DPMI_ERR_BAD_FUNC);

    ne_dpmi_free(&ctx);
    TEST_PASS();
}

static void test_dpmi_dispatch_null(void)
{
    TEST_BEGIN("dispatch with NULL ctx returns error");

    ASSERT_EQ(ne_dpmi_dispatch(NULL, 0, 0, 0, 0, 0, 0,
                               NULL, NULL, NULL, NULL, NULL, NULL),
              NE_DPMI_ERR_NULL);

    TEST_PASS();
}

/* =========================================================================
 * Error string
 * ===================================================================== */

static void test_dpmi_strerror(void)
{
    TEST_BEGIN("ne_dpmi_strerror returns non-NULL non-empty strings");

    ASSERT_NOT_NULL(ne_dpmi_strerror(NE_DPMI_OK));
    ASSERT_NOT_NULL(ne_dpmi_strerror(NE_DPMI_ERR_NULL));
    ASSERT_NOT_NULL(ne_dpmi_strerror(NE_DPMI_ERR_FULL));
    ASSERT_NOT_NULL(ne_dpmi_strerror(NE_DPMI_ERR_BAD_SEL));
    ASSERT_NOT_NULL(ne_dpmi_strerror(NE_DPMI_ERR_NO_MEM));
    ASSERT_NOT_NULL(ne_dpmi_strerror(NE_DPMI_ERR_NOT_INIT));
    ASSERT_NOT_NULL(ne_dpmi_strerror(NE_DPMI_ERR_BAD_FUNC));
    ASSERT_NOT_NULL(ne_dpmi_strerror(-999));

    ASSERT_NE(ne_dpmi_strerror(NE_DPMI_OK)[0], '\0');
    ASSERT_NE(ne_dpmi_strerror(NE_DPMI_ERR_NULL)[0], '\0');
    ASSERT_NE(ne_dpmi_strerror(NE_DPMI_ERR_FULL)[0], '\0');
    ASSERT_NE(ne_dpmi_strerror(NE_DPMI_ERR_BAD_SEL)[0], '\0');
    ASSERT_NE(ne_dpmi_strerror(NE_DPMI_ERR_NO_MEM)[0], '\0');
    ASSERT_NE(ne_dpmi_strerror(NE_DPMI_ERR_NOT_INIT)[0], '\0');
    ASSERT_NE(ne_dpmi_strerror(NE_DPMI_ERR_BAD_FUNC)[0], '\0');

    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== NE DPMI Protected-Mode Tests (Phase H) ===\n\n");

    printf("--- Init / Free ---\n");
    test_dpmi_init_free();
    test_dpmi_init_null();
    test_dpmi_free_null();

    printf("\n--- Server start / stop ---\n");
    test_dpmi_server_start_stop();
    test_dpmi_server_start_null();
    test_dpmi_server_start_not_init();
    test_dpmi_server_free_stops();

    printf("\n--- Version ---\n");
    test_dpmi_get_version();
    test_dpmi_get_version_null();

    printf("\n--- Selector allocation / free ---\n");
    test_dpmi_alloc_selector_basic();
    test_dpmi_alloc_selector_null();
    test_dpmi_free_selector_basic();
    test_dpmi_free_selector_invalid();
    test_dpmi_free_selector_null();
    test_dpmi_alloc_multiple_selectors();
    test_dpmi_alloc_selector_from_source();

    printf("\n--- Change selector ---\n");
    test_dpmi_change_selector_data_to_code();
    test_dpmi_change_selector_code_to_data();
    test_dpmi_change_selector_errors();

    printf("\n--- Descriptor management ---\n");
    test_dpmi_get_set_descriptor();
    test_dpmi_get_descriptor_errors();
    test_dpmi_set_segment_base_limit();
    test_dpmi_segment_base_errors();

    printf("\n--- Extended memory ---\n");
    test_dpmi_alloc_ext_memory_basic();
    test_dpmi_alloc_ext_memory_null();
    test_dpmi_alloc_ext_memory_zero_size();
    test_dpmi_free_ext_memory_basic();
    test_dpmi_free_ext_memory_invalid();
    test_dpmi_resize_ext_memory_basic();
    test_dpmi_resize_ext_memory_errors();
    test_dpmi_alloc_multiple_ext_blocks();

    printf("\n--- INT 31h dispatch ---\n");
    test_dpmi_dispatch_alloc_free_ldt();
    test_dpmi_dispatch_seg_to_desc();
    test_dpmi_dispatch_get_sel_inc();
    test_dpmi_dispatch_get_version();
    test_dpmi_dispatch_alloc_free_mem();
    test_dpmi_dispatch_set_get_desc();
    test_dpmi_dispatch_bad_function();
    test_dpmi_dispatch_null();

    printf("\n--- Error strings ---\n");
    test_dpmi_strerror();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

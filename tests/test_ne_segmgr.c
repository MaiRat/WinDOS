/*
 * test_ne_segmgr.c - Tests for Phase 5: Segment Manager
 *
 * Verifies:
 *   - Segment manager context initialisation and teardown
 *   - Segment registration
 *   - Discardable segment eviction
 *   - Demand-reload from file image
 *   - Segment locking (prevents eviction)
 *   - Movable segment compaction
 *   - Error handling and error strings
 */

#include "../src/ne_segmgr.h"
#include "../src/ne_parser.h"   /* NE_SEG_DISCARDABLE, NE_SEG_MOVABLE */

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

static void test_segmgr_init_free(void)
{
    NESegMgrContext ctx;
    TEST_BEGIN("init sets initialized=1, free sets it to 0");
    ASSERT_EQ(ne_segmgr_init(&ctx, 16, NULL, 0), NE_SEGMGR_OK);
    ASSERT_EQ(ctx.initialized, 1);
    ASSERT_EQ(ctx.capacity, 16);
    ASSERT_EQ(ctx.count, 0);
    ne_segmgr_free(&ctx);
    ASSERT_EQ(ctx.initialized, 0);
    TEST_PASS();
}

static void test_segmgr_init_null(void)
{
    TEST_BEGIN("init with NULL ctx returns ERR_NULL");
    ASSERT_EQ(ne_segmgr_init(NULL, 16, NULL, 0), NE_SEGMGR_ERR_NULL);
    TEST_PASS();
}

static void test_segmgr_init_zero_cap(void)
{
    NESegMgrContext ctx;
    TEST_BEGIN("init with zero capacity returns ERR_NULL");
    ASSERT_EQ(ne_segmgr_init(&ctx, 0, NULL, 0), NE_SEGMGR_ERR_NULL);
    TEST_PASS();
}

static void test_segmgr_free_null_safe(void)
{
    TEST_BEGIN("free(NULL) does not crash");
    ne_segmgr_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * Segment registration tests
 * ===================================================================== */

static void test_segmgr_add_segment(void)
{
    NESegMgrContext ctx;
    NESegHandle     h;
    uint8_t        *data;
    TEST_BEGIN("add_segment returns valid handle");
    ne_segmgr_init(&ctx, 8, NULL, 0);
    data = (uint8_t *)malloc(64);
    ASSERT_NOT_NULL(data);
    memset(data, 0xAA, 64);
    h = ne_segmgr_add_segment(&ctx, NE_SEG_MOVABLE, data, 64, 0, 0);
    ASSERT_NE(h, NE_SEGMGR_HANDLE_INVALID);
    ASSERT_EQ(ctx.count, 1);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_add_multiple(void)
{
    NESegMgrContext ctx;
    NESegHandle     h1, h2, h3;
    uint8_t        *d1, *d2, *d3;
    TEST_BEGIN("add three segments; count == 3");
    ne_segmgr_init(&ctx, 8, NULL, 0);
    d1 = (uint8_t *)malloc(32);
    d2 = (uint8_t *)malloc(32);
    d3 = (uint8_t *)malloc(32);
    memset(d1, 1, 32); memset(d2, 2, 32); memset(d3, 3, 32);
    h1 = ne_segmgr_add_segment(&ctx, 0, d1, 32, 0, 0);
    h2 = ne_segmgr_add_segment(&ctx, 0, d2, 32, 0, 0);
    h3 = ne_segmgr_add_segment(&ctx, 0, d3, 32, 0, 0);
    ASSERT_NE(h1, NE_SEGMGR_HANDLE_INVALID);
    ASSERT_NE(h2, NE_SEGMGR_HANDLE_INVALID);
    ASSERT_NE(h3, NE_SEGMGR_HANDLE_INVALID);
    ASSERT_NE(h1, h2);
    ASSERT_NE(h2, h3);
    ASSERT_EQ(ctx.count, 3);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_add_table_full(void)
{
    NESegMgrContext ctx;
    NESegHandle     h;
    uint8_t        *d;
    int             i;
    TEST_BEGIN("add_segment returns INVALID when table is full");
    ne_segmgr_init(&ctx, 4, NULL, 0);
    for (i = 0; i < 4; i++) {
        d = (uint8_t *)malloc(8);
        memset(d, (int)(uint8_t)i, 8);
        h = ne_segmgr_add_segment(&ctx, 0, d, 8, 0, 0);
        ASSERT_NE(h, NE_SEGMGR_HANDLE_INVALID);
    }
    d = (uint8_t *)malloc(8);
    memset(d, 0, 8);
    h = ne_segmgr_add_segment(&ctx, 0, d, 8, 0, 0);
    ASSERT_EQ(h, NE_SEGMGR_HANDLE_INVALID);
    free(d);  /* not owned by manager because add failed */
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_find(void)
{
    NESegMgrContext ctx;
    NESegHandle     h;
    NESegEntry     *e;
    uint8_t        *data;
    TEST_BEGIN("find returns correct entry after add");
    ne_segmgr_init(&ctx, 8, NULL, 0);
    data = (uint8_t *)malloc(16);
    memset(data, 0x55, 16);
    h = ne_segmgr_add_segment(&ctx, NE_SEG_DISCARDABLE, data, 16, 100, 16);
    ASSERT_NE(h, NE_SEGMGR_HANDLE_INVALID);
    e = ne_segmgr_find(&ctx, h);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->handle, h);
    ASSERT_EQ(e->file_off, 100u);
    ASSERT_EQ(e->file_size, 16u);
    ASSERT_EQ(e->alloc_size, 16u);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_find_invalid(void)
{
    NESegMgrContext ctx;
    TEST_BEGIN("find with invalid handle returns NULL");
    ne_segmgr_init(&ctx, 8, NULL, 0);
    ASSERT_NULL(ne_segmgr_find(&ctx, NE_SEGMGR_HANDLE_INVALID));
    ASSERT_NULL(ne_segmgr_find(&ctx, 99));
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Eviction tests
 * ===================================================================== */

static void test_segmgr_evict_basic(void)
{
    NESegMgrContext ctx;
    NESegHandle     h;
    NESegEntry     *e;
    uint8_t        *data;
    TEST_BEGIN("evict discardable segment frees data and marks evicted");
    ne_segmgr_init(&ctx, 8, NULL, 0);
    data = (uint8_t *)malloc(32);
    memset(data, 0xFF, 32);
    h = ne_segmgr_add_segment(&ctx, NE_SEG_DISCARDABLE, data, 32, 0, 32);
    ASSERT_NE(h, NE_SEGMGR_HANDLE_INVALID);
    ASSERT_EQ(ne_segmgr_evict(&ctx, h), NE_SEGMGR_OK);
    e = ne_segmgr_find(&ctx, h);
    ASSERT_NOT_NULL(e);
    ASSERT_NULL(e->data);
    ASSERT_EQ((int)(e->state & NE_SEG_STATE_EVICTED), (int)NE_SEG_STATE_EVICTED);
    ASSERT_EQ((int)(e->state & NE_SEG_STATE_LOADED), 0);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_evict_non_discardable(void)
{
    NESegMgrContext ctx;
    NESegHandle     h;
    uint8_t        *data;
    TEST_BEGIN("evict non-discardable segment returns ERR_NOT_DISC");
    ne_segmgr_init(&ctx, 8, NULL, 0);
    data = (uint8_t *)malloc(16);
    memset(data, 0, 16);
    h = ne_segmgr_add_segment(&ctx, 0 /* no DISCARDABLE */, data, 16, 0, 0);
    ASSERT_NE(h, NE_SEGMGR_HANDLE_INVALID);
    ASSERT_EQ(ne_segmgr_evict(&ctx, h), NE_SEGMGR_ERR_NOT_DISC);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_evict_locked(void)
{
    NESegMgrContext ctx;
    NESegHandle     h;
    void           *ptr;
    uint8_t        *data;
    TEST_BEGIN("evict locked segment returns ERR_LOCKED");
    ne_segmgr_init(&ctx, 8, NULL, 0);
    data = (uint8_t *)malloc(16);
    memset(data, 0, 16);
    h = ne_segmgr_add_segment(&ctx, NE_SEG_DISCARDABLE, data, 16, 0, 0);
    ptr = ne_segmgr_lock(&ctx, h);
    ASSERT_NOT_NULL(ptr);
    ASSERT_EQ(ne_segmgr_evict(&ctx, h), NE_SEGMGR_ERR_LOCKED);
    ne_segmgr_unlock(&ctx, h);
    ASSERT_EQ(ne_segmgr_evict(&ctx, h), NE_SEGMGR_OK);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_evict_bad_handle(void)
{
    NESegMgrContext ctx;
    TEST_BEGIN("evict with invalid handle returns appropriate error");
    ne_segmgr_init(&ctx, 8, NULL, 0);
    ASSERT_EQ(ne_segmgr_evict(&ctx, NE_SEGMGR_HANDLE_INVALID),
              NE_SEGMGR_ERR_BAD_HANDLE);
    ASSERT_EQ(ne_segmgr_evict(&ctx, 42), NE_SEGMGR_ERR_NOT_FOUND);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Demand-reload tests
 * ===================================================================== */

static void test_segmgr_reload_basic(void)
{
    /* Build a minimal fake file image: 16 bytes of payload at offset 8 */
    static const uint8_t fake_file[24] = {
        0,1,2,3,4,5,6,7,               /* 8 bytes header padding */
        0x11,0x22,0x33,0x44,            /* payload bytes 8..23 */
        0x55,0x66,0x77,0x88,
        0x99,0xAA,0xBB,0xCC,
        0xDD,0xEE,0xFF,0x00
    };
    NESegMgrContext ctx;
    NESegHandle     h;
    NESegEntry     *e;
    uint8_t        *data;

    TEST_BEGIN("reload re-loads evicted segment from file image");
    ne_segmgr_init(&ctx, 8, fake_file, sizeof(fake_file));

    data = (uint8_t *)malloc(16);
    memcpy(data, fake_file + 8, 16);
    h = ne_segmgr_add_segment(&ctx, NE_SEG_DISCARDABLE, data, 16, 8, 16);
    ASSERT_NE(h, NE_SEGMGR_HANDLE_INVALID);

    ASSERT_EQ(ne_segmgr_evict(&ctx, h), NE_SEGMGR_OK);
    ASSERT_EQ(ne_segmgr_reload(&ctx, h), NE_SEGMGR_OK);

    e = ne_segmgr_find(&ctx, h);
    ASSERT_NOT_NULL(e);
    ASSERT_NOT_NULL(e->data);
    ASSERT_EQ((int)(e->state & NE_SEG_STATE_LOADED), (int)NE_SEG_STATE_LOADED);
    ASSERT_EQ((int)(e->state & NE_SEG_STATE_EVICTED), 0);
    ASSERT_EQ(e->data[0], 0x11);
    ASSERT_EQ(e->data[15], 0x00);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_reload_already_loaded(void)
{
    NESegMgrContext ctx;
    NESegHandle     h;
    uint8_t        *data;
    TEST_BEGIN("reload of loaded segment returns ERR_LOADED");
    ne_segmgr_init(&ctx, 8, NULL, 0);
    data = (uint8_t *)malloc(8);
    memset(data, 0, 8);
    h = ne_segmgr_add_segment(&ctx, NE_SEG_DISCARDABLE, data, 8, 0, 0);
    ASSERT_NE(h, NE_SEGMGR_HANDLE_INVALID);
    ASSERT_EQ(ne_segmgr_reload(&ctx, h), NE_SEGMGR_ERR_LOADED);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_reload_no_file(void)
{
    NESegMgrContext ctx;
    NESegHandle     h;
    uint8_t        *data;
    TEST_BEGIN("reload without file image returns ERR_NO_FILE");
    ne_segmgr_init(&ctx, 8, NULL, 0);
    data = (uint8_t *)malloc(8);
    memset(data, 0, 8);
    /* file_size > 0 so reload will need the file */
    h = ne_segmgr_add_segment(&ctx, NE_SEG_DISCARDABLE, data, 8, 0, 8);
    ne_segmgr_evict(&ctx, h);
    ASSERT_EQ(ne_segmgr_reload(&ctx, h), NE_SEGMGR_ERR_NO_FILE);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_reload_io_out_of_bounds(void)
{
    static const uint8_t tiny_file[4] = { 0xAA, 0xBB, 0xCC, 0xDD };
    NESegMgrContext ctx;
    NESegHandle     h;
    uint8_t        *data;
    TEST_BEGIN("reload with out-of-bounds offset returns ERR_IO");
    ne_segmgr_init(&ctx, 8, tiny_file, sizeof(tiny_file));
    data = (uint8_t *)malloc(8);
    memset(data, 0, 8);
    /* file_off 2, file_size 8: extends beyond tiny_file */
    h = ne_segmgr_add_segment(&ctx, NE_SEG_DISCARDABLE, data, 8, 2, 8);
    ne_segmgr_evict(&ctx, h);
    ASSERT_EQ(ne_segmgr_reload(&ctx, h), NE_SEGMGR_ERR_IO);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Lock / unlock tests
 * ===================================================================== */

static void test_segmgr_lock_unlock(void)
{
    NESegMgrContext ctx;
    NESegHandle     h;
    void           *ptr;
    NESegEntry     *e;
    uint8_t        *data;
    TEST_BEGIN("lock increments count; unlock decrements it");
    ne_segmgr_init(&ctx, 8, NULL, 0);
    data = (uint8_t *)malloc(8);
    memset(data, 0x42, 8);
    h = ne_segmgr_add_segment(&ctx, 0, data, 8, 0, 0);

    ptr = ne_segmgr_lock(&ctx, h);
    ASSERT_NOT_NULL(ptr);
    ASSERT_EQ(((uint8_t *)ptr)[0], 0x42);

    e = ne_segmgr_find(&ctx, h);
    ASSERT_EQ(e->lock_count, 1);

    ne_segmgr_lock(&ctx, h);
    ASSERT_EQ(e->lock_count, 2);

    ne_segmgr_unlock(&ctx, h);
    ASSERT_EQ(e->lock_count, 1);

    ne_segmgr_unlock(&ctx, h);
    ASSERT_EQ(e->lock_count, 0);

    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_lock_evicted(void)
{
    NESegMgrContext ctx;
    NESegHandle     h;
    uint8_t        *data;
    TEST_BEGIN("lock of evicted segment returns NULL");
    ne_segmgr_init(&ctx, 8, NULL, 0);
    data = (uint8_t *)malloc(8);
    memset(data, 0, 8);
    h = ne_segmgr_add_segment(&ctx, NE_SEG_DISCARDABLE, data, 8, 0, 0);
    ne_segmgr_evict(&ctx, h);
    ASSERT_NULL(ne_segmgr_lock(&ctx, h));
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_unlock_bad_handle(void)
{
    NESegMgrContext ctx;
    TEST_BEGIN("unlock with invalid handle returns appropriate error");
    ne_segmgr_init(&ctx, 8, NULL, 0);
    ASSERT_EQ(ne_segmgr_unlock(&ctx, NE_SEGMGR_HANDLE_INVALID),
              NE_SEGMGR_ERR_BAD_HANDLE);
    ASSERT_EQ(ne_segmgr_unlock(&ctx, 99), NE_SEGMGR_ERR_NOT_FOUND);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Compaction tests
 * ===================================================================== */

static void test_segmgr_compact_basic(void)
{
    NESegMgrContext ctx;
    NESegHandle     h1, h2;
    NESegEntry     *e1, *e2;
    uint8_t        *d1, *d2;
    void           *old1, *old2;
    int             compacted;
    TEST_BEGIN("compact returns count of movable segments processed");
    ne_segmgr_init(&ctx, 8, NULL, 0);

    d1 = (uint8_t *)malloc(16);
    d2 = (uint8_t *)malloc(16);
    memset(d1, 0xAA, 16);
    memset(d2, 0xBB, 16);

    h1 = ne_segmgr_add_segment(&ctx, NE_SEG_MOVABLE, d1, 16, 0, 0);
    h2 = ne_segmgr_add_segment(&ctx, NE_SEG_MOVABLE, d2, 16, 0, 0);

    e1 = ne_segmgr_find(&ctx, h1);
    e2 = ne_segmgr_find(&ctx, h2);
    old1 = e1->data;
    old2 = e2->data;

    compacted = ne_segmgr_compact(&ctx);
    ASSERT_EQ(compacted, 2);

    /* Data must be preserved */
    ASSERT_EQ(e1->data[0], 0xAA);
    ASSERT_EQ(e2->data[0], 0xBB);

    /* Pointers may have changed (host malloc returns a new buffer) */
    (void)old1; (void)old2;

    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_compact_skips_locked(void)
{
    NESegMgrContext ctx;
    NESegHandle     h;
    NESegEntry     *e;
    uint8_t        *data;
    void           *orig;
    int             compacted;
    TEST_BEGIN("compact skips locked segments");
    ne_segmgr_init(&ctx, 8, NULL, 0);

    data = (uint8_t *)malloc(16);
    memset(data, 0xCC, 16);
    h = ne_segmgr_add_segment(&ctx, NE_SEG_MOVABLE, data, 16, 0, 0);
    e    = ne_segmgr_find(&ctx, h);
    orig = e->data;

    ne_segmgr_lock(&ctx, h);
    compacted = ne_segmgr_compact(&ctx);
    ASSERT_EQ(compacted, 0);
    ASSERT_EQ(e->data, orig);  /* pointer unchanged for locked segment */

    ne_segmgr_unlock(&ctx, h);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_compact_skips_fixed(void)
{
    NESegMgrContext ctx;
    NESegHandle     h;
    uint8_t        *data;
    int             compacted;
    TEST_BEGIN("compact skips non-movable segments");
    ne_segmgr_init(&ctx, 8, NULL, 0);
    data = (uint8_t *)malloc(8);
    memset(data, 0, 8);
    /* no NE_SEG_MOVABLE flag */
    h = ne_segmgr_add_segment(&ctx, 0, data, 8, 0, 0);
    (void)h;
    compacted = ne_segmgr_compact(&ctx);
    ASSERT_EQ(compacted, 0);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

static void test_segmgr_compact_null(void)
{
    TEST_BEGIN("compact with NULL ctx returns ERR_NULL");
    ASSERT_EQ(ne_segmgr_compact(NULL), NE_SEGMGR_ERR_NULL);
    TEST_PASS();
}

/* =========================================================================
 * NULL argument safety
 * ===================================================================== */

static void test_segmgr_null_args(void)
{
    NESegMgrContext ctx;
    TEST_BEGIN("NULL arg checks for all public functions");
    ASSERT_EQ(ne_segmgr_init(NULL, 8, NULL, 0),          NE_SEGMGR_ERR_NULL);
    ASSERT_EQ(ne_segmgr_evict(NULL, 1),                   NE_SEGMGR_ERR_NULL);
    ASSERT_EQ(ne_segmgr_reload(NULL, 1),                  NE_SEGMGR_ERR_NULL);
    ASSERT_EQ(ne_segmgr_unlock(NULL, 1),                  NE_SEGMGR_ERR_NULL);
    ASSERT_EQ(ne_segmgr_compact(NULL),                    NE_SEGMGR_ERR_NULL);
    ASSERT_NULL(ne_segmgr_find(NULL, 1));
    ASSERT_NULL(ne_segmgr_lock(NULL, 1));

    ne_segmgr_init(&ctx, 8, NULL, 0);
    ASSERT_EQ(ne_segmgr_add_segment(NULL, 0, NULL, 0, 0, 0),
              NE_SEGMGR_HANDLE_INVALID);
    ne_segmgr_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Error string test
 * ===================================================================== */

static void test_segmgr_strerror(void)
{
    TEST_BEGIN("strerror returns non-NULL for all known codes");
    ASSERT_NOT_NULL(ne_segmgr_strerror(NE_SEGMGR_OK));
    ASSERT_NOT_NULL(ne_segmgr_strerror(NE_SEGMGR_ERR_NULL));
    ASSERT_NOT_NULL(ne_segmgr_strerror(NE_SEGMGR_ERR_ALLOC));
    ASSERT_NOT_NULL(ne_segmgr_strerror(NE_SEGMGR_ERR_FULL));
    ASSERT_NOT_NULL(ne_segmgr_strerror(NE_SEGMGR_ERR_NOT_FOUND));
    ASSERT_NOT_NULL(ne_segmgr_strerror(NE_SEGMGR_ERR_BAD_HANDLE));
    ASSERT_NOT_NULL(ne_segmgr_strerror(NE_SEGMGR_ERR_LOCKED));
    ASSERT_NOT_NULL(ne_segmgr_strerror(NE_SEGMGR_ERR_NOT_DISC));
    ASSERT_NOT_NULL(ne_segmgr_strerror(NE_SEGMGR_ERR_LOADED));
    ASSERT_NOT_NULL(ne_segmgr_strerror(NE_SEGMGR_ERR_NO_FILE));
    ASSERT_NOT_NULL(ne_segmgr_strerror(NE_SEGMGR_ERR_IO));
    ASSERT_NOT_NULL(ne_segmgr_strerror(-99));
    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== WinDOS Segment Manager Tests (Phase 5) ===\n");

    printf("\n--- Context tests ---\n");
    test_segmgr_init_free();
    test_segmgr_init_null();
    test_segmgr_init_zero_cap();
    test_segmgr_free_null_safe();

    printf("\n--- Segment registration tests ---\n");
    test_segmgr_add_segment();
    test_segmgr_add_multiple();
    test_segmgr_add_table_full();
    test_segmgr_find();
    test_segmgr_find_invalid();

    printf("\n--- Eviction tests ---\n");
    test_segmgr_evict_basic();
    test_segmgr_evict_non_discardable();
    test_segmgr_evict_locked();
    test_segmgr_evict_bad_handle();

    printf("\n--- Demand-reload tests ---\n");
    test_segmgr_reload_basic();
    test_segmgr_reload_already_loaded();
    test_segmgr_reload_no_file();
    test_segmgr_reload_io_out_of_bounds();

    printf("\n--- Lock / unlock tests ---\n");
    test_segmgr_lock_unlock();
    test_segmgr_lock_evicted();
    test_segmgr_unlock_bad_handle();

    printf("\n--- Compaction tests ---\n");
    test_segmgr_compact_basic();
    test_segmgr_compact_skips_locked();
    test_segmgr_compact_skips_fixed();
    test_segmgr_compact_null();

    printf("\n--- NULL argument safety ---\n");
    test_segmgr_null_args();

    printf("\n--- Error strings ---\n");
    test_segmgr_strerror();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

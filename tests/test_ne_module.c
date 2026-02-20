/*
 * test_ne_module.c - Tests for the NE module table (Step 4)
 *
 * Verifies load, duplicate-load detection, reference counting,
 * dependency tracking, and unload bookkeeping correctness.
 *
 * Build with:
 *   gcc -std=c99 -Wall -Wextra -I../src ../src/ne_parser.c \
 *       ../src/ne_loader.c ../src/ne_module.c \
 *       test_ne_module.c -o test_ne_module
 */

#include "../src/ne_parser.h"
#include "../src/ne_loader.h"
#include "../src/ne_module.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Minimal test framework (mirrors test_ne_reloc.c)
 * ---------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_BEGIN(name) \
    do { \
        g_tests_run++; \
        printf("  %-60s ", (name)); \
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
            printf("FAIL - expected %lld got %lld (line %d)\n", \
                   (long long)(b), (long long)(a), __LINE__); \
            return; \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
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

/* -------------------------------------------------------------------------
 * Helper: initialise an empty NEParserContext and NELoaderContext.
 *
 * These "blank" contexts have no heap allocations and are safe to pass to
 * ne_mod_load() because ne_free() and ne_loader_free() are no-ops on
 * zeroed contexts.
 * ---------------------------------------------------------------------- */
static void make_blank_contexts(NEParserContext *parser,
                                NELoaderContext *loader)
{
    memset(parser, 0, sizeof(*parser));
    memset(loader, 0, sizeof(*loader));
}

/* =========================================================================
 * Test cases
 * ===================================================================== */

/* -------------------------------------------------------------------------
 * init / free
 * ---------------------------------------------------------------------- */

static void test_table_init_free(void)
{
    NEModuleTable tbl;
    int rc;

    TEST_BEGIN("table init and free (default capacity)");

    rc = ne_mod_table_init(&tbl, NE_MOD_TABLE_CAP);
    ASSERT_EQ(rc, NE_MOD_OK);
    ASSERT_NOT_NULL(tbl.entries);
    ASSERT_EQ(tbl.capacity,    (uint16_t)NE_MOD_TABLE_CAP);
    ASSERT_EQ(tbl.count,       (uint16_t)0);
    ASSERT_EQ(tbl.next_handle, (uint16_t)1);

    ne_mod_table_free(&tbl);
    /* After free the table is zeroed */
    ASSERT_EQ(tbl.entries,     NULL);
    ASSERT_EQ(tbl.capacity,    (uint16_t)0);

    TEST_PASS();
}

static void test_table_init_null(void)
{
    TEST_BEGIN("table init with NULL pointer returns error");
    ASSERT_EQ(ne_mod_table_init(NULL, 4), NE_MOD_ERR_NULL);
    TEST_PASS();
}

static void test_table_init_zero_capacity(void)
{
    NEModuleTable tbl;
    TEST_BEGIN("table init with zero capacity returns error");
    ASSERT_EQ(ne_mod_table_init(&tbl, 0), NE_MOD_ERR_NULL);
    TEST_PASS();
}

static void test_table_free_null(void)
{
    TEST_BEGIN("table free on NULL is safe");
    ne_mod_table_free(NULL); /* must not crash */
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * ne_mod_load – basic registration
 * ---------------------------------------------------------------------- */

static void test_load_single_module(void)
{
    NEModuleTable   tbl;
    NEParserContext parser;
    NELoaderContext loader;
    NEModuleHandle  h = NE_MOD_HANDLE_INVALID;
    NEModuleEntry  *e;
    int rc;

    TEST_BEGIN("load single module assigns valid handle");

    ASSERT_EQ(ne_mod_table_init(&tbl, 8), NE_MOD_OK);
    make_blank_contexts(&parser, &loader);

    rc = ne_mod_load(&tbl, "KERNEL", &parser, &loader, &h);
    ASSERT_EQ(rc, NE_MOD_OK);
    ASSERT_NE((long long)h, (long long)NE_MOD_HANDLE_INVALID);
    ASSERT_EQ(tbl.count, (uint16_t)1);

    e = ne_mod_get(&tbl, h);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->ref_count, (uint16_t)1);
    ASSERT_EQ((int)(e->name[0]), (int)'K');

    ne_mod_table_free(&tbl);
    TEST_PASS();
}

static void test_load_multiple_distinct_modules(void)
{
    NEModuleTable   tbl;
    NEParserContext p1, p2;
    NELoaderContext l1, l2;
    NEModuleHandle  h1, h2;
    int rc;

    TEST_BEGIN("load two distinct modules gets different handles");

    ASSERT_EQ(ne_mod_table_init(&tbl, 8), NE_MOD_OK);
    make_blank_contexts(&p1, &l1);
    make_blank_contexts(&p2, &l2);

    rc = ne_mod_load(&tbl, "KERNEL", &p1, &l1, &h1);
    ASSERT_EQ(rc, NE_MOD_OK);
    rc = ne_mod_load(&tbl, "USER", &p2, &l2, &h2);
    ASSERT_EQ(rc, NE_MOD_OK);

    ASSERT_NE((long long)h1, (long long)NE_MOD_HANDLE_INVALID);
    ASSERT_NE((long long)h2, (long long)NE_MOD_HANDLE_INVALID);
    ASSERT_NE((long long)h1, (long long)h2);
    ASSERT_EQ(tbl.count, (uint16_t)2);

    ne_mod_table_free(&tbl);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * ne_mod_load – duplicate detection
 * ---------------------------------------------------------------------- */

static void test_load_duplicate_returns_same_handle(void)
{
    NEModuleTable   tbl;
    NEParserContext p1, p2;
    NELoaderContext l1, l2;
    NEModuleHandle  h1, h2;
    NEModuleEntry  *e;
    int rc;

    TEST_BEGIN("duplicate load returns existing handle and bumps ref count");

    ASSERT_EQ(ne_mod_table_init(&tbl, 8), NE_MOD_OK);
    make_blank_contexts(&p1, &l1);
    make_blank_contexts(&p2, &l2);

    rc = ne_mod_load(&tbl, "KERNEL", &p1, &l1, &h1);
    ASSERT_EQ(rc, NE_MOD_OK);

    /* Second load of same name: should return h1, ref_count becomes 2 */
    rc = ne_mod_load(&tbl, "KERNEL", &p2, &l2, &h2);
    ASSERT_EQ(rc, NE_MOD_OK);
    ASSERT_EQ((long long)h1, (long long)h2);
    ASSERT_EQ(tbl.count, (uint16_t)1);  /* still only one entry */

    e = ne_mod_get(&tbl, h1);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->ref_count, (uint16_t)2);

    /* Caller owns p2/l2 — free them explicitly */
    ne_free(&p2);
    ne_loader_free(&l2);

    ne_mod_table_free(&tbl);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * ne_mod_addref
 * ---------------------------------------------------------------------- */

static void test_addref_increments_count(void)
{
    NEModuleTable   tbl;
    NEParserContext p;
    NELoaderContext l;
    NEModuleHandle  h;
    NEModuleEntry  *e;

    TEST_BEGIN("addref increments reference count");

    ASSERT_EQ(ne_mod_table_init(&tbl, 8), NE_MOD_OK);
    make_blank_contexts(&p, &l);
    ASSERT_EQ(ne_mod_load(&tbl, "GDI", &p, &l, &h), NE_MOD_OK);

    ASSERT_EQ(ne_mod_addref(&tbl, h), NE_MOD_OK);
    e = ne_mod_get(&tbl, h);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->ref_count, (uint16_t)2);

    ne_mod_table_free(&tbl);
    TEST_PASS();
}

static void test_addref_invalid_handle(void)
{
    NEModuleTable tbl;
    TEST_BEGIN("addref with invalid handle returns error");
    ASSERT_EQ(ne_mod_table_init(&tbl, 4), NE_MOD_OK);
    ASSERT_EQ(ne_mod_addref(&tbl, NE_MOD_HANDLE_INVALID), NE_MOD_ERR_BAD_HANDLE);
    ne_mod_table_free(&tbl);
    TEST_PASS();
}

static void test_addref_not_found(void)
{
    NEModuleTable tbl;
    TEST_BEGIN("addref with unknown handle returns NE_MOD_ERR_NOT_FOUND");
    ASSERT_EQ(ne_mod_table_init(&tbl, 4), NE_MOD_OK);
    ASSERT_EQ(ne_mod_addref(&tbl, (NEModuleHandle)99), NE_MOD_ERR_NOT_FOUND);
    ne_mod_table_free(&tbl);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * ne_mod_unload – reference counting
 * ---------------------------------------------------------------------- */

static void test_unload_decrements_and_removes(void)
{
    NEModuleTable   tbl;
    NEParserContext p;
    NELoaderContext l;
    NEModuleHandle  h;
    int rc;

    TEST_BEGIN("unload with ref_count 1 removes entry");

    ASSERT_EQ(ne_mod_table_init(&tbl, 8), NE_MOD_OK);
    make_blank_contexts(&p, &l);
    ASSERT_EQ(ne_mod_load(&tbl, "MYMOD", &p, &l, &h), NE_MOD_OK);
    ASSERT_EQ(tbl.count, (uint16_t)1);

    rc = ne_mod_unload(&tbl, h);
    ASSERT_EQ(rc, NE_MOD_OK);
    ASSERT_EQ(tbl.count, (uint16_t)0);
    ASSERT_EQ((long long)ne_mod_get(&tbl, h), (long long)NULL);

    ne_mod_table_free(&tbl);
    TEST_PASS();
}

static void test_unload_decrements_only_when_refs_remain(void)
{
    NEModuleTable   tbl;
    NEParserContext p1, p2;
    NELoaderContext l1, l2;
    NEModuleHandle  h1, h2;
    NEModuleEntry  *e;

    TEST_BEGIN("unload with ref_count > 1 only decrements count");

    ASSERT_EQ(ne_mod_table_init(&tbl, 8), NE_MOD_OK);
    make_blank_contexts(&p1, &l1);
    make_blank_contexts(&p2, &l2);

    /* Load twice to get ref_count == 2 */
    ASSERT_EQ(ne_mod_load(&tbl, "MYMOD", &p1, &l1, &h1), NE_MOD_OK);
    ASSERT_EQ(ne_mod_load(&tbl, "MYMOD", &p2, &l2, &h2), NE_MOD_OK);
    ASSERT_EQ((long long)h1, (long long)h2);

    ne_free(&p2);
    ne_loader_free(&l2);

    /* First unload: ref_count goes from 2 to 1; entry stays */
    ASSERT_EQ(ne_mod_unload(&tbl, h1), NE_MOD_OK);
    e = ne_mod_get(&tbl, h1);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->ref_count, (uint16_t)1);
    ASSERT_EQ(tbl.count, (uint16_t)1);

    /* Second unload: ref_count reaches 0; entry is removed */
    ASSERT_EQ(ne_mod_unload(&tbl, h1), NE_MOD_OK);
    ASSERT_EQ(tbl.count, (uint16_t)0);

    ne_mod_table_free(&tbl);
    TEST_PASS();
}

static void test_unload_invalid_handle(void)
{
    NEModuleTable tbl;
    TEST_BEGIN("unload with invalid handle returns error");
    ASSERT_EQ(ne_mod_table_init(&tbl, 4), NE_MOD_OK);
    ASSERT_EQ(ne_mod_unload(&tbl, NE_MOD_HANDLE_INVALID), NE_MOD_ERR_BAD_HANDLE);
    ne_mod_table_free(&tbl);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * ne_mod_add_dep – dependency tracking
 * ---------------------------------------------------------------------- */

static void test_add_dep_basic(void)
{
    NEModuleTable   tbl;
    NEParserContext pA, pB;
    NELoaderContext lA, lB;
    NEModuleHandle  hA, hB;
    NEModuleEntry  *eA;

    TEST_BEGIN("add_dep records dependency between two modules");

    ASSERT_EQ(ne_mod_table_init(&tbl, 8), NE_MOD_OK);
    make_blank_contexts(&pA, &lA);
    make_blank_contexts(&pB, &lB);

    ASSERT_EQ(ne_mod_load(&tbl, "MODA", &pA, &lA, &hA), NE_MOD_OK);
    ASSERT_EQ(ne_mod_load(&tbl, "MODB", &pB, &lB, &hB), NE_MOD_OK);

    ASSERT_EQ(ne_mod_add_dep(&tbl, hA, hB), NE_MOD_OK);

    eA = ne_mod_get(&tbl, hA);
    ASSERT_NOT_NULL(eA);
    ASSERT_EQ(eA->dep_count, (uint16_t)1);
    ASSERT_EQ((long long)eA->deps[0], (long long)hB);

    ne_mod_table_free(&tbl);
    TEST_PASS();
}

static void test_add_dep_duplicate_ignored(void)
{
    NEModuleTable   tbl;
    NEParserContext pA, pB;
    NELoaderContext lA, lB;
    NEModuleHandle  hA, hB;
    NEModuleEntry  *eA;

    TEST_BEGIN("add_dep ignores duplicate dependency entries");

    ASSERT_EQ(ne_mod_table_init(&tbl, 8), NE_MOD_OK);
    make_blank_contexts(&pA, &lA);
    make_blank_contexts(&pB, &lB);

    ASSERT_EQ(ne_mod_load(&tbl, "MODA", &pA, &lA, &hA), NE_MOD_OK);
    ASSERT_EQ(ne_mod_load(&tbl, "MODB", &pB, &lB, &hB), NE_MOD_OK);

    ASSERT_EQ(ne_mod_add_dep(&tbl, hA, hB), NE_MOD_OK);
    ASSERT_EQ(ne_mod_add_dep(&tbl, hA, hB), NE_MOD_OK); /* duplicate */

    eA = ne_mod_get(&tbl, hA);
    ASSERT_NOT_NULL(eA);
    ASSERT_EQ(eA->dep_count, (uint16_t)1); /* still only one record */

    ne_mod_table_free(&tbl);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * ne_mod_unload – dependency-guarded unload
 * ---------------------------------------------------------------------- */

static void test_unload_blocked_by_dependent(void)
{
    NEModuleTable   tbl;
    NEParserContext pA, pB;
    NELoaderContext lA, lB;
    NEModuleHandle  hA, hB;
    int rc;

    TEST_BEGIN("unload blocked when another module depends on this one");

    ASSERT_EQ(ne_mod_table_init(&tbl, 8), NE_MOD_OK);
    make_blank_contexts(&pA, &lA);
    make_blank_contexts(&pB, &lB);

    ASSERT_EQ(ne_mod_load(&tbl, "MODA", &pA, &lA, &hA), NE_MOD_OK);
    ASSERT_EQ(ne_mod_load(&tbl, "MODB", &pB, &lB, &hB), NE_MOD_OK);

    /* MODA depends on MODB */
    ASSERT_EQ(ne_mod_add_dep(&tbl, hA, hB), NE_MOD_OK);

    /* Unloading MODB while MODA still depends on it must fail */
    rc = ne_mod_unload(&tbl, hB);
    ASSERT_EQ(rc, NE_MOD_ERR_IN_USE);

    /* MODB is still in the table */
    ASSERT_NOT_NULL(ne_mod_get(&tbl, hB));
    ASSERT_EQ(tbl.count, (uint16_t)2);

    ne_mod_table_free(&tbl);
    TEST_PASS();
}

static void test_unload_allowed_after_dependent_removed(void)
{
    NEModuleTable   tbl;
    NEParserContext pA, pB;
    NELoaderContext lA, lB;
    NEModuleHandle  hA, hB;

    TEST_BEGIN("unload succeeds after the dependent module is unloaded first");

    ASSERT_EQ(ne_mod_table_init(&tbl, 8), NE_MOD_OK);
    make_blank_contexts(&pA, &lA);
    make_blank_contexts(&pB, &lB);

    ASSERT_EQ(ne_mod_load(&tbl, "MODA", &pA, &lA, &hA), NE_MOD_OK);
    ASSERT_EQ(ne_mod_load(&tbl, "MODB", &pB, &lB, &hB), NE_MOD_OK);
    ASSERT_EQ(ne_mod_add_dep(&tbl, hA, hB), NE_MOD_OK);

    /* Unload MODA first (the dependent), then MODB */
    ASSERT_EQ(ne_mod_unload(&tbl, hA), NE_MOD_OK);
    ASSERT_EQ(tbl.count, (uint16_t)1);

    ASSERT_EQ(ne_mod_unload(&tbl, hB), NE_MOD_OK);
    ASSERT_EQ(tbl.count, (uint16_t)0);

    ne_mod_table_free(&tbl);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * ne_mod_find
 * ---------------------------------------------------------------------- */

static void test_find_by_name(void)
{
    NEModuleTable   tbl;
    NEParserContext p;
    NELoaderContext l;
    NEModuleHandle  h, found;

    TEST_BEGIN("find returns correct handle for loaded module name");

    ASSERT_EQ(ne_mod_table_init(&tbl, 8), NE_MOD_OK);
    make_blank_contexts(&p, &l);
    ASSERT_EQ(ne_mod_load(&tbl, "GDI", &p, &l, &h), NE_MOD_OK);

    found = ne_mod_find(&tbl, "GDI");
    ASSERT_EQ((long long)found, (long long)h);

    ne_mod_table_free(&tbl);
    TEST_PASS();
}

static void test_find_not_found(void)
{
    NEModuleTable tbl;
    TEST_BEGIN("find returns invalid handle for unknown name");
    ASSERT_EQ(ne_mod_table_init(&tbl, 4), NE_MOD_OK);
    ASSERT_EQ((long long)ne_mod_find(&tbl, "MISSING"),
              (long long)NE_MOD_HANDLE_INVALID);
    ne_mod_table_free(&tbl);
    TEST_PASS();
}

static void test_find_case_sensitive(void)
{
    NEModuleTable   tbl;
    NEParserContext p;
    NELoaderContext l;
    NEModuleHandle  h;

    TEST_BEGIN("find is case-sensitive");

    ASSERT_EQ(ne_mod_table_init(&tbl, 4), NE_MOD_OK);
    make_blank_contexts(&p, &l);
    ASSERT_EQ(ne_mod_load(&tbl, "KERNEL", &p, &l, &h), NE_MOD_OK);

    /* Lower-case version must not match */
    ASSERT_EQ((long long)ne_mod_find(&tbl, "kernel"),
              (long long)NE_MOD_HANDLE_INVALID);

    ne_mod_table_free(&tbl);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * Table capacity limit
 * ---------------------------------------------------------------------- */

static void test_table_full(void)
{
    NEModuleTable   tbl;
    NEParserContext p;
    NELoaderContext l;
    NEModuleHandle  h;
    int rc;
    /* Use a tiny capacity of 2 to trigger NE_MOD_ERR_FULL quickly */
    char name[NE_MOD_NAME_MAX];

    TEST_BEGIN("load into full table returns NE_MOD_ERR_FULL");

    ASSERT_EQ(ne_mod_table_init(&tbl, 2), NE_MOD_OK);

    make_blank_contexts(&p, &l);
    ASSERT_EQ(ne_mod_load(&tbl, "MOD1", &p, &l, &h), NE_MOD_OK);
    make_blank_contexts(&p, &l);
    ASSERT_EQ(ne_mod_load(&tbl, "MOD2", &p, &l, &h), NE_MOD_OK);

    /* Table is now full; a third distinct module must fail */
    make_blank_contexts(&p, &l);
    strncpy(name, "MOD3", NE_MOD_NAME_MAX - 1);
    name[NE_MOD_NAME_MAX - 1] = '\0';
    rc = ne_mod_load(&tbl, name, &p, &l, &h);
    ASSERT_EQ(rc, NE_MOD_ERR_FULL);
    ASSERT_EQ((long long)h, (long long)NE_MOD_HANDLE_INVALID);

    /* p and l are still owned by the caller after failure */
    ne_free(&p);
    ne_loader_free(&l);

    ne_mod_table_free(&tbl);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * Name truncation
 * ---------------------------------------------------------------------- */

static void test_name_truncation(void)
{
    NEModuleTable   tbl;
    NEParserContext p;
    NELoaderContext l;
    NEModuleHandle  h;
    NEModuleEntry  *e;
    /* Longer than NE_MOD_NAME_MAX - 1 */
    const char *long_name = "VERYLONGNAME";

    TEST_BEGIN("module name is truncated to NE_MOD_NAME_MAX-1 characters");

    ASSERT_EQ(ne_mod_table_init(&tbl, 4), NE_MOD_OK);
    make_blank_contexts(&p, &l);
    ASSERT_EQ(ne_mod_load(&tbl, long_name, &p, &l, &h), NE_MOD_OK);

    e = ne_mod_get(&tbl, h);
    ASSERT_NOT_NULL(e);
    /* Name field must be NUL-terminated within bounds */
    ASSERT_EQ(e->name[NE_MOD_NAME_MAX - 1u], '\0');

    ne_mod_table_free(&tbl);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * ne_mod_strerror
 * ---------------------------------------------------------------------- */

static void test_strerror(void)
{
    TEST_BEGIN("strerror returns non-NULL strings for known codes");
    ASSERT_NOT_NULL(ne_mod_strerror(NE_MOD_OK));
    ASSERT_NOT_NULL(ne_mod_strerror(NE_MOD_ERR_NULL));
    ASSERT_NOT_NULL(ne_mod_strerror(NE_MOD_ERR_ALLOC));
    ASSERT_NOT_NULL(ne_mod_strerror(NE_MOD_ERR_NOT_FOUND));
    ASSERT_NOT_NULL(ne_mod_strerror(NE_MOD_ERR_FULL));
    ASSERT_NOT_NULL(ne_mod_strerror(NE_MOD_ERR_DEP_FULL));
    ASSERT_NOT_NULL(ne_mod_strerror(NE_MOD_ERR_IN_USE));
    ASSERT_NOT_NULL(ne_mod_strerror(NE_MOD_ERR_BAD_HANDLE));
    ASSERT_NOT_NULL(ne_mod_strerror(-999));
    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== NE Module Table Tests (Step 4) ===\n\n");

    /* init / free */
    test_table_init_free();
    test_table_init_null();
    test_table_init_zero_capacity();
    test_table_free_null();

    /* load */
    test_load_single_module();
    test_load_multiple_distinct_modules();
    test_load_duplicate_returns_same_handle();

    /* addref */
    test_addref_increments_count();
    test_addref_invalid_handle();
    test_addref_not_found();

    /* unload – reference counting */
    test_unload_decrements_and_removes();
    test_unload_decrements_only_when_refs_remain();
    test_unload_invalid_handle();

    /* dependencies */
    test_add_dep_basic();
    test_add_dep_duplicate_ignored();
    test_unload_blocked_by_dependent();
    test_unload_allowed_after_dependent_removed();

    /* find */
    test_find_by_name();
    test_find_not_found();
    test_find_case_sensitive();

    /* capacity */
    test_table_full();

    /* name handling */
    test_name_truncation();

    /* error strings */
    test_strerror();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

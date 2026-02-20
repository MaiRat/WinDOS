/*
 * test_ne_release.c - Tests for Phase 7: Release Readiness
 *
 * Verifies:
 *   - Context initialisation and teardown
 *   - Readiness item status management
 *   - Regression test suite tracking
 *   - Known-issue tracking
 *   - Release metadata (version, tag, date)
 *   - Reproducible build verification
 *   - Overall readiness check
 *   - Error handling and error strings
 */

#include "../src/ne_release.h"

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

#define ASSERT_STR_EQ(a, b) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            g_tests_failed++; \
            printf("FAIL - expected \"%s\" got \"%s\" (line %d)\n", \
                   (b), (a), __LINE__); \
            return; \
        } \
    } while (0)

/* =========================================================================
 * Context tests
 * ===================================================================== */

static void test_release_init_free(void)
{
    NEReleaseContext ctx;
    TEST_BEGIN("init sets initialized=1, free sets it to 0");
    ASSERT_EQ(ne_release_init(&ctx), NE_RELEASE_OK);
    ASSERT_EQ(ctx.initialized, 1);
    ASSERT_EQ(ctx.regression_count, 0);
    ASSERT_EQ(ctx.known_issue_count, 0);
    ASSERT_EQ(ctx.build_hash_count, 0);
    ne_release_free(&ctx);
    ASSERT_EQ(ctx.initialized, 0);
    TEST_PASS();
}

static void test_release_init_null(void)
{
    TEST_BEGIN("init with NULL ctx returns ERR_NULL");
    ASSERT_EQ(ne_release_init(NULL), NE_RELEASE_ERR_NULL);
    TEST_PASS();
}

static void test_release_free_null_safe(void)
{
    TEST_BEGIN("free(NULL) does not crash");
    ne_release_free(NULL);
    TEST_PASS();
}

static void test_release_init_items_pending(void)
{
    NEReleaseContext ctx;
    uint8_t i;

    TEST_BEGIN("init sets all items to PENDING");
    ne_release_init(&ctx);
    for (i = 0; i < NE_RELEASE_ITEM_COUNT; i++) {
        ASSERT_EQ(ctx.items[i].item_id, i);
        ASSERT_EQ(ctx.items[i].status, NE_RELEASE_STATUS_PENDING);
    }
    ne_release_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Readiness item status tests
 * ===================================================================== */

static void test_set_get_status(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("set and get status for boot sequence item");
    ne_release_init(&ctx);
    ASSERT_EQ(ne_release_get_status(&ctx, NE_RELEASE_ITEM_BOOT_SEQ),
              (int)NE_RELEASE_STATUS_PENDING);
    ASSERT_EQ(ne_release_set_status(&ctx, NE_RELEASE_ITEM_BOOT_SEQ,
                                    NE_RELEASE_STATUS_PASS), NE_RELEASE_OK);
    ASSERT_EQ(ne_release_get_status(&ctx, NE_RELEASE_ITEM_BOOT_SEQ),
              (int)NE_RELEASE_STATUS_PASS);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_status_bad_item(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("set/get status with bad item ID returns ERR_BAD_ITEM");
    ne_release_init(&ctx);
    ASSERT_EQ(ne_release_set_status(&ctx, NE_RELEASE_ITEM_COUNT,
                                    NE_RELEASE_STATUS_PASS),
              NE_RELEASE_ERR_BAD_ITEM);
    ASSERT_EQ(ne_release_get_status(&ctx, NE_RELEASE_ITEM_COUNT),
              NE_RELEASE_ERR_BAD_ITEM);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_status_bad_status(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("set status with bad status value returns ERR_BAD_STATUS");
    ne_release_init(&ctx);
    ASSERT_EQ(ne_release_set_status(&ctx, NE_RELEASE_ITEM_BOOT_SEQ,
                                    NE_RELEASE_STATUS_COUNT),
              NE_RELEASE_ERR_BAD_STATUS);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_status_null_ctx(void)
{
    TEST_BEGIN("set/get status with NULL ctx returns ERR_NULL");
    ASSERT_EQ(ne_release_set_status(NULL, 0, NE_RELEASE_STATUS_PASS),
              NE_RELEASE_ERR_NULL);
    ASSERT_EQ(ne_release_get_status(NULL, 0), NE_RELEASE_ERR_NULL);
    TEST_PASS();
}

static void test_set_get_notes(void)
{
    NEReleaseContext ctx;
    const char *notes;

    TEST_BEGIN("set and get notes for an item");
    ne_release_init(&ctx);
    ASSERT_EQ(ne_release_set_notes(&ctx, NE_RELEASE_ITEM_BOOT_SEQ,
                                   "Boot validated on DOSBox 0.74"),
              NE_RELEASE_OK);
    notes = ne_release_get_notes(&ctx, NE_RELEASE_ITEM_BOOT_SEQ);
    ASSERT_NOT_NULL(notes);
    ASSERT_STR_EQ(notes, "Boot validated on DOSBox 0.74");
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_notes_null_args(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("notes NULL arg checks");
    ne_release_init(&ctx);
    ASSERT_EQ(ne_release_set_notes(NULL, 0, "x"), NE_RELEASE_ERR_NULL);
    ASSERT_EQ(ne_release_set_notes(&ctx, 0, NULL), NE_RELEASE_ERR_NULL);
    ASSERT_NULL(ne_release_get_notes(NULL, 0));
    ne_release_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Regression test suite tests
 * ===================================================================== */

static void test_regression_add_and_query(void)
{
    NEReleaseContext ctx;
    const NEReleaseRegrEntry *entry;

    TEST_BEGIN("add regression entry and query by index");
    ne_release_init(&ctx);
    ASSERT_EQ(ne_release_add_regression(&ctx, "NE Parser", 8, 8, 0,
                                        NE_RELEASE_TEST_PASS),
              NE_RELEASE_OK);
    ASSERT_EQ(ne_release_regression_count(&ctx), 1);
    entry = ne_release_get_regression(&ctx, 0);
    ASSERT_NOT_NULL(entry);
    ASSERT_STR_EQ(entry->suite_name, "NE Parser");
    ASSERT_EQ(entry->tests_total, 8);
    ASSERT_EQ(entry->tests_passed, 8);
    ASSERT_EQ(entry->tests_failed, 0);
    ASSERT_EQ(entry->result, NE_RELEASE_TEST_PASS);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_regression_multiple(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("add multiple regression entries; count is correct");
    ne_release_init(&ctx);
    ne_release_add_regression(&ctx, "Parser", 8, 8, 0,
                              NE_RELEASE_TEST_PASS);
    ne_release_add_regression(&ctx, "Loader", 5, 5, 0,
                              NE_RELEASE_TEST_PASS);
    ne_release_add_regression(&ctx, "Compat", 34, 34, 0,
                              NE_RELEASE_TEST_PASS);
    ASSERT_EQ(ne_release_regression_count(&ctx), 3);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_regression_all_pass_true(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("regression_all_pass returns 1 when all suites pass");
    ne_release_init(&ctx);
    ne_release_add_regression(&ctx, "A", 10, 10, 0, NE_RELEASE_TEST_PASS);
    ne_release_add_regression(&ctx, "B", 5, 5, 0, NE_RELEASE_TEST_PASS);
    ASSERT_EQ(ne_release_regression_all_pass(&ctx), 1);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_regression_all_pass_false(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("regression_all_pass returns 0 when a suite fails");
    ne_release_init(&ctx);
    ne_release_add_regression(&ctx, "A", 10, 10, 0, NE_RELEASE_TEST_PASS);
    ne_release_add_regression(&ctx, "B", 5, 3, 2, NE_RELEASE_TEST_FAIL);
    ASSERT_EQ(ne_release_regression_all_pass(&ctx), 0);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_regression_all_pass_empty(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("regression_all_pass returns 0 when no suites registered");
    ne_release_init(&ctx);
    ASSERT_EQ(ne_release_regression_all_pass(&ctx), 0);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_regression_out_of_range(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("get_regression returns NULL for out-of-range index");
    ne_release_init(&ctx);
    ASSERT_NULL(ne_release_get_regression(&ctx, 0));
    ASSERT_NULL(ne_release_get_regression(&ctx, 100));
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_regression_null_args(void)
{
    TEST_BEGIN("regression NULL arg checks");
    ASSERT_EQ(ne_release_add_regression(NULL, "X", 1, 1, 0,
                                        NE_RELEASE_TEST_PASS),
              NE_RELEASE_ERR_NULL);
    ASSERT_EQ(ne_release_regression_count(NULL), 0);
    ASSERT_NULL(ne_release_get_regression(NULL, 0));
    ASSERT_EQ(ne_release_regression_all_pass(NULL), 0);
    TEST_PASS();
}

/* =========================================================================
 * Known issues tests
 * ===================================================================== */

static void test_known_issue_add_and_query(void)
{
    NEReleaseContext ctx;
    const NEReleaseKnownIssue *issue;

    TEST_BEGIN("add known issue and query by index");
    ne_release_init(&ctx);
    ASSERT_EQ(ne_release_add_known_issue(&ctx,
        "OLE2 APIs not implemented", NE_RELEASE_SEV_MEDIUM),
              NE_RELEASE_OK);
    ASSERT_EQ(ne_release_known_issue_count(&ctx), 1);
    issue = ne_release_get_known_issue(&ctx, 0);
    ASSERT_NOT_NULL(issue);
    ASSERT_STR_EQ(issue->description, "OLE2 APIs not implemented");
    ASSERT_EQ(issue->severity, NE_RELEASE_SEV_MEDIUM);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_known_issue_multiple(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("add multiple known issues; count is correct");
    ne_release_init(&ctx);
    ne_release_add_known_issue(&ctx, "Issue 1", NE_RELEASE_SEV_LOW);
    ne_release_add_known_issue(&ctx, "Issue 2", NE_RELEASE_SEV_HIGH);
    ne_release_add_known_issue(&ctx, "Issue 3", NE_RELEASE_SEV_CRITICAL);
    ASSERT_EQ(ne_release_known_issue_count(&ctx), 3);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_known_issue_out_of_range(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("get_known_issue returns NULL for out-of-range index");
    ne_release_init(&ctx);
    ASSERT_NULL(ne_release_get_known_issue(&ctx, 0));
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_known_issue_null_args(void)
{
    TEST_BEGIN("known issue NULL arg checks");
    ASSERT_EQ(ne_release_add_known_issue(NULL, "X", 0),
              NE_RELEASE_ERR_NULL);
    ASSERT_EQ(ne_release_known_issue_count(NULL), 0);
    ASSERT_NULL(ne_release_get_known_issue(NULL, 0));
    TEST_PASS();
}

/* =========================================================================
 * Release metadata tests
 * ===================================================================== */

static void test_metadata_version_tag_date(void)
{
    NEReleaseContext ctx;
    const NEReleaseMetadata *meta;

    TEST_BEGIN("set and get version, tag, date");
    ne_release_init(&ctx);
    ASSERT_EQ(ne_release_set_version(&ctx, "1.0.0"), NE_RELEASE_OK);
    ASSERT_EQ(ne_release_set_tag(&ctx, "v1.0.0"), NE_RELEASE_OK);
    ASSERT_EQ(ne_release_set_date(&ctx, "2026-02-20"), NE_RELEASE_OK);

    meta = ne_release_get_metadata(&ctx);
    ASSERT_NOT_NULL(meta);
    ASSERT_STR_EQ(meta->version, "1.0.0");
    ASSERT_STR_EQ(meta->tag, "v1.0.0");
    ASSERT_STR_EQ(meta->date, "2026-02-20");
    ASSERT_NE(meta->tagged, 0);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_metadata_not_tagged(void)
{
    NEReleaseContext ctx;
    const NEReleaseMetadata *meta;

    TEST_BEGIN("metadata tagged is 0 before set_tag");
    ne_release_init(&ctx);
    meta = ne_release_get_metadata(&ctx);
    ASSERT_NOT_NULL(meta);
    ASSERT_EQ(meta->tagged, 0);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_metadata_null_args(void)
{
    TEST_BEGIN("metadata NULL arg checks");
    ASSERT_EQ(ne_release_set_version(NULL, "1.0"), NE_RELEASE_ERR_NULL);
    ASSERT_EQ(ne_release_set_tag(NULL, "v1.0"), NE_RELEASE_ERR_NULL);
    ASSERT_EQ(ne_release_set_date(NULL, "2026-01-01"), NE_RELEASE_ERR_NULL);
    ASSERT_NULL(ne_release_get_metadata(NULL));
    TEST_PASS();
}

/* =========================================================================
 * Reproducible build verification tests
 * ===================================================================== */

static void test_build_hash_add_and_verify(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("add matching build hashes; verify reproducible");
    ne_release_init(&ctx);
    ne_release_add_build_hash(&ctx, "CI Linux x86", 0xDEADBEEF);
    ne_release_add_build_hash(&ctx, "CI Linux ARM", 0xDEADBEEF);
    ASSERT_EQ(ne_release_build_hash_count(&ctx), 2);
    ASSERT_EQ(ne_release_verify_reproducible(&ctx), 1);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_build_hash_mismatch(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("mismatched build hashes; verify fails");
    ne_release_init(&ctx);
    ne_release_add_build_hash(&ctx, "Env A", 0x11111111);
    ne_release_add_build_hash(&ctx, "Env B", 0x22222222);
    ASSERT_EQ(ne_release_verify_reproducible(&ctx), 0);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_build_hash_single(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("single build hash; verify returns 0 (need >=2)");
    ne_release_init(&ctx);
    ne_release_add_build_hash(&ctx, "Env A", 0xAAAAAAAA);
    ASSERT_EQ(ne_release_verify_reproducible(&ctx), 0);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_build_hash_null_args(void)
{
    TEST_BEGIN("build hash NULL arg checks");
    ASSERT_EQ(ne_release_add_build_hash(NULL, "X", 0),
              NE_RELEASE_ERR_NULL);
    ASSERT_EQ(ne_release_build_hash_count(NULL), 0);
    ASSERT_EQ(ne_release_verify_reproducible(NULL), 0);
    TEST_PASS();
}

/* =========================================================================
 * Overall readiness tests
 * ===================================================================== */

static void test_is_ready_all_pass(void)
{
    NEReleaseContext ctx;
    uint8_t i;

    TEST_BEGIN("is_ready returns 1 when all items PASS");
    ne_release_init(&ctx);
    for (i = 0; i < NE_RELEASE_ITEM_COUNT; i++) {
        ne_release_set_status(&ctx, i, NE_RELEASE_STATUS_PASS);
    }
    ASSERT_EQ(ne_release_is_ready(&ctx), 1);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_is_ready_not_all_pass(void)
{
    NEReleaseContext ctx;
    uint8_t i;

    TEST_BEGIN("is_ready returns 0 when any item is not PASS");
    ne_release_init(&ctx);
    for (i = 0; i < NE_RELEASE_ITEM_COUNT; i++) {
        ne_release_set_status(&ctx, i, NE_RELEASE_STATUS_PASS);
    }
    ne_release_set_status(&ctx, NE_RELEASE_ITEM_DEV_GUIDE,
                          NE_RELEASE_STATUS_IN_PROGRESS);
    ASSERT_EQ(ne_release_is_ready(&ctx), 0);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_is_ready_initial(void)
{
    NEReleaseContext ctx;

    TEST_BEGIN("is_ready returns 0 on fresh init");
    ne_release_init(&ctx);
    ASSERT_EQ(ne_release_is_ready(&ctx), 0);
    ne_release_free(&ctx);
    TEST_PASS();
}

static void test_is_ready_null(void)
{
    TEST_BEGIN("is_ready returns 0 with NULL ctx");
    ASSERT_EQ(ne_release_is_ready(NULL), 0);
    TEST_PASS();
}

/* =========================================================================
 * String helper tests
 * ===================================================================== */

static void test_item_names(void)
{
    TEST_BEGIN("item_name returns non-NULL for all item IDs");
    ASSERT_NOT_NULL(ne_release_item_name(NE_RELEASE_ITEM_BOOT_SEQ));
    ASSERT_NOT_NULL(ne_release_item_name(NE_RELEASE_ITEM_REGRESSION));
    ASSERT_NOT_NULL(ne_release_item_name(NE_RELEASE_ITEM_INSTALL_GUIDE));
    ASSERT_NOT_NULL(ne_release_item_name(NE_RELEASE_ITEM_DEV_GUIDE));
    ASSERT_NOT_NULL(ne_release_item_name(NE_RELEASE_ITEM_REPRO_BUILD));
    ASSERT_NOT_NULL(ne_release_item_name(NE_RELEASE_ITEM_RELEASE_TAG));
    ASSERT_STR_EQ(ne_release_item_name(99), "UNKNOWN");
    TEST_PASS();
}

static void test_status_names(void)
{
    TEST_BEGIN("status_name returns correct strings");
    ASSERT_STR_EQ(ne_release_status_name(NE_RELEASE_STATUS_PENDING),
                  "PENDING");
    ASSERT_STR_EQ(ne_release_status_name(NE_RELEASE_STATUS_PASS),
                  "PASS");
    ASSERT_STR_EQ(ne_release_status_name(99), "UNKNOWN");
    TEST_PASS();
}

static void test_strerror(void)
{
    TEST_BEGIN("strerror returns non-NULL for all known codes");
    ASSERT_NOT_NULL(ne_release_strerror(NE_RELEASE_OK));
    ASSERT_NOT_NULL(ne_release_strerror(NE_RELEASE_ERR_NULL));
    ASSERT_NOT_NULL(ne_release_strerror(NE_RELEASE_ERR_BAD_ITEM));
    ASSERT_NOT_NULL(ne_release_strerror(NE_RELEASE_ERR_BAD_STATUS));
    ASSERT_NOT_NULL(ne_release_strerror(NE_RELEASE_ERR_FULL));
    ASSERT_NOT_NULL(ne_release_strerror(NE_RELEASE_ERR_INIT));
    ASSERT_NOT_NULL(ne_release_strerror(-99));
    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== WinDOS Release Readiness Tests (Phase 7) ===\n");

    printf("\n--- Context tests ---\n");
    test_release_init_free();
    test_release_init_null();
    test_release_free_null_safe();
    test_release_init_items_pending();

    printf("\n--- Readiness item status tests ---\n");
    test_set_get_status();
    test_status_bad_item();
    test_status_bad_status();
    test_status_null_ctx();
    test_set_get_notes();
    test_notes_null_args();

    printf("\n--- Regression test suite tests ---\n");
    test_regression_add_and_query();
    test_regression_multiple();
    test_regression_all_pass_true();
    test_regression_all_pass_false();
    test_regression_all_pass_empty();
    test_regression_out_of_range();
    test_regression_null_args();

    printf("\n--- Known issues tests ---\n");
    test_known_issue_add_and_query();
    test_known_issue_multiple();
    test_known_issue_out_of_range();
    test_known_issue_null_args();

    printf("\n--- Release metadata tests ---\n");
    test_metadata_version_tag_date();
    test_metadata_not_tagged();
    test_metadata_null_args();

    printf("\n--- Reproducible build tests ---\n");
    test_build_hash_add_and_verify();
    test_build_hash_mismatch();
    test_build_hash_single();
    test_build_hash_null_args();

    printf("\n--- Overall readiness tests ---\n");
    test_is_ready_all_pass();
    test_is_ready_not_all_pass();
    test_is_ready_initial();
    test_is_ready_null();

    printf("\n--- String helper tests ---\n");
    test_item_names();
    test_status_names();
    test_strerror();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

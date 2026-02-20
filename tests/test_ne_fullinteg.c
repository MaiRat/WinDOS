/*
 * test_ne_fullinteg.c - Tests for Step 9: full integration validation
 *
 * Verifies:
 *   - ne_fullinteg_table_init / ne_fullinteg_table_free
 *   - ne_fullinteg_set_status: valid transitions and logging
 *   - ne_fullinteg_set_notes: storage and truncation
 *   - ne_fullinteg_checklist_set: done/cleared transitions and logging
 *   - ne_fullinteg_checklist_set_notes: storage and truncation
 *   - ne_fullinteg_is_complete: all-pass + all-done required
 *   - ne_fullinteg_report: output written to log_fp
 *   - ne_fullinteg_item_name / ne_fullinteg_status_name /
 *     ne_fullinteg_cl_name / ne_fullinteg_strerror
 *   - Error-path coverage for all public API functions
 *
 * Build with Watcom (DOS target):
 *   wcc -ml -za99 -wx -d2 -i=../src ../src/ne_fullinteg.c \
 *       test_ne_fullinteg.c
 *   wlink system dos name test_ne_fullinteg.exe file \
 *         test_ne_fullinteg.obj,ne_fullinteg.obj
 *
 * Build on POSIX host (CI):
 *   cc -std=c99 -Wall -I../src ../src/ne_fullinteg.c \
 *      test_ne_fullinteg.c -o test_ne_fullinteg
 */

#include "../src/ne_fullinteg.h"

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
 * Full integration table – init / free
 * ===================================================================== */

static void test_table_init_free(void)
{
    NEFullIntegTable tbl;
    uint8_t          i;

    TEST_BEGIN("fullinteg table init and free");

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);

    /* Every item should start PENDING with empty notes. */
    for (i = 0; i < NE_FULLINTEG_ITEM_COUNT; i++) {
        ASSERT_EQ(tbl.items[i].item_id, (int)i);
        ASSERT_EQ(tbl.items[i].status,  NE_FULLINTEG_STATUS_PENDING);
        ASSERT_EQ(tbl.items[i].notes[0], '\0');
    }

    /* Every checklist item should start not-done with empty notes. */
    for (i = 0; i < NE_FULLINTEG_CL_COUNT; i++) {
        ASSERT_EQ(tbl.checklist[i].cl_id, (int)i);
        ASSERT_EQ(tbl.checklist[i].done,  0);
        ASSERT_EQ(tbl.checklist[i].notes[0], '\0');
    }

    /* Default log stream is stderr. */
    ASSERT_NOT_NULL(tbl.log_fp);

    ne_fullinteg_table_free(&tbl);

    /* After free the table is zeroed. */
    ASSERT_NULL(tbl.log_fp);

    TEST_PASS();
}

static void test_table_init_null(void)
{
    TEST_BEGIN("fullinteg table init with NULL returns error");
    ASSERT_EQ(ne_fullinteg_table_init(NULL), NE_FULLINTEG_ERR_NULL);
    TEST_PASS();
}

static void test_table_free_null(void)
{
    TEST_BEGIN("fullinteg table free NULL is safe (no crash)");
    ne_fullinteg_table_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * ne_fullinteg_set_status
 * ===================================================================== */

static void test_set_status_progression(void)
{
    NEFullIntegTable tbl;

    TEST_BEGIN("set_status: BOOT_SEQ advances through all states");

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_fullinteg_set_status(&tbl, NE_FULLINTEG_ITEM_BOOT_SEQ,
                                       NE_FULLINTEG_STATUS_IN_PROGRESS),
              NE_FULLINTEG_OK);
    ASSERT_EQ(tbl.items[NE_FULLINTEG_ITEM_BOOT_SEQ].status,
              NE_FULLINTEG_STATUS_IN_PROGRESS);

    ASSERT_EQ(ne_fullinteg_set_status(&tbl, NE_FULLINTEG_ITEM_BOOT_SEQ,
                                       NE_FULLINTEG_STATUS_PASS),
              NE_FULLINTEG_OK);
    ASSERT_EQ(tbl.items[NE_FULLINTEG_ITEM_BOOT_SEQ].status,
              NE_FULLINTEG_STATUS_PASS);

    ASSERT_EQ(ne_fullinteg_set_status(&tbl, NE_FULLINTEG_ITEM_BOOT_SEQ,
                                       NE_FULLINTEG_STATUS_FAIL),
              NE_FULLINTEG_OK);
    ASSERT_EQ(tbl.items[NE_FULLINTEG_ITEM_BOOT_SEQ].status,
              NE_FULLINTEG_STATUS_FAIL);

    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

static void test_set_status_logs_transition(void)
{
    NEFullIntegTable tbl;
    FILE            *fp;
    char             buf[256];
    int              n;

    TEST_BEGIN("set_status: transition message written to log_fp");

    fp = tmpfile();
    ASSERT_NOT_NULL(fp);

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = fp;

    ASSERT_EQ(ne_fullinteg_set_status(&tbl, NE_FULLINTEG_ITEM_REGRESSION,
                                       NE_FULLINTEG_STATUS_IN_PROGRESS),
              NE_FULLINTEG_OK);
    fflush(fp);

    rewind(fp);
    n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';

    ASSERT_NE(strstr(buf, "REGRESSION"),   NULL);
    ASSERT_NE(strstr(buf, "IN_PROGRESS"),  NULL);
    ASSERT_NE(strstr(buf, "PENDING"),      NULL);

    fclose(fp);
    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

static void test_set_status_error_paths(void)
{
    NEFullIntegTable tbl;

    TEST_BEGIN("set_status: error paths (NULL, bad item, bad status)");

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_fullinteg_set_status(NULL, NE_FULLINTEG_ITEM_BOOT_SEQ,
                                       NE_FULLINTEG_STATUS_PASS),
              NE_FULLINTEG_ERR_NULL);
    ASSERT_EQ(ne_fullinteg_set_status(&tbl, NE_FULLINTEG_ITEM_COUNT,
                                       NE_FULLINTEG_STATUS_PASS),
              NE_FULLINTEG_ERR_BAD_ITEM);
    ASSERT_EQ(ne_fullinteg_set_status(&tbl, NE_FULLINTEG_ITEM_BOOT_SEQ,
                                       NE_FULLINTEG_STATUS_COUNT),
              NE_FULLINTEG_ERR_BAD_STATUS);

    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_fullinteg_set_notes
 * ===================================================================== */

static void test_set_notes_stored(void)
{
    NEFullIntegTable tbl;

    TEST_BEGIN("set_notes stores text in item");

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_fullinteg_set_notes(&tbl, NE_FULLINTEG_ITEM_LIMITATIONS,
                                      "No EMS support in this release"),
              NE_FULLINTEG_OK);

    ASSERT_EQ(strcmp(tbl.items[NE_FULLINTEG_ITEM_LIMITATIONS].notes,
                     "No EMS support in this release"), 0);

    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

static void test_set_notes_truncation(void)
{
    NEFullIntegTable tbl;
    char             long_str[NE_FULLINTEG_NOTES_MAX + 32];
    uint16_t         i;

    TEST_BEGIN("set_notes truncates strings longer than NOTES_MAX-1");

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = NULL;

    /* Build a string longer than the buffer. */
    for (i = 0; i < NE_FULLINTEG_NOTES_MAX + 31u; i++)
        long_str[i] = 'A';
    long_str[NE_FULLINTEG_NOTES_MAX + 31u] = '\0';

    ASSERT_EQ(ne_fullinteg_set_notes(&tbl, NE_FULLINTEG_ITEM_BOOT_SEQ,
                                      long_str),
              NE_FULLINTEG_OK);

    /* Buffer must be NUL-terminated within the allocated space. */
    ASSERT_EQ(tbl.items[NE_FULLINTEG_ITEM_BOOT_SEQ]
                  .notes[NE_FULLINTEG_NOTES_MAX - 1u],
              '\0');

    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

static void test_set_notes_error_paths(void)
{
    NEFullIntegTable tbl;

    TEST_BEGIN("set_notes error paths (NULL tbl, NULL notes, bad item)");

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_fullinteg_set_notes(NULL, NE_FULLINTEG_ITEM_BOOT_SEQ, "x"),
              NE_FULLINTEG_ERR_NULL);
    ASSERT_EQ(ne_fullinteg_set_notes(&tbl, NE_FULLINTEG_ITEM_BOOT_SEQ, NULL),
              NE_FULLINTEG_ERR_NULL);
    ASSERT_EQ(ne_fullinteg_set_notes(&tbl, NE_FULLINTEG_ITEM_COUNT, "x"),
              NE_FULLINTEG_ERR_BAD_ITEM);

    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_fullinteg_checklist_set
 * ===================================================================== */

static void test_checklist_set_done(void)
{
    NEFullIntegTable tbl;

    TEST_BEGIN("checklist_set marks item done and cleared correctly");

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(tbl.checklist[NE_FULLINTEG_CL_BUILD_STEPS].done, 0);

    ASSERT_EQ(ne_fullinteg_checklist_set(&tbl, NE_FULLINTEG_CL_BUILD_STEPS, 1u),
              NE_FULLINTEG_OK);
    ASSERT_NE(tbl.checklist[NE_FULLINTEG_CL_BUILD_STEPS].done, 0);

    ASSERT_EQ(ne_fullinteg_checklist_set(&tbl, NE_FULLINTEG_CL_BUILD_STEPS, 0u),
              NE_FULLINTEG_OK);
    ASSERT_EQ(tbl.checklist[NE_FULLINTEG_CL_BUILD_STEPS].done, 0);

    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

static void test_checklist_set_logs(void)
{
    NEFullIntegTable tbl;
    FILE            *fp;
    char             buf[256];
    int              n;

    TEST_BEGIN("checklist_set writes message to log_fp");

    fp = tmpfile();
    ASSERT_NOT_NULL(fp);

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = fp;

    ASSERT_EQ(ne_fullinteg_checklist_set(&tbl, NE_FULLINTEG_CL_SIGNOFF, 1u),
              NE_FULLINTEG_OK);
    fflush(fp);

    rewind(fp);
    n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';

    ASSERT_NE(strstr(buf, "SIGNOFF"), NULL);
    ASSERT_NE(strstr(buf, "DONE"),    NULL);

    fclose(fp);
    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

static void test_checklist_set_error_paths(void)
{
    NEFullIntegTable tbl;

    TEST_BEGIN("checklist_set error paths (NULL, bad cl_id)");

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_fullinteg_checklist_set(NULL, NE_FULLINTEG_CL_BUILD_STEPS, 1u),
              NE_FULLINTEG_ERR_NULL);
    ASSERT_EQ(ne_fullinteg_checklist_set(&tbl, NE_FULLINTEG_CL_COUNT, 1u),
              NE_FULLINTEG_ERR_BAD_CL);

    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_fullinteg_checklist_set_notes
 * ===================================================================== */

static void test_checklist_notes_stored(void)
{
    NEFullIntegTable tbl;

    TEST_BEGIN("checklist_set_notes stores text in checklist item");

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_fullinteg_checklist_set_notes(&tbl, NE_FULLINTEG_CL_TEST_STEPS,
                                                "Run make test on clean build"),
              NE_FULLINTEG_OK);

    ASSERT_EQ(strcmp(tbl.checklist[NE_FULLINTEG_CL_TEST_STEPS].notes,
                     "Run make test on clean build"), 0);

    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

static void test_checklist_notes_error_paths(void)
{
    NEFullIntegTable tbl;

    TEST_BEGIN("checklist_set_notes error paths");

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_fullinteg_checklist_set_notes(NULL,
                                                NE_FULLINTEG_CL_BUILD_STEPS,
                                                "x"),
              NE_FULLINTEG_ERR_NULL);
    ASSERT_EQ(ne_fullinteg_checklist_set_notes(&tbl,
                                                NE_FULLINTEG_CL_BUILD_STEPS,
                                                NULL),
              NE_FULLINTEG_ERR_NULL);
    ASSERT_EQ(ne_fullinteg_checklist_set_notes(&tbl,
                                                NE_FULLINTEG_CL_COUNT,
                                                "x"),
              NE_FULLINTEG_ERR_BAD_CL);

    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_fullinteg_is_complete
 * ===================================================================== */

static void test_is_complete_requires_all_pass(void)
{
    NEFullIntegTable tbl;
    uint8_t          i;

    TEST_BEGIN("is_complete: requires every item PASS and checklist done");

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = NULL;

    /* Not complete while any item is PENDING. */
    ASSERT_EQ(ne_fullinteg_is_complete(&tbl), 0);

    /* Set all items to PASS. */
    for (i = 0; i < NE_FULLINTEG_ITEM_COUNT; i++)
        tbl.items[i].status = NE_FULLINTEG_STATUS_PASS;

    /* Still not complete: checklist items not done. */
    ASSERT_EQ(ne_fullinteg_is_complete(&tbl), 0);

    /* Mark all checklist items done. */
    for (i = 0; i < NE_FULLINTEG_CL_COUNT; i++)
        tbl.checklist[i].done = 1u;

    /* Now complete. */
    ASSERT_NE(ne_fullinteg_is_complete(&tbl), 0);

    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

static void test_is_complete_fail_blocks(void)
{
    NEFullIntegTable tbl;
    uint8_t          i;

    TEST_BEGIN("is_complete: any FAIL status blocks completion");

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = NULL;

    /* Set all items to PASS and all checklist items done. */
    for (i = 0; i < NE_FULLINTEG_ITEM_COUNT; i++)
        tbl.items[i].status = NE_FULLINTEG_STATUS_PASS;
    for (i = 0; i < NE_FULLINTEG_CL_COUNT; i++)
        tbl.checklist[i].done = 1u;

    ASSERT_NE(ne_fullinteg_is_complete(&tbl), 0);

    /* Flip one item to FAIL. */
    tbl.items[NE_FULLINTEG_ITEM_REPRO_BUILD].status = NE_FULLINTEG_STATUS_FAIL;
    ASSERT_EQ(ne_fullinteg_is_complete(&tbl), 0);

    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

static void test_is_complete_null_safe(void)
{
    TEST_BEGIN("is_complete with NULL tbl returns 0");
    ASSERT_EQ(ne_fullinteg_is_complete(NULL), 0);
    TEST_PASS();
}

/* =========================================================================
 * ne_fullinteg_report
 * ===================================================================== */

static void test_report_writes_all_items(void)
{
    NEFullIntegTable tbl;
    FILE            *fp;
    char             buf[2048];
    int              n;

    TEST_BEGIN("ne_fullinteg_report writes all item names");

    fp = tmpfile();
    ASSERT_NOT_NULL(fp);

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = fp;

    ne_fullinteg_report(&tbl);
    fflush(fp);

    rewind(fp);
    n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';

    ASSERT_NE(strstr(buf, "BOOT_SEQ"),        NULL);
    ASSERT_NE(strstr(buf, "RUNTIME_STABLE"),  NULL);
    ASSERT_NE(strstr(buf, "REGRESSION"),      NULL);
    ASSERT_NE(strstr(buf, "TEST_PROC"),       NULL);
    ASSERT_NE(strstr(buf, "LIMITATIONS"),     NULL);
    ASSERT_NE(strstr(buf, "CONFIG_REQS"),     NULL);
    ASSERT_NE(strstr(buf, "REPRO_BUILD"),     NULL);
    ASSERT_NE(strstr(buf, "BUILD_STEPS"),     NULL);
    ASSERT_NE(strstr(buf, "TEST_STEPS"),      NULL);
    ASSERT_NE(strstr(buf, "SIGNOFF"),         NULL);

    fclose(fp);
    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

static void test_report_shows_complete(void)
{
    NEFullIntegTable tbl;
    FILE            *fp;
    char             buf[2048];
    int              n;
    uint8_t          i;

    TEST_BEGIN("ne_fullinteg_report shows COMPLETE when all pass");

    fp = tmpfile();
    ASSERT_NOT_NULL(fp);

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = fp;

    for (i = 0; i < NE_FULLINTEG_ITEM_COUNT; i++)
        tbl.items[i].status = NE_FULLINTEG_STATUS_PASS;
    for (i = 0; i < NE_FULLINTEG_CL_COUNT; i++)
        tbl.checklist[i].done = 1u;

    ne_fullinteg_report(&tbl);
    fflush(fp);

    rewind(fp);
    n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';

    ASSERT_NE(strstr(buf, "COMPLETE"), NULL);

    fclose(fp);
    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

static void test_report_includes_notes(void)
{
    NEFullIntegTable tbl;
    FILE            *fp;
    char             buf[2048];
    int              n;

    TEST_BEGIN("ne_fullinteg_report includes notes text");

    fp = tmpfile();
    ASSERT_NOT_NULL(fp);

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = fp;

    ne_fullinteg_set_notes(&tbl, NE_FULLINTEG_ITEM_LIMITATIONS,
                            "No EMS/XMS in initial release");
    ne_fullinteg_checklist_set_notes(&tbl, NE_FULLINTEG_CL_SIGNOFF,
                                      "Signed off by lead developer");

    ne_fullinteg_report(&tbl);
    fflush(fp);

    rewind(fp);
    n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';

    ASSERT_NE(strstr(buf, "No EMS/XMS in initial release"),  NULL);
    ASSERT_NE(strstr(buf, "Signed off by lead developer"),   NULL);

    fclose(fp);
    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

static void test_report_null_safe(void)
{
    NEFullIntegTable tbl;

    TEST_BEGIN("ne_fullinteg_report with NULL tbl or NULL log_fp is safe");

    ne_fullinteg_report(NULL);   /* must not crash */

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = NULL;
    ne_fullinteg_report(&tbl);   /* must not crash */

    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * Full validation sequence
 * ===================================================================== */

static void test_full_validation_sequence(void)
{
    NEFullIntegTable tbl;
    uint8_t          i;

    TEST_BEGIN("full validation sequence completes end-to-end");

    ASSERT_EQ(ne_fullinteg_table_init(&tbl), NE_FULLINTEG_OK);
    tbl.log_fp = NULL;

    /* Not complete at start. */
    ASSERT_EQ(ne_fullinteg_is_complete(&tbl), 0);

    /* Advance each item through IN_PROGRESS to PASS. */
    for (i = 0; i < NE_FULLINTEG_ITEM_COUNT; i++) {
        ASSERT_EQ(ne_fullinteg_set_status(&tbl, i,
                                           NE_FULLINTEG_STATUS_IN_PROGRESS),
                  NE_FULLINTEG_OK);
        ASSERT_EQ(ne_fullinteg_set_status(&tbl, i,
                                           NE_FULLINTEG_STATUS_PASS),
                  NE_FULLINTEG_OK);
    }

    /* Still not complete: checklist pending. */
    ASSERT_EQ(ne_fullinteg_is_complete(&tbl), 0);

    /* Complete all checklist items. */
    ASSERT_EQ(ne_fullinteg_checklist_set(&tbl, NE_FULLINTEG_CL_BUILD_STEPS, 1u),
              NE_FULLINTEG_OK);
    ASSERT_EQ(ne_fullinteg_checklist_set(&tbl, NE_FULLINTEG_CL_TEST_STEPS, 1u),
              NE_FULLINTEG_OK);
    ASSERT_EQ(ne_fullinteg_checklist_set(&tbl, NE_FULLINTEG_CL_SIGNOFF, 1u),
              NE_FULLINTEG_OK);

    /* Now the milestone is complete. */
    ASSERT_NE(ne_fullinteg_is_complete(&tbl), 0);

    ne_fullinteg_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_fullinteg_item_name / ne_fullinteg_status_name /
 * ne_fullinteg_cl_name / ne_fullinteg_strerror
 * ===================================================================== */

static void test_item_names(void)
{
    TEST_BEGIN("ne_fullinteg_item_name exact string matches");

    ASSERT_EQ(strcmp(ne_fullinteg_item_name(NE_FULLINTEG_ITEM_BOOT_SEQ),
                     "BOOT_SEQ"),       0);
    ASSERT_EQ(strcmp(ne_fullinteg_item_name(NE_FULLINTEG_ITEM_RUNTIME_STABLE),
                     "RUNTIME_STABLE"), 0);
    ASSERT_EQ(strcmp(ne_fullinteg_item_name(NE_FULLINTEG_ITEM_REGRESSION),
                     "REGRESSION"),     0);
    ASSERT_EQ(strcmp(ne_fullinteg_item_name(NE_FULLINTEG_ITEM_TEST_PROC),
                     "TEST_PROC"),      0);
    ASSERT_EQ(strcmp(ne_fullinteg_item_name(NE_FULLINTEG_ITEM_LIMITATIONS),
                     "LIMITATIONS"),    0);
    ASSERT_EQ(strcmp(ne_fullinteg_item_name(NE_FULLINTEG_ITEM_CONFIG_REQS),
                     "CONFIG_REQS"),    0);
    ASSERT_EQ(strcmp(ne_fullinteg_item_name(NE_FULLINTEG_ITEM_REPRO_BUILD),
                     "REPRO_BUILD"),    0);
    ASSERT_EQ(strcmp(ne_fullinteg_item_name(0xFFu), "UNKNOWN"), 0);

    TEST_PASS();
}

static void test_status_names(void)
{
    TEST_BEGIN("ne_fullinteg_status_name exact string matches");

    ASSERT_EQ(strcmp(ne_fullinteg_status_name(NE_FULLINTEG_STATUS_PENDING),
                     "PENDING"),      0);
    ASSERT_EQ(strcmp(ne_fullinteg_status_name(NE_FULLINTEG_STATUS_IN_PROGRESS),
                     "IN_PROGRESS"),  0);
    ASSERT_EQ(strcmp(ne_fullinteg_status_name(NE_FULLINTEG_STATUS_PASS),
                     "PASS"),         0);
    ASSERT_EQ(strcmp(ne_fullinteg_status_name(NE_FULLINTEG_STATUS_FAIL),
                     "FAIL"),         0);
    ASSERT_EQ(strcmp(ne_fullinteg_status_name(0xFFu), "UNKNOWN"), 0);

    TEST_PASS();
}

static void test_cl_names(void)
{
    TEST_BEGIN("ne_fullinteg_cl_name exact string matches");

    ASSERT_EQ(strcmp(ne_fullinteg_cl_name(NE_FULLINTEG_CL_BUILD_STEPS),
                     "BUILD_STEPS"), 0);
    ASSERT_EQ(strcmp(ne_fullinteg_cl_name(NE_FULLINTEG_CL_TEST_STEPS),
                     "TEST_STEPS"),  0);
    ASSERT_EQ(strcmp(ne_fullinteg_cl_name(NE_FULLINTEG_CL_SIGNOFF),
                     "SIGNOFF"),     0);
    ASSERT_EQ(strcmp(ne_fullinteg_cl_name(0xFFu), "UNKNOWN"), 0);

    TEST_PASS();
}

static void test_strerror(void)
{
    TEST_BEGIN("ne_fullinteg_strerror returns non-NULL non-empty strings");

    ASSERT_NOT_NULL(ne_fullinteg_strerror(NE_FULLINTEG_OK));
    ASSERT_NOT_NULL(ne_fullinteg_strerror(NE_FULLINTEG_ERR_NULL));
    ASSERT_NOT_NULL(ne_fullinteg_strerror(NE_FULLINTEG_ERR_BAD_ITEM));
    ASSERT_NOT_NULL(ne_fullinteg_strerror(NE_FULLINTEG_ERR_BAD_STATUS));
    ASSERT_NOT_NULL(ne_fullinteg_strerror(NE_FULLINTEG_ERR_BAD_CL));
    ASSERT_NOT_NULL(ne_fullinteg_strerror(-999));

    ASSERT_NE(ne_fullinteg_strerror(NE_FULLINTEG_OK)[0],             '\0');
    ASSERT_NE(ne_fullinteg_strerror(NE_FULLINTEG_ERR_NULL)[0],       '\0');
    ASSERT_NE(ne_fullinteg_strerror(NE_FULLINTEG_ERR_BAD_ITEM)[0],   '\0');
    ASSERT_NE(ne_fullinteg_strerror(NE_FULLINTEG_ERR_BAD_STATUS)[0], '\0');
    ASSERT_NE(ne_fullinteg_strerror(NE_FULLINTEG_ERR_BAD_CL)[0],     '\0');

    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== WinDOS Full Integration Tests (Step 9) ===\n\n");

    printf("--- Full integration table ---\n");
    test_table_init_free();
    test_table_init_null();
    test_table_free_null();

    printf("\n--- Set status ---\n");
    test_set_status_progression();
    test_set_status_logs_transition();
    test_set_status_error_paths();

    printf("\n--- Set notes ---\n");
    test_set_notes_stored();
    test_set_notes_truncation();
    test_set_notes_error_paths();

    printf("\n--- Release checklist ---\n");
    test_checklist_set_done();
    test_checklist_set_logs();
    test_checklist_set_error_paths();
    test_checklist_notes_stored();
    test_checklist_notes_error_paths();

    printf("\n--- Completion check ---\n");
    test_is_complete_requires_all_pass();
    test_is_complete_fail_blocks();
    test_is_complete_null_safe();

    printf("\n--- Integration report ---\n");
    test_report_writes_all_items();
    test_report_shows_complete();
    test_report_includes_notes();
    test_report_null_safe();

    printf("\n--- Full validation sequence ---\n");
    test_full_validation_sequence();

    printf("\n--- Names and error strings ---\n");
    test_item_names();
    test_status_names();
    test_cl_names();
    test_strerror();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

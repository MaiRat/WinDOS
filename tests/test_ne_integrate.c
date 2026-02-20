/*
 * test_ne_integrate.c - Tests for Step 8: integration management
 *
 * Verifies:
 *   - ne_integ_table_init / ne_integ_table_free
 *   - ne_integ_set_status: valid transitions and gate enforcement
 *   - ne_integ_gate_check: passes when prerequisites met, fails otherwise
 *   - ne_integ_log_regression: counter increment and logging
 *   - ne_integ_set_gap / ne_integ_set_workaround / ne_integ_set_fallback
 *   - ne_integ_report: output written to log_fp
 *   - ne_integ_subsystem_name / ne_integ_status_name / ne_integ_strerror
 *   - Error-path coverage for all public API functions
 *
 * Build with Watcom (DOS target):
 *   wcc -ml -za99 -wx -d2 -i=../src ../src/ne_integrate.c test_ne_integrate.c
 *   wlink system dos name test_ne_integrate.exe file \
 *         test_ne_integrate.obj,ne_integrate.obj
 *
 * Build on POSIX host (CI):
 *   cc -std=c99 -Wall -I../src ../src/ne_integrate.c test_ne_integrate.c \
 *      -o test_ne_integrate
 */

#include "../src/ne_integrate.h"

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
 * Integration table – init / free
 * ===================================================================== */

static void test_integ_table_init_free(void)
{
    NEIntegTable tbl;
    uint8_t      i;

    TEST_BEGIN("integ table init and free");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);

    /* Every subsystem should start NOT_STARTED with zero regressions. */
    for (i = 0; i < NE_INTEG_SUBSYS_COUNT; i++) {
        ASSERT_EQ(tbl.entries[i].subsys_id,        (int)i);
        ASSERT_EQ(tbl.entries[i].status,            NE_INTEG_STATUS_NOT_STARTED);
        ASSERT_EQ(tbl.entries[i].regression_count,  0);
        ASSERT_EQ(tbl.entries[i].fallback_active,   0);
        ASSERT_EQ(tbl.entries[i].gap[0],            '\0');
        ASSERT_EQ(tbl.entries[i].workaround[0],     '\0');
    }

    /* Default log stream is stderr. */
    ASSERT_NOT_NULL(tbl.log_fp);

    ne_integ_table_free(&tbl);

    /* After free the table is zeroed. */
    ASSERT_NULL(tbl.log_fp);

    TEST_PASS();
}

static void test_integ_table_init_null(void)
{
    TEST_BEGIN("integ table init with NULL returns error");
    ASSERT_EQ(ne_integ_table_init(NULL), NE_INTEG_ERR_NULL);
    TEST_PASS();
}

static void test_integ_table_free_null(void)
{
    TEST_BEGIN("integ table free NULL is safe (no crash)");
    ne_integ_table_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * ne_integ_gate_check
 * ===================================================================== */

static void test_gate_kernel_no_deps(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("gate: KERNEL has no prerequisites (always passes)");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    /* KERNEL can advance regardless of all other subsystem states. */
    ASSERT_EQ(ne_integ_gate_check(&tbl, NE_INTEG_SUBSYS_KERNEL,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_OK);
    ASSERT_EQ(ne_integ_gate_check(&tbl, NE_INTEG_SUBSYS_KERNEL,
                                   NE_INTEG_STATUS_COMPAT_TESTED),
              NE_INTEG_OK);
    ASSERT_EQ(ne_integ_gate_check(&tbl, NE_INTEG_SUBSYS_KERNEL,
                                   NE_INTEG_STATUS_COMPLETE),
              NE_INTEG_OK);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_gate_drv_blocked_without_kernel(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("gate: DRV_DISPLAY blocked when KERNEL not COMPAT_TESTED");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    /* KERNEL is NOT_STARTED; DRV_DISPLAY must be blocked. */
    ASSERT_EQ(ne_integ_gate_check(&tbl, NE_INTEG_SUBSYS_DRV_DISPLAY,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_ERR_GATE);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_gate_drv_passes_after_kernel_compat(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("gate: DRV_DISPLAY passes after KERNEL reaches COMPAT_TESTED");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    /* Manually set KERNEL to COMPAT_TESTED. */
    tbl.entries[NE_INTEG_SUBSYS_KERNEL].status = NE_INTEG_STATUS_COMPAT_TESTED;

    ASSERT_EQ(ne_integ_gate_check(&tbl, NE_INTEG_SUBSYS_DRV_DISPLAY,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_OK);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_gate_gdi_blocked_without_display(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("gate: GDI blocked when DRV_DISPLAY not COMPAT_TESTED");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    /* KERNEL is COMPAT_TESTED but DRV_DISPLAY is only IN_PROGRESS. */
    tbl.entries[NE_INTEG_SUBSYS_KERNEL].status =
        NE_INTEG_STATUS_COMPAT_TESTED;
    tbl.entries[NE_INTEG_SUBSYS_DRV_DISPLAY].status =
        NE_INTEG_STATUS_IN_PROGRESS;

    ASSERT_EQ(ne_integ_gate_check(&tbl, NE_INTEG_SUBSYS_GDI,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_ERR_GATE);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_gate_gdi_passes_with_all_deps(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("gate: GDI passes when KERNEL and DRV_DISPLAY both COMPAT_TESTED");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    tbl.entries[NE_INTEG_SUBSYS_KERNEL].status =
        NE_INTEG_STATUS_COMPAT_TESTED;
    tbl.entries[NE_INTEG_SUBSYS_DRV_DISPLAY].status =
        NE_INTEG_STATUS_COMPAT_TESTED;

    ASSERT_EQ(ne_integ_gate_check(&tbl, NE_INTEG_SUBSYS_GDI,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_OK);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_gate_user_blocked_without_gdi(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("gate: USER blocked when GDI not COMPAT_TESTED");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    /* KERNEL is COMPAT_TESTED but GDI is only IN_PROGRESS. */
    tbl.entries[NE_INTEG_SUBSYS_KERNEL].status = NE_INTEG_STATUS_COMPAT_TESTED;
    tbl.entries[NE_INTEG_SUBSYS_GDI].status    = NE_INTEG_STATUS_IN_PROGRESS;

    ASSERT_EQ(ne_integ_gate_check(&tbl, NE_INTEG_SUBSYS_USER,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_ERR_GATE);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_gate_user_passes_with_all_deps(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("gate: USER passes when KERNEL and GDI both COMPAT_TESTED");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    tbl.entries[NE_INTEG_SUBSYS_KERNEL].status = NE_INTEG_STATUS_COMPAT_TESTED;
    tbl.entries[NE_INTEG_SUBSYS_GDI].status    = NE_INTEG_STATUS_COMPAT_TESTED;

    ASSERT_EQ(ne_integ_gate_check(&tbl, NE_INTEG_SUBSYS_USER,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_OK);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_gate_reset_to_not_started_always_allowed(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("gate: reset to NOT_STARTED always allowed");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    /* Even with no prerequisites met, resetting to NOT_STARTED is fine. */
    ASSERT_EQ(ne_integ_gate_check(&tbl, NE_INTEG_SUBSYS_USER,
                                   NE_INTEG_STATUS_NOT_STARTED),
              NE_INTEG_OK);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_gate_error_paths(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("gate: error paths (NULL, bad subsys, bad status)");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_integ_gate_check(NULL, NE_INTEG_SUBSYS_KERNEL,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_ERR_NULL);
    ASSERT_EQ(ne_integ_gate_check(&tbl, NE_INTEG_SUBSYS_COUNT,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_ERR_BAD_SUBSYS);
    ASSERT_EQ(ne_integ_gate_check(&tbl, NE_INTEG_SUBSYS_KERNEL,
                                   NE_INTEG_STATUS_COUNT),
              NE_INTEG_ERR_BAD_STATUS);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_integ_set_status
 * ===================================================================== */

static void test_set_status_kernel_full_progression(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("set_status: KERNEL advances through all states");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_KERNEL,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_OK);
    ASSERT_EQ(tbl.entries[NE_INTEG_SUBSYS_KERNEL].status,
              NE_INTEG_STATUS_IN_PROGRESS);

    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_KERNEL,
                                   NE_INTEG_STATUS_COMPAT_TESTED),
              NE_INTEG_OK);
    ASSERT_EQ(tbl.entries[NE_INTEG_SUBSYS_KERNEL].status,
              NE_INTEG_STATUS_COMPAT_TESTED);

    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_KERNEL,
                                   NE_INTEG_STATUS_COMPLETE),
              NE_INTEG_OK);
    ASSERT_EQ(tbl.entries[NE_INTEG_SUBSYS_KERNEL].status,
              NE_INTEG_STATUS_COMPLETE);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_set_status_gate_blocks_user(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("set_status: USER blocked by gate when deps not met");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    /* KERNEL and GDI are NOT_STARTED; USER must not advance. */
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_USER,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_ERR_GATE);

    /* State must remain NOT_STARTED. */
    ASSERT_EQ(tbl.entries[NE_INTEG_SUBSYS_USER].status,
              NE_INTEG_STATUS_NOT_STARTED);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_set_status_logs_transition(void)
{
    NEIntegTable tbl;
    FILE        *fp;
    char         buf[256];
    int          n;

    TEST_BEGIN("set_status: transition message written to log_fp");

    fp = tmpfile();
    ASSERT_NOT_NULL(fp);

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = fp;

    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_KERNEL,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_OK);
    fflush(fp);

    rewind(fp);
    n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';

    ASSERT_NE(strstr(buf, "KERNEL"),       NULL);
    ASSERT_NE(strstr(buf, "IN_PROGRESS"),  NULL);
    ASSERT_NE(strstr(buf, "NOT_STARTED"),  NULL);

    fclose(fp);
    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_set_status_error_paths(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("set_status: error paths (NULL, bad subsys, bad status)");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_integ_set_status(NULL, NE_INTEG_SUBSYS_KERNEL,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_ERR_NULL);
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_COUNT,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_ERR_BAD_SUBSYS);
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_KERNEL,
                                   NE_INTEG_STATUS_COUNT),
              NE_INTEG_ERR_BAD_STATUS);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_integ_log_regression
 * ===================================================================== */

static void test_regression_increments_counter(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("log_regression increments counter per subsystem");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_integ_log_regression(&tbl, NE_INTEG_SUBSYS_KERNEL,
                                       "display flicker on init"),
              NE_INTEG_OK);
    ASSERT_EQ(tbl.entries[NE_INTEG_SUBSYS_KERNEL].regression_count, 1);

    ASSERT_EQ(ne_integ_log_regression(&tbl, NE_INTEG_SUBSYS_KERNEL,
                                       "timer interrupt lost"),
              NE_INTEG_OK);
    ASSERT_EQ(tbl.entries[NE_INTEG_SUBSYS_KERNEL].regression_count, 2);

    /* Other subsystems unaffected. */
    ASSERT_EQ(tbl.entries[NE_INTEG_SUBSYS_USER].regression_count, 0);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_regression_logs_to_fp(void)
{
    NEIntegTable tbl;
    FILE        *fp;
    char         buf[256];
    int          n;

    TEST_BEGIN("log_regression writes message to log_fp");

    fp = tmpfile();
    ASSERT_NOT_NULL(fp);

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = fp;

    ASSERT_EQ(ne_integ_log_regression(&tbl, NE_INTEG_SUBSYS_GDI,
                                       "pixel corruption"),
              NE_INTEG_OK);
    fflush(fp);

    rewind(fp);
    n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';

    ASSERT_NE(strstr(buf, "GDI"),             NULL);
    ASSERT_NE(strstr(buf, "pixel corruption"), NULL);

    fclose(fp);
    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_regression_null_desc_safe(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("log_regression with NULL desc does not crash");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_integ_log_regression(&tbl, NE_INTEG_SUBSYS_KERNEL, NULL),
              NE_INTEG_OK);
    ASSERT_EQ(tbl.entries[NE_INTEG_SUBSYS_KERNEL].regression_count, 1);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_regression_error_paths(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("log_regression error paths");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_integ_log_regression(NULL, NE_INTEG_SUBSYS_KERNEL, NULL),
              NE_INTEG_ERR_NULL);
    ASSERT_EQ(ne_integ_log_regression(&tbl, NE_INTEG_SUBSYS_COUNT, NULL),
              NE_INTEG_ERR_BAD_SUBSYS);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_integ_set_gap / ne_integ_set_workaround / ne_integ_set_fallback
 * ===================================================================== */

static void test_set_gap_stored_and_retrieved(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("set_gap stores text in entry");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_integ_set_gap(&tbl, NE_INTEG_SUBSYS_GDI,
                                "BitBlt not yet implemented"),
              NE_INTEG_OK);

    ASSERT_EQ(strcmp(tbl.entries[NE_INTEG_SUBSYS_GDI].gap,
                     "BitBlt not yet implemented"), 0);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_set_workaround_stored_and_retrieved(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("set_workaround stores text in entry");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_integ_set_workaround(&tbl, NE_INTEG_SUBSYS_GDI,
                                       "use stub returning white rectangle"),
              NE_INTEG_OK);

    ASSERT_EQ(strcmp(tbl.entries[NE_INTEG_SUBSYS_GDI].workaround,
                     "use stub returning white rectangle"), 0);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_set_fallback_flag(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("set_fallback enables and disables fallback flag");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(tbl.entries[NE_INTEG_SUBSYS_USER].fallback_active, 0);

    ASSERT_EQ(ne_integ_set_fallback(&tbl, NE_INTEG_SUBSYS_USER, 1u),
              NE_INTEG_OK);
    ASSERT_NE(tbl.entries[NE_INTEG_SUBSYS_USER].fallback_active, 0);

    ASSERT_EQ(ne_integ_set_fallback(&tbl, NE_INTEG_SUBSYS_USER, 0u),
              NE_INTEG_OK);
    ASSERT_EQ(tbl.entries[NE_INTEG_SUBSYS_USER].fallback_active, 0);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_set_gap_error_paths(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("set_gap error paths");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_integ_set_gap(NULL, NE_INTEG_SUBSYS_GDI, "x"),
              NE_INTEG_ERR_NULL);
    ASSERT_EQ(ne_integ_set_gap(&tbl, NE_INTEG_SUBSYS_GDI, NULL),
              NE_INTEG_ERR_NULL);
    ASSERT_EQ(ne_integ_set_gap(&tbl, NE_INTEG_SUBSYS_COUNT, "x"),
              NE_INTEG_ERR_BAD_SUBSYS);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_set_workaround_error_paths(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("set_workaround error paths");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_integ_set_workaround(NULL, NE_INTEG_SUBSYS_GDI, "x"),
              NE_INTEG_ERR_NULL);
    ASSERT_EQ(ne_integ_set_workaround(&tbl, NE_INTEG_SUBSYS_GDI, NULL),
              NE_INTEG_ERR_NULL);
    ASSERT_EQ(ne_integ_set_workaround(&tbl, NE_INTEG_SUBSYS_COUNT, "x"),
              NE_INTEG_ERR_BAD_SUBSYS);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_set_fallback_error_paths(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("set_fallback error paths");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_integ_set_fallback(NULL, NE_INTEG_SUBSYS_USER, 1u),
              NE_INTEG_ERR_NULL);
    ASSERT_EQ(ne_integ_set_fallback(&tbl, NE_INTEG_SUBSYS_COUNT, 1u),
              NE_INTEG_ERR_BAD_SUBSYS);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_integ_report
 * ===================================================================== */

static void test_report_writes_all_subsystems(void)
{
    NEIntegTable tbl;
    FILE        *fp;
    char         buf[1024];
    int          n;

    TEST_BEGIN("ne_integ_report writes all subsystem names");

    fp = tmpfile();
    ASSERT_NOT_NULL(fp);

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = fp;

    ne_integ_report(&tbl);
    fflush(fp);

    rewind(fp);
    n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';

    ASSERT_NE(strstr(buf, "KERNEL"),       NULL);
    ASSERT_NE(strstr(buf, "USER"),         NULL);
    ASSERT_NE(strstr(buf, "GDI"),          NULL);
    ASSERT_NE(strstr(buf, "DRV_KEYBOARD"), NULL);
    ASSERT_NE(strstr(buf, "DRV_TIMER"),    NULL);
    ASSERT_NE(strstr(buf, "DRV_DISPLAY"),  NULL);

    fclose(fp);
    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_report_includes_gap_and_workaround(void)
{
    NEIntegTable tbl;
    FILE        *fp;
    char         buf[1024];
    int          n;

    TEST_BEGIN("ne_integ_report includes gap and workaround text");

    fp = tmpfile();
    ASSERT_NOT_NULL(fp);

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = fp;

    ne_integ_set_gap(&tbl, NE_INTEG_SUBSYS_GDI, "BitBlt stub only");
    ne_integ_set_workaround(&tbl, NE_INTEG_SUBSYS_GDI, "returns black rect");

    ne_integ_report(&tbl);
    fflush(fp);

    rewind(fp);
    n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';

    ASSERT_NE(strstr(buf, "BitBlt stub only"),  NULL);
    ASSERT_NE(strstr(buf, "returns black rect"), NULL);

    fclose(fp);
    ne_integ_table_free(&tbl);
    TEST_PASS();
}

static void test_report_null_safe(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("ne_integ_report with NULL tbl or NULL log_fp is safe");

    ne_integ_report(NULL);   /* must not crash */

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;
    ne_integ_report(&tbl);   /* must not crash */

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * Full integration sequence: KERNEL → drivers → GDI → USER
 * ===================================================================== */

static void test_full_integration_sequence(void)
{
    NEIntegTable tbl;

    TEST_BEGIN("full integration sequence gates correctly end-to-end");

    ASSERT_EQ(ne_integ_table_init(&tbl), NE_INTEG_OK);
    tbl.log_fp = NULL;

    /* Advance KERNEL through all stages. */
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_KERNEL,
                                   NE_INTEG_STATUS_IN_PROGRESS),   NE_INTEG_OK);
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_KERNEL,
                                   NE_INTEG_STATUS_COMPAT_TESTED), NE_INTEG_OK);

    /* Device drivers can now advance. */
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_DRV_KEYBOARD,
                                   NE_INTEG_STATUS_IN_PROGRESS),   NE_INTEG_OK);
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_DRV_TIMER,
                                   NE_INTEG_STATUS_IN_PROGRESS),   NE_INTEG_OK);
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_DRV_DISPLAY,
                                   NE_INTEG_STATUS_IN_PROGRESS),   NE_INTEG_OK);

    /* GDI still blocked: DRV_DISPLAY not yet COMPAT_TESTED. */
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_GDI,
                                   NE_INTEG_STATUS_IN_PROGRESS),
              NE_INTEG_ERR_GATE);

    /* Complete DRV_DISPLAY testing, then GDI can proceed. */
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_DRV_DISPLAY,
                                   NE_INTEG_STATUS_COMPAT_TESTED), NE_INTEG_OK);
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_GDI,
                                   NE_INTEG_STATUS_IN_PROGRESS),   NE_INTEG_OK);
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_GDI,
                                   NE_INTEG_STATUS_COMPAT_TESTED), NE_INTEG_OK);

    /* USER still blocked: KERNEL is COMPAT_TESTED but GDI just reached it. */
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_USER,
                                   NE_INTEG_STATUS_IN_PROGRESS),   NE_INTEG_OK);
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_USER,
                                   NE_INTEG_STATUS_COMPAT_TESTED), NE_INTEG_OK);
    ASSERT_EQ(ne_integ_set_status(&tbl, NE_INTEG_SUBSYS_USER,
                                   NE_INTEG_STATUS_COMPLETE),      NE_INTEG_OK);

    ASSERT_EQ(tbl.entries[NE_INTEG_SUBSYS_USER].status,
              NE_INTEG_STATUS_COMPLETE);

    ne_integ_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_integ_subsystem_name / ne_integ_status_name / ne_integ_strerror
 * ===================================================================== */

static void test_subsystem_names(void)
{
    TEST_BEGIN("ne_integ_subsystem_name exact string matches");

    ASSERT_EQ(strcmp(ne_integ_subsystem_name(NE_INTEG_SUBSYS_KERNEL),
                     "KERNEL"),       0);
    ASSERT_EQ(strcmp(ne_integ_subsystem_name(NE_INTEG_SUBSYS_USER),
                     "USER"),         0);
    ASSERT_EQ(strcmp(ne_integ_subsystem_name(NE_INTEG_SUBSYS_GDI),
                     "GDI"),          0);
    ASSERT_EQ(strcmp(ne_integ_subsystem_name(NE_INTEG_SUBSYS_DRV_KEYBOARD),
                     "DRV_KEYBOARD"), 0);
    ASSERT_EQ(strcmp(ne_integ_subsystem_name(NE_INTEG_SUBSYS_DRV_TIMER),
                     "DRV_TIMER"),    0);
    ASSERT_EQ(strcmp(ne_integ_subsystem_name(NE_INTEG_SUBSYS_DRV_DISPLAY),
                     "DRV_DISPLAY"),  0);
    ASSERT_EQ(strcmp(ne_integ_subsystem_name(0xFFu), "UNKNOWN"), 0);

    TEST_PASS();
}

static void test_status_names(void)
{
    TEST_BEGIN("ne_integ_status_name exact string matches");

    ASSERT_EQ(strcmp(ne_integ_status_name(NE_INTEG_STATUS_NOT_STARTED),
                     "NOT_STARTED"),   0);
    ASSERT_EQ(strcmp(ne_integ_status_name(NE_INTEG_STATUS_IN_PROGRESS),
                     "IN_PROGRESS"),   0);
    ASSERT_EQ(strcmp(ne_integ_status_name(NE_INTEG_STATUS_COMPAT_TESTED),
                     "COMPAT_TESTED"), 0);
    ASSERT_EQ(strcmp(ne_integ_status_name(NE_INTEG_STATUS_COMPLETE),
                     "COMPLETE"),      0);
    ASSERT_EQ(strcmp(ne_integ_status_name(0xFFu), "UNKNOWN"), 0);

    TEST_PASS();
}

static void test_strerror(void)
{
    TEST_BEGIN("ne_integ_strerror returns non-NULL non-empty strings");

    ASSERT_NOT_NULL(ne_integ_strerror(NE_INTEG_OK));
    ASSERT_NOT_NULL(ne_integ_strerror(NE_INTEG_ERR_NULL));
    ASSERT_NOT_NULL(ne_integ_strerror(NE_INTEG_ERR_BAD_SUBSYS));
    ASSERT_NOT_NULL(ne_integ_strerror(NE_INTEG_ERR_BAD_STATUS));
    ASSERT_NOT_NULL(ne_integ_strerror(NE_INTEG_ERR_GATE));
    ASSERT_NOT_NULL(ne_integ_strerror(NE_INTEG_ERR_REGRESSION));
    ASSERT_NOT_NULL(ne_integ_strerror(-999));

    ASSERT_NE(ne_integ_strerror(NE_INTEG_OK)[0],             '\0');
    ASSERT_NE(ne_integ_strerror(NE_INTEG_ERR_NULL)[0],       '\0');
    ASSERT_NE(ne_integ_strerror(NE_INTEG_ERR_GATE)[0],       '\0');
    ASSERT_NE(ne_integ_strerror(NE_INTEG_ERR_REGRESSION)[0], '\0');

    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== WinDOS Integration Management Tests (Step 8) ===\n\n");

    printf("--- Integration table ---\n");
    test_integ_table_init_free();
    test_integ_table_init_null();
    test_integ_table_free_null();

    printf("\n--- Compatibility gate ---\n");
    test_gate_kernel_no_deps();
    test_gate_drv_blocked_without_kernel();
    test_gate_drv_passes_after_kernel_compat();
    test_gate_gdi_blocked_without_display();
    test_gate_gdi_passes_with_all_deps();
    test_gate_user_blocked_without_gdi();
    test_gate_user_passes_with_all_deps();
    test_gate_reset_to_not_started_always_allowed();
    test_gate_error_paths();

    printf("\n--- Set status ---\n");
    test_set_status_kernel_full_progression();
    test_set_status_gate_blocks_user();
    test_set_status_logs_transition();
    test_set_status_error_paths();

    printf("\n--- Regression tracking ---\n");
    test_regression_increments_counter();
    test_regression_logs_to_fp();
    test_regression_null_desc_safe();
    test_regression_error_paths();

    printf("\n--- Gap / workaround / fallback ---\n");
    test_set_gap_stored_and_retrieved();
    test_set_workaround_stored_and_retrieved();
    test_set_fallback_flag();
    test_set_gap_error_paths();
    test_set_workaround_error_paths();
    test_set_fallback_error_paths();

    printf("\n--- Integration report ---\n");
    test_report_writes_all_subsystems();
    test_report_includes_gap_and_workaround();
    test_report_null_safe();

    printf("\n--- Full integration sequence ---\n");
    test_full_integration_sequence();

    printf("\n--- Names and error strings ---\n");
    test_subsystem_names();
    test_status_names();
    test_strerror();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

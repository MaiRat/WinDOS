/*
 * test_ne_compat.c - Tests for Phase 6: Compatibility Testing and Hardening
 *
 * Verifies:
 *   - Context initialisation and teardown
 *   - System DLL validation (KERNEL.EXE, USER.EXE, GDI.EXE)
 *   - Module loading, relocation, and import resolution checks
 *   - Memory profiling and leak detection
 *   - Scheduler stress testing with multiple concurrent tasks
 *   - Known limitations tracking
 *   - Compatibility matrix (application x subsystem x status)
 *   - Error handling and error strings
 */

#include "../src/ne_compat.h"

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

static void test_compat_init_free(void)
{
    NECompatContext ctx;
    TEST_BEGIN("init sets initialized=1, free sets it to 0");
    ASSERT_EQ(ne_compat_init(&ctx), NE_COMPAT_OK);
    ASSERT_EQ(ctx.initialized, 1);
    ASSERT_EQ(ctx.dll_count, 0);
    ASSERT_EQ(ctx.limitation_count, 0);
    ASSERT_EQ(ctx.matrix_count, 0);
    ne_compat_free(&ctx);
    ASSERT_EQ(ctx.initialized, 0);
    TEST_PASS();
}

static void test_compat_init_null(void)
{
    TEST_BEGIN("init with NULL ctx returns ERR_NULL");
    ASSERT_EQ(ne_compat_init(NULL), NE_COMPAT_ERR_NULL);
    TEST_PASS();
}

static void test_compat_free_null_safe(void)
{
    TEST_BEGIN("free(NULL) does not crash");
    ne_compat_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * System DLL validation tests
 * ===================================================================== */

/*
 * Build a minimal NE binary image: just the 'NE' signature at offset 0.
 * This is the simplest valid NE header for validation purposes.
 */
static const uint8_t g_ne_minimal[] = {
    0x4E, 0x45,  /* 'N', 'E' signature */
    0x00, 0x00,  /* linker version */
    0x00, 0x00, 0x00, 0x00  /* padding */
};

static void test_validate_kernel_dll(void)
{
    NECompatContext ctx;
    int status;
    const NECompatDLLEntry *entry;

    TEST_BEGIN("validate KERNEL.EXE with valid NE image");
    ne_compat_init(&ctx);
    status = ne_compat_validate_dll(&ctx, "KERNEL.EXE",
                                    g_ne_minimal, sizeof(g_ne_minimal));
    ASSERT_EQ(status, (int)NE_COMPAT_DLL_VALIDATED);
    entry = ne_compat_get_dll_status(&ctx, "KERNEL.EXE");
    ASSERT_NOT_NULL(entry);
    ASSERT_STR_EQ(entry->name, "KERNEL.EXE");
    ASSERT_EQ(entry->status, NE_COMPAT_DLL_VALIDATED);
    ASSERT_NE(entry->export_count, 0);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_validate_user_dll(void)
{
    NECompatContext ctx;
    int status;
    const NECompatDLLEntry *entry;

    TEST_BEGIN("validate USER.EXE with valid NE image");
    ne_compat_init(&ctx);
    status = ne_compat_validate_dll(&ctx, "USER.EXE",
                                    g_ne_minimal, sizeof(g_ne_minimal));
    ASSERT_EQ(status, (int)NE_COMPAT_DLL_VALIDATED);
    entry = ne_compat_get_dll_status(&ctx, "USER.EXE");
    ASSERT_NOT_NULL(entry);
    ASSERT_STR_EQ(entry->name, "USER.EXE");
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_validate_gdi_dll(void)
{
    NECompatContext ctx;
    int status;
    const NECompatDLLEntry *entry;

    TEST_BEGIN("validate GDI.EXE with valid NE image");
    ne_compat_init(&ctx);
    status = ne_compat_validate_dll(&ctx, "GDI.EXE",
                                    g_ne_minimal, sizeof(g_ne_minimal));
    ASSERT_EQ(status, (int)NE_COMPAT_DLL_VALIDATED);
    entry = ne_compat_get_dll_status(&ctx, "GDI.EXE");
    ASSERT_NOT_NULL(entry);
    ASSERT_STR_EQ(entry->name, "GDI.EXE");
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_validate_all_system_dlls(void)
{
    NECompatContext ctx;

    TEST_BEGIN("validate all three system DLLs in one context");
    ne_compat_init(&ctx);
    ASSERT_EQ(ne_compat_validate_dll(&ctx, "KERNEL.EXE",
                                     g_ne_minimal, sizeof(g_ne_minimal)),
              (int)NE_COMPAT_DLL_VALIDATED);
    ASSERT_EQ(ne_compat_validate_dll(&ctx, "USER.EXE",
                                     g_ne_minimal, sizeof(g_ne_minimal)),
              (int)NE_COMPAT_DLL_VALIDATED);
    ASSERT_EQ(ne_compat_validate_dll(&ctx, "GDI.EXE",
                                     g_ne_minimal, sizeof(g_ne_minimal)),
              (int)NE_COMPAT_DLL_VALIDATED);
    ASSERT_EQ(ctx.dll_count, 3);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_validate_dll_invalid_image(void)
{
    NECompatContext ctx;
    int status;
    static const uint8_t bad_data[] = { 0xFF, 0xFF, 0x00, 0x00 };

    TEST_BEGIN("validate DLL with invalid NE image sets LOAD_FAIL");
    ne_compat_init(&ctx);
    status = ne_compat_validate_dll(&ctx, "BAD.EXE",
                                    bad_data, sizeof(bad_data));
    ASSERT_EQ(status & NE_COMPAT_DLL_LOAD_FAIL, NE_COMPAT_DLL_LOAD_FAIL);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_validate_dll_no_data(void)
{
    NECompatContext ctx;
    int status;

    TEST_BEGIN("validate DLL with no data returns NOT_TESTED");
    ne_compat_init(&ctx);
    status = ne_compat_validate_dll(&ctx, "EMPTY.EXE", NULL, 0);
    ASSERT_EQ(status, (int)NE_COMPAT_DLL_NOT_TESTED);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_validate_dll_mz_ne(void)
{
    NECompatContext ctx;
    int status;
    /*
     * Build a minimal MZ+NE image:
     * MZ header at offset 0, NE header offset at 0x3C pointing to 0x40,
     * NE signature at offset 0x40.
     */
    uint8_t mz_ne[68];
    memset(mz_ne, 0, sizeof(mz_ne));
    mz_ne[0] = 0x4D; mz_ne[1] = 0x5A;  /* 'MZ' */
    mz_ne[0x3C] = 0x40;                 /* NE offset = 64 */
    mz_ne[0x40] = 0x4E; mz_ne[0x41] = 0x45;  /* 'NE' */

    TEST_BEGIN("validate DLL with MZ+NE image succeeds");
    ne_compat_init(&ctx);
    status = ne_compat_validate_dll(&ctx, "MZNE.DLL", mz_ne, sizeof(mz_ne));
    ASSERT_EQ(status, (int)NE_COMPAT_DLL_VALIDATED);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_validate_dll_revalidation(void)
{
    NECompatContext ctx;
    const NECompatDLLEntry *entry;

    TEST_BEGIN("re-validating same DLL updates existing entry");
    ne_compat_init(&ctx);
    ne_compat_validate_dll(&ctx, "KERNEL.EXE", NULL, 0);
    entry = ne_compat_get_dll_status(&ctx, "KERNEL.EXE");
    ASSERT_NOT_NULL(entry);
    ASSERT_EQ(entry->status, NE_COMPAT_DLL_NOT_TESTED);
    ASSERT_EQ(ctx.dll_count, 1);

    ne_compat_validate_dll(&ctx, "KERNEL.EXE",
                           g_ne_minimal, sizeof(g_ne_minimal));
    ASSERT_EQ(ctx.dll_count, 1);  /* count unchanged */
    entry = ne_compat_get_dll_status(&ctx, "KERNEL.EXE");
    ASSERT_EQ(entry->status, NE_COMPAT_DLL_VALIDATED);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_validate_dll_null_args(void)
{
    NECompatContext ctx;

    TEST_BEGIN("validate_dll NULL arg checks");
    ASSERT_EQ(ne_compat_validate_dll(NULL, "X", NULL, 0),
              NE_COMPAT_ERR_NULL);
    ne_compat_init(&ctx);
    ASSERT_EQ(ne_compat_validate_dll(&ctx, NULL, NULL, 0),
              NE_COMPAT_ERR_NULL);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_get_dll_status_not_found(void)
{
    NECompatContext ctx;

    TEST_BEGIN("get_dll_status returns NULL for unknown DLL");
    ne_compat_init(&ctx);
    ASSERT_NULL(ne_compat_get_dll_status(&ctx, "UNKNOWN.EXE"));
    ne_compat_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Memory profiling tests
 * ===================================================================== */

static void test_mem_profile_alloc_free(void)
{
    NECompatContext ctx;
    NEMemProfile   snap;

    TEST_BEGIN("mem_profile tracks allocations and frees");
    ne_compat_init(&ctx);
    ne_compat_mem_profile_alloc(&ctx, 100);
    ne_compat_mem_profile_alloc(&ctx, 200);
    ne_compat_mem_profile_free(&ctx, 100);

    ASSERT_EQ(ne_compat_mem_profile_snapshot(&ctx, &snap), NE_COMPAT_OK);
    ASSERT_EQ(snap.total_allocs, 2u);
    ASSERT_EQ(snap.total_frees, 1u);
    ASSERT_EQ(snap.current_blocks, 1u);
    ASSERT_EQ(snap.total_bytes_alloc, 300u);
    ASSERT_EQ(snap.total_bytes_freed, 100u);
    ASSERT_EQ(snap.current_bytes, 200u);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_mem_profile_peak(void)
{
    NECompatContext ctx;
    NEMemProfile   snap;

    TEST_BEGIN("mem_profile records peak usage");
    ne_compat_init(&ctx);
    ne_compat_mem_profile_alloc(&ctx, 500);
    ne_compat_mem_profile_alloc(&ctx, 300);
    ne_compat_mem_profile_free(&ctx, 500);
    ne_compat_mem_profile_free(&ctx, 300);

    ASSERT_EQ(ne_compat_mem_profile_snapshot(&ctx, &snap), NE_COMPAT_OK);
    ASSERT_EQ(snap.peak_bytes, 800u);
    ASSERT_EQ(snap.peak_blocks, 2u);
    ASSERT_EQ(snap.current_bytes, 0u);
    ASSERT_EQ(snap.current_blocks, 0u);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_mem_profile_has_leaks(void)
{
    NECompatContext ctx;

    TEST_BEGIN("mem_profile_has_leaks detects outstanding allocations");
    ne_compat_init(&ctx);
    ne_compat_mem_profile_alloc(&ctx, 64);
    ASSERT_EQ(ne_compat_mem_profile_has_leaks(&ctx), 1);
    ne_compat_mem_profile_free(&ctx, 64);
    ASSERT_EQ(ne_compat_mem_profile_has_leaks(&ctx), 0);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_mem_profile_reset(void)
{
    NECompatContext ctx;
    NEMemProfile   snap;

    TEST_BEGIN("mem_profile_reset clears all counters");
    ne_compat_init(&ctx);
    ne_compat_mem_profile_alloc(&ctx, 1024);
    ne_compat_mem_profile_reset(&ctx);
    ASSERT_EQ(ne_compat_mem_profile_snapshot(&ctx, &snap), NE_COMPAT_OK);
    ASSERT_EQ(snap.total_allocs, 0u);
    ASSERT_EQ(snap.current_bytes, 0u);
    ASSERT_EQ(snap.peak_bytes, 0u);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_mem_profile_null_args(void)
{
    NEMemProfile snap;

    TEST_BEGIN("mem_profile NULL arg checks");
    ASSERT_EQ(ne_compat_mem_profile_snapshot(NULL, &snap),
              NE_COMPAT_ERR_NULL);
    ASSERT_EQ(ne_compat_mem_profile_has_leaks(NULL),
              NE_COMPAT_ERR_NULL);
    /* These should not crash */
    ne_compat_mem_profile_reset(NULL);
    ne_compat_mem_profile_alloc(NULL, 100);
    ne_compat_mem_profile_free(NULL, 100);
    TEST_PASS();
}

/* =========================================================================
 * Scheduler stress tests
 * ===================================================================== */

static void test_stress_scheduler_basic(void)
{
    NECompatContext ctx;

    TEST_BEGIN("stress_scheduler with 4 tasks x 10 iterations");
    ne_compat_init(&ctx);
    ASSERT_EQ(ne_compat_stress_scheduler(&ctx, 4, 10), NE_COMPAT_OK);
    ASSERT_EQ(ctx.sched_result.tasks_created, 4);
    ASSERT_EQ(ctx.sched_result.tasks_completed, 4);
    ASSERT_EQ(ctx.sched_result.all_completed, 1);
    ASSERT_EQ(ctx.sched_result.total_yields, 40u);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_stress_scheduler_single_task(void)
{
    NECompatContext ctx;

    TEST_BEGIN("stress_scheduler with 1 task x 100 iterations");
    ne_compat_init(&ctx);
    ASSERT_EQ(ne_compat_stress_scheduler(&ctx, 1, 100), NE_COMPAT_OK);
    ASSERT_EQ(ctx.sched_result.tasks_created, 1);
    ASSERT_EQ(ctx.sched_result.tasks_completed, 1);
    ASSERT_EQ(ctx.sched_result.all_completed, 1);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_stress_scheduler_max_tasks(void)
{
    NECompatContext ctx;

    TEST_BEGIN("stress_scheduler with max tasks succeeds");
    ne_compat_init(&ctx);
    ASSERT_EQ(ne_compat_stress_scheduler(&ctx,
                                         NE_COMPAT_STRESS_MAX_TASKS, 5),
              NE_COMPAT_OK);
    ASSERT_EQ(ctx.sched_result.tasks_created, NE_COMPAT_STRESS_MAX_TASKS);
    ASSERT_EQ(ctx.sched_result.all_completed, 1);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_stress_scheduler_zero_tasks(void)
{
    NECompatContext ctx;

    TEST_BEGIN("stress_scheduler with 0 tasks returns ERR_BAD_DATA");
    ne_compat_init(&ctx);
    ASSERT_EQ(ne_compat_stress_scheduler(&ctx, 0, 10),
              NE_COMPAT_ERR_BAD_DATA);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_stress_scheduler_too_many(void)
{
    NECompatContext ctx;

    TEST_BEGIN("stress_scheduler beyond max returns ERR_BAD_DATA");
    ne_compat_init(&ctx);
    ASSERT_EQ(ne_compat_stress_scheduler(&ctx,
                                         NE_COMPAT_STRESS_MAX_TASKS + 1, 5),
              NE_COMPAT_ERR_BAD_DATA);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_stress_scheduler_null_ctx(void)
{
    TEST_BEGIN("stress_scheduler with NULL ctx returns ERR_NULL");
    ASSERT_EQ(ne_compat_stress_scheduler(NULL, 4, 10),
              NE_COMPAT_ERR_NULL);
    TEST_PASS();
}

/* =========================================================================
 * Known limitations tests
 * ===================================================================== */

static void test_limitation_add_and_query(void)
{
    NECompatContext ctx;
    const NECompatLimitation *lim;

    TEST_BEGIN("add limitation and query by index");
    ne_compat_init(&ctx);
    ASSERT_EQ(ne_compat_add_limitation(&ctx,
        "ExitWindows", "Not implemented; stub returns 0",
        NE_COMPAT_SEV_WARNING, NE_COMPAT_SUB_KERNEL), NE_COMPAT_OK);
    ASSERT_EQ(ne_compat_get_limitation_count(&ctx), 1);
    lim = ne_compat_get_limitation(&ctx, 0);
    ASSERT_NOT_NULL(lim);
    ASSERT_STR_EQ(lim->api_name, "ExitWindows");
    ASSERT_EQ(lim->severity, NE_COMPAT_SEV_WARNING);
    ASSERT_EQ(lim->subsystem, NE_COMPAT_SUB_KERNEL);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_limitation_multiple(void)
{
    NECompatContext ctx;

    TEST_BEGIN("add multiple limitations; count is correct");
    ne_compat_init(&ctx);
    ne_compat_add_limitation(&ctx, "ExitWindows", "stub",
                             NE_COMPAT_SEV_WARNING, NE_COMPAT_SUB_KERNEL);
    ne_compat_add_limitation(&ctx, "SetCapture", "no mouse capture",
                             NE_COMPAT_SEV_INFO, NE_COMPAT_SUB_USER);
    ne_compat_add_limitation(&ctx, "BitBlt", "no raster ops",
                             NE_COMPAT_SEV_CRITICAL, NE_COMPAT_SUB_GDI);
    ASSERT_EQ(ne_compat_get_limitation_count(&ctx), 3);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_limitation_out_of_range(void)
{
    NECompatContext ctx;

    TEST_BEGIN("get_limitation returns NULL for out-of-range index");
    ne_compat_init(&ctx);
    ASSERT_NULL(ne_compat_get_limitation(&ctx, 0));
    ASSERT_NULL(ne_compat_get_limitation(&ctx, 100));
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_limitation_null_args(void)
{
    NECompatContext ctx;

    TEST_BEGIN("limitation NULL arg checks");
    ne_compat_init(&ctx);
    ASSERT_EQ(ne_compat_add_limitation(NULL, "X", "Y", 0, 0),
              NE_COMPAT_ERR_NULL);
    ASSERT_EQ(ne_compat_add_limitation(&ctx, NULL, "Y", 0, 0),
              NE_COMPAT_ERR_NULL);
    ASSERT_EQ(ne_compat_add_limitation(&ctx, "X", NULL, 0, 0),
              NE_COMPAT_ERR_NULL);
    ASSERT_EQ(ne_compat_get_limitation_count(NULL), 0);
    ASSERT_NULL(ne_compat_get_limitation(NULL, 0));
    ne_compat_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Compatibility matrix tests
 * ===================================================================== */

static void test_matrix_add_and_query(void)
{
    NECompatContext ctx;
    const NECompatMatrixEntry *entry;
    uint8_t sub_status[NE_COMPAT_SUB_COUNT];

    TEST_BEGIN("add matrix entry and query by app name");
    ne_compat_init(&ctx);
    memset(sub_status, NE_COMPAT_STATUS_PASS, NE_COMPAT_SUB_COUNT);
    sub_status[NE_COMPAT_SUB_GDI] = NE_COMPAT_STATUS_PARTIAL;

    ASSERT_EQ(ne_compat_matrix_add(&ctx, "NOTEPAD.EXE",
                                   sub_status, NE_COMPAT_STATUS_PARTIAL),
              NE_COMPAT_OK);
    ASSERT_EQ(ne_compat_matrix_count(&ctx), 1);

    entry = ne_compat_matrix_get(&ctx, "NOTEPAD.EXE");
    ASSERT_NOT_NULL(entry);
    ASSERT_STR_EQ(entry->app_name, "NOTEPAD.EXE");
    ASSERT_EQ(entry->overall_status, NE_COMPAT_STATUS_PARTIAL);
    ASSERT_EQ(entry->subsystem_status[NE_COMPAT_SUB_KERNEL],
              NE_COMPAT_STATUS_PASS);
    ASSERT_EQ(entry->subsystem_status[NE_COMPAT_SUB_GDI],
              NE_COMPAT_STATUS_PARTIAL);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_matrix_multiple_apps(void)
{
    NECompatContext ctx;
    uint8_t sub_status[NE_COMPAT_SUB_COUNT];

    TEST_BEGIN("add multiple apps to matrix; count is correct");
    ne_compat_init(&ctx);
    memset(sub_status, NE_COMPAT_STATUS_PASS, NE_COMPAT_SUB_COUNT);

    ne_compat_matrix_add(&ctx, "NOTEPAD.EXE", sub_status,
                         NE_COMPAT_STATUS_PASS);
    ne_compat_matrix_add(&ctx, "CALC.EXE", sub_status,
                         NE_COMPAT_STATUS_PASS);
    ne_compat_matrix_add(&ctx, "WRITE.EXE", sub_status,
                         NE_COMPAT_STATUS_PARTIAL);
    ASSERT_EQ(ne_compat_matrix_count(&ctx), 3);
    ASSERT_NOT_NULL(ne_compat_matrix_get(&ctx, "NOTEPAD.EXE"));
    ASSERT_NOT_NULL(ne_compat_matrix_get(&ctx, "CALC.EXE"));
    ASSERT_NOT_NULL(ne_compat_matrix_get(&ctx, "WRITE.EXE"));
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_matrix_update_existing(void)
{
    NECompatContext ctx;
    const NECompatMatrixEntry *entry;
    uint8_t sub_pass[NE_COMPAT_SUB_COUNT];
    uint8_t sub_fail[NE_COMPAT_SUB_COUNT];

    TEST_BEGIN("updating existing matrix entry does not add duplicate");
    ne_compat_init(&ctx);
    memset(sub_pass, NE_COMPAT_STATUS_PASS, NE_COMPAT_SUB_COUNT);
    memset(sub_fail, NE_COMPAT_STATUS_FAIL, NE_COMPAT_SUB_COUNT);

    ne_compat_matrix_add(&ctx, "NOTEPAD.EXE", sub_pass,
                         NE_COMPAT_STATUS_PASS);
    ASSERT_EQ(ne_compat_matrix_count(&ctx), 1);

    ne_compat_matrix_add(&ctx, "NOTEPAD.EXE", sub_fail,
                         NE_COMPAT_STATUS_FAIL);
    ASSERT_EQ(ne_compat_matrix_count(&ctx), 1);  /* no duplicate */

    entry = ne_compat_matrix_get(&ctx, "NOTEPAD.EXE");
    ASSERT_EQ(entry->overall_status, NE_COMPAT_STATUS_FAIL);
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_matrix_not_found(void)
{
    NECompatContext ctx;

    TEST_BEGIN("matrix_get returns NULL for unknown app");
    ne_compat_init(&ctx);
    ASSERT_NULL(ne_compat_matrix_get(&ctx, "UNKNOWN.EXE"));
    ne_compat_free(&ctx);
    TEST_PASS();
}

static void test_matrix_null_args(void)
{
    NECompatContext ctx;
    uint8_t sub_status[NE_COMPAT_SUB_COUNT];

    TEST_BEGIN("matrix NULL arg checks");
    ne_compat_init(&ctx);
    memset(sub_status, 0, NE_COMPAT_SUB_COUNT);
    ASSERT_EQ(ne_compat_matrix_add(NULL, "X", sub_status, 0),
              NE_COMPAT_ERR_NULL);
    ASSERT_EQ(ne_compat_matrix_add(&ctx, NULL, sub_status, 0),
              NE_COMPAT_ERR_NULL);
    ASSERT_EQ(ne_compat_matrix_add(&ctx, "X", NULL, 0),
              NE_COMPAT_ERR_NULL);
    ASSERT_NULL(ne_compat_matrix_get(NULL, "X"));
    ASSERT_NULL(ne_compat_matrix_get(&ctx, NULL));
    ASSERT_EQ(ne_compat_matrix_count(NULL), 0);
    ne_compat_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Error string test
 * ===================================================================== */

static void test_compat_strerror(void)
{
    TEST_BEGIN("strerror returns non-NULL for all known codes");
    ASSERT_NOT_NULL(ne_compat_strerror(NE_COMPAT_OK));
    ASSERT_NOT_NULL(ne_compat_strerror(NE_COMPAT_ERR_NULL));
    ASSERT_NOT_NULL(ne_compat_strerror(NE_COMPAT_ERR_ALLOC));
    ASSERT_NOT_NULL(ne_compat_strerror(NE_COMPAT_ERR_FULL));
    ASSERT_NOT_NULL(ne_compat_strerror(NE_COMPAT_ERR_NOT_FOUND));
    ASSERT_NOT_NULL(ne_compat_strerror(NE_COMPAT_ERR_BAD_DATA));
    ASSERT_NOT_NULL(ne_compat_strerror(NE_COMPAT_ERR_VALIDATION));
    ASSERT_NOT_NULL(ne_compat_strerror(NE_COMPAT_ERR_INIT));
    ASSERT_NOT_NULL(ne_compat_strerror(-99));
    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== WinDOS Compatibility Testing Tests (Phase 6) ===\n");

    printf("\n--- Context tests ---\n");
    test_compat_init_free();
    test_compat_init_null();
    test_compat_free_null_safe();

    printf("\n--- System DLL validation tests ---\n");
    test_validate_kernel_dll();
    test_validate_user_dll();
    test_validate_gdi_dll();
    test_validate_all_system_dlls();
    test_validate_dll_invalid_image();
    test_validate_dll_no_data();
    test_validate_dll_mz_ne();
    test_validate_dll_revalidation();
    test_validate_dll_null_args();
    test_get_dll_status_not_found();

    printf("\n--- Memory profiling tests ---\n");
    test_mem_profile_alloc_free();
    test_mem_profile_peak();
    test_mem_profile_has_leaks();
    test_mem_profile_reset();
    test_mem_profile_null_args();

    printf("\n--- Scheduler stress tests ---\n");
    test_stress_scheduler_basic();
    test_stress_scheduler_single_task();
    test_stress_scheduler_max_tasks();
    test_stress_scheduler_zero_tasks();
    test_stress_scheduler_too_many();
    test_stress_scheduler_null_ctx();

    printf("\n--- Known limitations tests ---\n");
    test_limitation_add_and_query();
    test_limitation_multiple();
    test_limitation_out_of_range();
    test_limitation_null_args();

    printf("\n--- Compatibility matrix tests ---\n");
    test_matrix_add_and_query();
    test_matrix_multiple_apps();
    test_matrix_update_existing();
    test_matrix_not_found();
    test_matrix_null_args();

    printf("\n--- Error strings ---\n");
    test_compat_strerror();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

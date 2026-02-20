/*
 * test_ne_trap.c - Tests for Step 7: exception and trap handling
 *
 * Verifies:
 *   - ne_trap_table_init / ne_trap_table_free
 *   - ne_trap_install / ne_trap_remove
 *   - ne_trap_dispatch: custom handler invoked with correct arguments
 *   - ne_trap_dispatch: default handler path (log + recovery code)
 *   - Recovery codes: RETRY, SKIP, PANIC
 *   - Panic handler: custom panic_fn called for PANIC recovery
 *   - ne_trap_log: diagnostic output written to log_fp
 *   - ne_trap_vec_name: correct names for all defined vectors
 *   - ne_trap_strerror: string for each error code
 *   - Error-path coverage for all public API functions
 *
 * Build with Watcom (DOS target):
 *   wcc -ml -za99 -wx -d2 -i=../src ../src/ne_trap.c test_ne_trap.c
 *   wlink system dos name test_ne_trap.exe file test_ne_trap.obj,ne_trap.obj
 *
 * Build on POSIX host (CI):
 *   cc -std=c99 -Wall -I../src ../src/ne_trap.c test_ne_trap.c
 *      -o test_ne_trap
 */

#include "../src/ne_trap.h"

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

/* -------------------------------------------------------------------------
 * Helper: build a minimal NETrapContext for testing
 * ---------------------------------------------------------------------- */

static NETrapContext make_ctx(uint8_t vec, uint16_t error_code,
                               uint16_t cs, uint16_t ip)
{
    NETrapContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fault_vec  = vec;
    ctx.error_code = error_code;
    ctx.cs         = cs;
    ctx.ip         = ip;
    return ctx;
}

/* =========================================================================
 * Trap table – init / free
 * ===================================================================== */

static void test_trap_table_init_free(void)
{
    NETrapTable tbl;
    uint8_t     i;

    TEST_BEGIN("trap table init and free");

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);

    /* All handler slots should be NULL after init. */
    for (i = 0; i < NE_TRAP_VEC_COUNT; i++) {
        ASSERT_NULL(tbl.entries[i].fn);
        ASSERT_NULL(tbl.entries[i].user);
    }

    /* Default log stream is stderr. */
    ASSERT_NOT_NULL(tbl.log_fp);

    ne_trap_table_free(&tbl);

    /* After free the table is zeroed. */
    ASSERT_NULL(tbl.log_fp);

    TEST_PASS();
}

static void test_trap_table_init_null(void)
{
    TEST_BEGIN("trap table init with NULL returns error");
    ASSERT_EQ(ne_trap_table_init(NULL), NE_TRAP_ERR_NULL);
    TEST_PASS();
}

static void test_trap_table_free_null(void)
{
    TEST_BEGIN("trap table free NULL is safe (no crash)");
    ne_trap_table_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * ne_trap_install / ne_trap_remove
 * ===================================================================== */

/* Dummy handler used for install/remove tests. */
static int dummy_handler(uint8_t vec, const NETrapContext *ctx, void *user)
{
    (void)vec; (void)ctx; (void)user;
    return NE_TRAP_RECOVER_SKIP;
}

static void test_trap_install_basic(void)
{
    NETrapTable tbl;

    TEST_BEGIN("ne_trap_install stores handler and user pointer");

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_trap_install(&tbl, NE_TRAP_VEC_GP_FAULT,
                               dummy_handler, &tbl),
              NE_TRAP_OK);

    ASSERT_EQ((void *)tbl.entries[NE_TRAP_VEC_GP_FAULT].fn,
              (void *)dummy_handler);
    ASSERT_EQ(tbl.entries[NE_TRAP_VEC_GP_FAULT].user, (void *)&tbl);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

static void test_trap_install_null_fn_resets_to_default(void)
{
    NETrapTable tbl;

    TEST_BEGIN("ne_trap_install with NULL fn restores default");

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp = NULL;

    /* First install a real handler, then clear it. */
    ASSERT_EQ(ne_trap_install(&tbl, NE_TRAP_VEC_BREAKPOINT,
                               dummy_handler, NULL),
              NE_TRAP_OK);
    ASSERT_EQ(ne_trap_install(&tbl, NE_TRAP_VEC_BREAKPOINT, NULL, NULL),
              NE_TRAP_OK);

    ASSERT_NULL(tbl.entries[NE_TRAP_VEC_BREAKPOINT].fn);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

static void test_trap_install_errors(void)
{
    NETrapTable tbl;

    TEST_BEGIN("ne_trap_install error paths");

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_trap_install(NULL, NE_TRAP_VEC_GP_FAULT, dummy_handler, NULL),
              NE_TRAP_ERR_NULL);
    ASSERT_EQ(ne_trap_install(&tbl, NE_TRAP_VEC_COUNT, dummy_handler, NULL),
              NE_TRAP_ERR_BAD_VEC);
    ASSERT_EQ(ne_trap_install(&tbl, 0xFF, dummy_handler, NULL),
              NE_TRAP_ERR_BAD_VEC);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

static void test_trap_remove_basic(void)
{
    NETrapTable tbl;

    TEST_BEGIN("ne_trap_remove clears handler slot");

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_trap_install(&tbl, NE_TRAP_VEC_STACK_FAULT,
                               dummy_handler, &tbl),
              NE_TRAP_OK);
    ASSERT_EQ(ne_trap_remove(&tbl, NE_TRAP_VEC_STACK_FAULT), NE_TRAP_OK);

    ASSERT_NULL(tbl.entries[NE_TRAP_VEC_STACK_FAULT].fn);
    ASSERT_NULL(tbl.entries[NE_TRAP_VEC_STACK_FAULT].user);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

static void test_trap_remove_errors(void)
{
    NETrapTable tbl;

    TEST_BEGIN("ne_trap_remove error paths");

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_trap_remove(NULL, NE_TRAP_VEC_GP_FAULT),
              NE_TRAP_ERR_NULL);
    ASSERT_EQ(ne_trap_remove(&tbl, NE_TRAP_VEC_COUNT),
              NE_TRAP_ERR_BAD_VEC);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_trap_dispatch – custom handler invoked
 * ===================================================================== */

/*
 * Tracked dispatch state: allows test handlers to record calls.
 */
typedef struct {
    int     called;
    uint8_t last_vec;
    uint16_t last_error_code;
    int     return_code;
} DispatchRecord;

static int recording_handler(uint8_t vec, const NETrapContext *ctx, void *user)
{
    DispatchRecord *rec = (DispatchRecord *)user;
    rec->called++;
    rec->last_vec        = vec;
    rec->last_error_code = ctx ? ctx->error_code : 0;
    return rec->return_code;
}

static void test_dispatch_calls_custom_handler(void)
{
    NETrapTable    tbl;
    DispatchRecord rec;
    NETrapContext  ctx;
    int            rc;

    TEST_BEGIN("dispatch invokes custom handler with correct vec/ctx");

    memset(&rec, 0, sizeof(rec));
    rec.return_code = NE_TRAP_RECOVER_SKIP;

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_trap_install(&tbl, NE_TRAP_VEC_INVALID_OPCODE,
                               recording_handler, &rec),
              NE_TRAP_OK);

    ctx = make_ctx(NE_TRAP_VEC_INVALID_OPCODE, 0x0000u, 0x1234u, 0x0010u);
    rc  = ne_trap_dispatch(&tbl, NE_TRAP_VEC_INVALID_OPCODE, &ctx);

    ASSERT_EQ(rec.called,   1);
    ASSERT_EQ(rec.last_vec, (uint8_t)NE_TRAP_VEC_INVALID_OPCODE);
    ASSERT_EQ(rc,           NE_TRAP_RECOVER_SKIP);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

static void test_dispatch_handler_returns_retry(void)
{
    NETrapTable    tbl;
    DispatchRecord rec;
    NETrapContext  ctx;
    int            rc;

    TEST_BEGIN("dispatch forwards RETRY recovery code from handler");

    memset(&rec, 0, sizeof(rec));
    rec.return_code = NE_TRAP_RECOVER_RETRY;

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp = NULL;

    ASSERT_EQ(ne_trap_install(&tbl, NE_TRAP_VEC_BREAKPOINT,
                               recording_handler, &rec),
              NE_TRAP_OK);

    ctx = make_ctx(NE_TRAP_VEC_BREAKPOINT, 0, 0, 0);
    rc  = ne_trap_dispatch(&tbl, NE_TRAP_VEC_BREAKPOINT, &ctx);

    ASSERT_EQ(rc, NE_TRAP_RECOVER_RETRY);
    ASSERT_EQ(rec.called, 1);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

/* File-scope panic recorder used by the next test. */
static int g_panic_called = 0;

static void panic_recorder(const char *msg, const NETrapContext *ctx,
                            void *user)
{
    (void)msg; (void)ctx;
    *((int *)user) = 1;
}

static void test_dispatch_panic_path_calls_panic_fn(void)
{
    NETrapTable    tbl;
    DispatchRecord rec;
    NETrapContext  ctx;
    int            rc;

    TEST_BEGIN("dispatch calls panic_fn for PANIC recovery code");

    memset(&rec, 0, sizeof(rec));
    rec.return_code = NE_TRAP_RECOVER_PANIC;
    g_panic_called  = 0;

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp    = NULL;
    tbl.panic_fn  = panic_recorder;
    tbl.panic_user = &g_panic_called;

    ASSERT_EQ(ne_trap_install(&tbl, NE_TRAP_VEC_OVERFLOW,
                               recording_handler, &rec),
              NE_TRAP_OK);

    ctx = make_ctx(NE_TRAP_VEC_OVERFLOW, 0, 0, 0);
    rc  = ne_trap_dispatch(&tbl, NE_TRAP_VEC_OVERFLOW, &ctx);

    ASSERT_EQ(rc,              NE_TRAP_RECOVER_PANIC);
    ASSERT_EQ(rec.called,      1);
    ASSERT_EQ(g_panic_called,  1);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_trap_dispatch – default handler path
 * ===================================================================== */

static void test_dispatch_default_recoverable(void)
{
    NETrapTable   tbl;
    NETrapContext ctx;
    int           rc;

    TEST_BEGIN("default handler: recoverable vector returns SKIP");

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp = NULL; /* suppress output during test */

    /* NE_TRAP_VEC_SINGLE_STEP is not in the fatal set. */
    ctx = make_ctx(NE_TRAP_VEC_SINGLE_STEP, 0, 0, 0);
    rc  = ne_trap_dispatch(&tbl, NE_TRAP_VEC_SINGLE_STEP, &ctx);

    ASSERT_EQ(rc, NE_TRAP_RECOVER_SKIP);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

static void test_dispatch_default_gp_fault_is_fatal(void)
{
    NETrapTable   tbl;
    NETrapContext ctx;
    int           rc;

    TEST_BEGIN("default handler: GP fault invokes panic_fn");

    g_panic_called = 0;

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp    = NULL;
    tbl.panic_fn  = panic_recorder;
    tbl.panic_user = &g_panic_called;

    ctx = make_ctx(NE_TRAP_VEC_GP_FAULT, 0x0012u, 0x1000u, 0x0042u);
    rc  = ne_trap_dispatch(&tbl, NE_TRAP_VEC_GP_FAULT, &ctx);

    ASSERT_EQ(rc,             NE_TRAP_RECOVER_PANIC);
    ASSERT_EQ(g_panic_called, 1);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

static void test_dispatch_default_stack_fault_is_fatal(void)
{
    NETrapTable   tbl;
    NETrapContext ctx;
    int           rc;

    TEST_BEGIN("default handler: stack fault invokes panic_fn");

    g_panic_called = 0;

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp    = NULL;
    tbl.panic_fn  = panic_recorder;
    tbl.panic_user = &g_panic_called;

    ctx = make_ctx(NE_TRAP_VEC_STACK_FAULT, 0, 0, 0);
    rc  = ne_trap_dispatch(&tbl, NE_TRAP_VEC_STACK_FAULT, &ctx);

    ASSERT_EQ(rc,             NE_TRAP_RECOVER_PANIC);
    ASSERT_EQ(g_panic_called, 1);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

static void test_dispatch_default_divide_error_is_fatal(void)
{
    NETrapTable   tbl;
    NETrapContext ctx;
    int           rc;

    TEST_BEGIN("default handler: divide error invokes panic_fn");

    g_panic_called = 0;

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp    = NULL;
    tbl.panic_fn  = panic_recorder;
    tbl.panic_user = &g_panic_called;

    ctx = make_ctx(NE_TRAP_VEC_DIVIDE_ERROR, 0, 0, 0);
    rc  = ne_trap_dispatch(&tbl, NE_TRAP_VEC_DIVIDE_ERROR, &ctx);

    ASSERT_EQ(rc,             NE_TRAP_RECOVER_PANIC);
    ASSERT_EQ(g_panic_called, 1);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

static void test_dispatch_null_tbl(void)
{
    NETrapContext ctx;

    TEST_BEGIN("dispatch with NULL table returns PANIC");
    ctx = make_ctx(NE_TRAP_VEC_GP_FAULT, 0, 0, 0);
    ASSERT_EQ(ne_trap_dispatch(NULL, NE_TRAP_VEC_GP_FAULT, &ctx),
              NE_TRAP_RECOVER_PANIC);
    TEST_PASS();
}

/* =========================================================================
 * Multiple vectors – install all, dispatch each, verify dispatch
 * ===================================================================== */

static void test_dispatch_all_vectors_with_custom_handler(void)
{
    NETrapTable    tbl;
    DispatchRecord rec;
    NETrapContext  ctx;
    uint8_t        vec;

    TEST_BEGIN("custom handlers installed for all vectors dispatch correctly");

    memset(&rec, 0, sizeof(rec));
    rec.return_code = NE_TRAP_RECOVER_SKIP;

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp = NULL;

    /* Install the same recording handler on every slot. */
    for (vec = 0; vec < NE_TRAP_VEC_COUNT; vec++)
        ASSERT_EQ(ne_trap_install(&tbl, vec, recording_handler, &rec),
                  NE_TRAP_OK);

    /* Dispatch every vector and check the call counter increments. */
    for (vec = 0; vec < NE_TRAP_VEC_COUNT; vec++) {
        ctx = make_ctx(vec, 0, 0, 0);
        ASSERT_EQ(ne_trap_dispatch(&tbl, vec, &ctx), NE_TRAP_RECOVER_SKIP);
    }

    ASSERT_EQ(rec.called, (int)NE_TRAP_VEC_COUNT);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_trap_log – diagnostic output
 * ===================================================================== */

static void test_trap_log_writes_to_stream(void)
{
    NETrapTable   tbl;
    NETrapContext ctx;
    FILE         *fp;
    char          buf[512];
    int           n;

    TEST_BEGIN("ne_trap_log writes fault info to log_fp");

    fp = tmpfile();
    ASSERT_NOT_NULL(fp);

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp = fp;

    ctx = make_ctx(NE_TRAP_VEC_GP_FAULT, 0x0030u, 0x1000u, 0x0080u);
    ctx.ax = 0xAAAAu;
    ctx.bx = 0xBBBBu;

    ne_trap_log(&tbl, NE_TRAP_VEC_GP_FAULT, &ctx);
    fflush(fp);

    rewind(fp);
    n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
    buf[n] = '\0';

    /* Verify key fields appear in the output. */
    ASSERT_NE(strstr(buf, "GP_FAULT"), NULL);
    ASSERT_NE(strstr(buf, "0030"),     NULL);  /* error_code */
    ASSERT_NE(strstr(buf, "AAAA"),     NULL);  /* ax */

    fclose(fp);
    ne_trap_table_free(&tbl);
    TEST_PASS();
}

static void test_trap_log_null_fp_no_crash(void)
{
    NETrapTable   tbl;
    NETrapContext ctx;

    TEST_BEGIN("ne_trap_log with NULL log_fp does not crash");

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp = NULL;

    ctx = make_ctx(NE_TRAP_VEC_SINGLE_STEP, 0, 0, 0);
    ne_trap_log(&tbl, NE_TRAP_VEC_SINGLE_STEP, &ctx);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

static void test_trap_log_null_ctx(void)
{
    NETrapTable tbl;
    FILE       *fp;

    TEST_BEGIN("ne_trap_log with NULL ctx logs vec only");

    fp = tmpfile();
    ASSERT_NOT_NULL(fp);

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp = fp;

    ne_trap_log(&tbl, NE_TRAP_VEC_NMI, NULL);
    fflush(fp);

    /* Must not crash; content is minimal but present. */
    {
        char buf[256];
        int  n;
        rewind(fp);
        n = (int)fread(buf, 1, sizeof(buf) - 1, fp);
        buf[n] = '\0';
        ASSERT_NE(strstr(buf, "NMI"), NULL);
    }

    fclose(fp);
    ne_trap_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_trap_vec_name / ne_trap_strerror
 * ===================================================================== */

static void test_trap_vec_name_exact(void)
{
    TEST_BEGIN("ne_trap_vec_name exact string matches");

    ASSERT_EQ(strcmp(ne_trap_vec_name(NE_TRAP_VEC_DIVIDE_ERROR),  "DIVIDE_ERROR"),  0);
    ASSERT_EQ(strcmp(ne_trap_vec_name(NE_TRAP_VEC_GP_FAULT),      "GP_FAULT"),      0);
    ASSERT_EQ(strcmp(ne_trap_vec_name(NE_TRAP_VEC_STACK_FAULT),   "STACK_FAULT"),   0);
    ASSERT_EQ(strcmp(ne_trap_vec_name(NE_TRAP_VEC_DOUBLE_FAULT),  "DOUBLE_FAULT"),  0);
    ASSERT_EQ(strcmp(ne_trap_vec_name(NE_TRAP_VEC_BREAKPOINT),    "BREAKPOINT"),    0);
    ASSERT_EQ(strcmp(ne_trap_vec_name(NE_TRAP_VEC_SINGLE_STEP),   "SINGLE_STEP"),   0);
    ASSERT_EQ(strcmp(ne_trap_vec_name(NE_TRAP_VEC_NMI),           "NMI"),           0);
    ASSERT_EQ(strcmp(ne_trap_vec_name(NE_TRAP_VEC_OVERFLOW),      "OVERFLOW"),      0);
    ASSERT_EQ(strcmp(ne_trap_vec_name(NE_TRAP_VEC_BOUND),         "BOUND"),         0);
    ASSERT_EQ(strcmp(ne_trap_vec_name(NE_TRAP_VEC_INVALID_OPCODE),"INVALID_OPCODE"),0);
    ASSERT_EQ(strcmp(ne_trap_vec_name(NE_TRAP_VEC_NO_FPU),        "NO_FPU"),        0);
    ASSERT_EQ(strcmp(ne_trap_vec_name(NE_TRAP_VEC_FPU_SEG),       "FPU_SEG"),       0);
    ASSERT_EQ(strcmp(ne_trap_vec_name(0xFFu),                     "UNKNOWN"),       0);

    TEST_PASS();
}

static void test_trap_strerror(void)
{
    TEST_BEGIN("ne_trap_strerror returns non-NULL non-empty strings");

    ASSERT_NOT_NULL(ne_trap_strerror(NE_TRAP_OK));
    ASSERT_NOT_NULL(ne_trap_strerror(NE_TRAP_ERR_NULL));
    ASSERT_NOT_NULL(ne_trap_strerror(NE_TRAP_ERR_BAD_VEC));
    ASSERT_NOT_NULL(ne_trap_strerror(-999));

    ASSERT_NE(ne_trap_strerror(NE_TRAP_OK)[0],          '\0');
    ASSERT_NE(ne_trap_strerror(NE_TRAP_ERR_NULL)[0],    '\0');
    ASSERT_NE(ne_trap_strerror(NE_TRAP_ERR_BAD_VEC)[0], '\0');

    TEST_PASS();
}

/* =========================================================================
 * Recovery path integration: install handler, verify remove restores default
 * ===================================================================== */

static void test_remove_restores_default_behaviour(void)
{
    NETrapTable    tbl;
    DispatchRecord rec;
    NETrapContext  ctx;
    int            rc;

    TEST_BEGIN("remove restores default fatal path for GP fault");

    memset(&rec, 0, sizeof(rec));
    rec.return_code = NE_TRAP_RECOVER_SKIP; /* custom handler returns SKIP */
    g_panic_called  = 0;

    ASSERT_EQ(ne_trap_table_init(&tbl), NE_TRAP_OK);
    tbl.log_fp    = NULL;
    tbl.panic_fn  = panic_recorder;
    tbl.panic_user = &g_panic_called;

    /* With custom handler: GP fault returns SKIP (not PANIC). */
    ASSERT_EQ(ne_trap_install(&tbl, NE_TRAP_VEC_GP_FAULT,
                               recording_handler, &rec),
              NE_TRAP_OK);
    ctx = make_ctx(NE_TRAP_VEC_GP_FAULT, 0, 0, 0);
    rc  = ne_trap_dispatch(&tbl, NE_TRAP_VEC_GP_FAULT, &ctx);
    ASSERT_EQ(rc,             NE_TRAP_RECOVER_SKIP);
    ASSERT_EQ(g_panic_called, 0);

    /* After removing the handler: GP fault falls through to fatal default. */
    ASSERT_EQ(ne_trap_remove(&tbl, NE_TRAP_VEC_GP_FAULT), NE_TRAP_OK);
    g_panic_called = 0;
    rc = ne_trap_dispatch(&tbl, NE_TRAP_VEC_GP_FAULT, &ctx);
    ASSERT_EQ(rc,             NE_TRAP_RECOVER_PANIC);
    ASSERT_EQ(g_panic_called, 1);

    ne_trap_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== NE Exception and Trap Handling Tests (Step 7) ===\n\n");

    printf("--- Trap table ---\n");
    test_trap_table_init_free();
    test_trap_table_init_null();
    test_trap_table_free_null();

    printf("\n--- Install / remove ---\n");
    test_trap_install_basic();
    test_trap_install_null_fn_resets_to_default();
    test_trap_install_errors();
    test_trap_remove_basic();
    test_trap_remove_errors();

    printf("\n--- Dispatch: custom handler ---\n");
    test_dispatch_calls_custom_handler();
    test_dispatch_handler_returns_retry();
    test_dispatch_panic_path_calls_panic_fn();
    test_dispatch_all_vectors_with_custom_handler();

    printf("\n--- Dispatch: default handler ---\n");
    test_dispatch_default_recoverable();
    test_dispatch_default_gp_fault_is_fatal();
    test_dispatch_default_stack_fault_is_fatal();
    test_dispatch_default_divide_error_is_fatal();
    test_dispatch_null_tbl();

    printf("\n--- Fault logging ---\n");
    test_trap_log_writes_to_stream();
    test_trap_log_null_fp_no_crash();
    test_trap_log_null_ctx();

    printf("\n--- Vector names / error strings ---\n");
    test_trap_vec_name_exact();
    test_trap_strerror();

    printf("\n--- Recovery path integration ---\n");
    test_remove_restores_default_behaviour();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

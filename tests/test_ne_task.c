/*
 * test_ne_task.c - Tests for Step 6: task and memory management
 *
 * Verifies:
 *   - ne_task_table_init / ne_task_table_free
 *   - ne_task_create / ne_task_destroy
 *   - Cooperative scheduling: READY → RUNNING → TERMINATED state path
 *   - Yield and resume: RUNNING → YIELDED → RUNNING → TERMINATED
 *   - Task priority ordering (HIGH runs before LOW)
 *   - Memory ownership tracking (own_mem / disown_mem)
 *   - ne_gmem_table_init / ne_gmem_table_free
 *   - ne_gmem_alloc / ne_gmem_free / ne_gmem_lock / ne_gmem_unlock
 *   - ne_gmem_free_by_owner (task teardown cleanup)
 *   - ne_lmem_heap_init / ne_lmem_heap_free
 *   - ne_lmem_alloc / ne_lmem_free / ne_lmem_lock / ne_lmem_unlock
 *   - Error-path coverage for all public API functions
 *
 * Build with Watcom (DOS target):
 *   wcc -ml -za99 -wx -d2 -i=../src ../src/ne_task.c ../src/ne_mem.c
 *       test_ne_task.c
 *   wlink system dos name test_ne_task.exe
 *         file test_ne_task.obj,ne_task.obj,ne_mem.obj
 *
 * Build on POSIX host (CI):
 *   cc -std=c99 -Wall -I../src ../src/ne_task.c ../src/ne_mem.c
 *      test_ne_task.c -o test_ne_task
 */

#include "../src/ne_task.h"
#include "../src/ne_mem.h"

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
 * Task entry functions used by multiple tests
 * ===================================================================== */

/*
 * entry_set_flag – sets an int flag then returns immediately.
 * arg must point to an int initialised to 0.
 */
static void entry_set_flag(void *arg)
{
    *((int *)arg) = 1;
}

/*
 * entry_yield_once – yields once then increments the counter pointed to by
 * arg.  Used to test yield/resume round-trips.
 */
static void entry_yield_once(void *arg)
{
    int **pp = (int **)arg;
    NETaskTable *tbl  = (NETaskTable *)pp[0];
    int         *ctr  = (int *)pp[1];

    ne_task_yield(tbl);
    (*ctr)++;
}

/*
 * entry_yield_twice – yields twice, incrementing *ctr after each resume.
 */
static void entry_yield_twice(void *arg)
{
    int **pp = (int **)arg;
    NETaskTable *tbl = (NETaskTable *)pp[0];
    int         *ctr = (int *)pp[1];

    ne_task_yield(tbl);
    (*ctr)++;
    ne_task_yield(tbl);
    (*ctr)++;
}

/*
 * entry_priority_record – records the order of execution in a shared array.
 * arg is a pointer to a struct { NETaskTable *tbl; int *log; int *idx; }.
 */
typedef struct {
    NETaskTable *tbl;
    int         *log;
    int         *idx;
    int          id;
} PriorityArg;

static void entry_priority_record(void *arg)
{
    PriorityArg *pa = (PriorityArg *)arg;
    pa->log[(*pa->idx)++] = pa->id;
}

/* =========================================================================
 * Task table – init / free
 * ===================================================================== */

static void test_task_table_init_free(void)
{
    NETaskTable tbl;

    TEST_BEGIN("task table init and free (default capacity)");

    ASSERT_EQ(ne_task_table_init(&tbl, NE_TASK_TABLE_CAP), NE_TASK_OK);
    ASSERT_NOT_NULL(tbl.tasks);
    ASSERT_EQ(tbl.capacity,    (uint16_t)NE_TASK_TABLE_CAP);
    ASSERT_EQ(tbl.count,       (uint16_t)0);
    ASSERT_EQ(tbl.next_handle, (uint16_t)1);

    ne_task_table_free(&tbl);
    ASSERT_NULL(tbl.tasks);
    ASSERT_EQ(tbl.capacity, (uint16_t)0);

    TEST_PASS();
}

static void test_task_table_init_null(void)
{
    TEST_BEGIN("task table init with NULL returns error");
    ASSERT_EQ(ne_task_table_init(NULL, 4), NE_TASK_ERR_NULL);
    TEST_PASS();
}

static void test_task_table_init_zero_cap(void)
{
    NETaskTable tbl;
    TEST_BEGIN("task table init with zero capacity returns error");
    ASSERT_EQ(ne_task_table_init(&tbl, 0), NE_TASK_ERR_NULL);
    TEST_PASS();
}

static void test_task_table_free_null(void)
{
    TEST_BEGIN("task table free on NULL is safe");
    ne_task_table_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * ne_task_create
 * ===================================================================== */

static void test_task_create_basic(void)
{
    NETaskTable      tbl;
    NETaskHandle     h = NE_TASK_HANDLE_INVALID;
    NETaskDescriptor *t;
    int              dummy;

    TEST_BEGIN("task create assigns valid handle and initialises state");

    ASSERT_EQ(ne_task_table_init(&tbl, 4), NE_TASK_OK);

    ASSERT_EQ(ne_task_create(&tbl, entry_set_flag, &dummy,
                              0 /* default stack */,
                              NE_TASK_PRIORITY_NORMAL, &h),
              NE_TASK_OK);
    ASSERT_NE((long long)h, (long long)NE_TASK_HANDLE_INVALID);
    ASSERT_EQ(tbl.count, (uint16_t)1);

    t = ne_task_get(&tbl, h);
    ASSERT_NOT_NULL(t);
    ASSERT_EQ(t->state,    (uint8_t)NE_TASK_STATE_READY);
    ASSERT_EQ(t->priority, (uint8_t)NE_TASK_PRIORITY_NORMAL);
    ASSERT_NOT_NULL(t->stack_base);

    ne_task_table_free(&tbl);
    TEST_PASS();
}

static void test_task_create_null_args(void)
{
    NETaskTable  tbl;
    NETaskHandle h;
    int          dummy;

    TEST_BEGIN("task create with NULL args returns error");

    ASSERT_EQ(ne_task_table_init(&tbl, 4), NE_TASK_OK);
    ASSERT_EQ(ne_task_create(NULL, entry_set_flag, &dummy,
                              0, NE_TASK_PRIORITY_NORMAL, &h),
              NE_TASK_ERR_NULL);
    ASSERT_EQ(ne_task_create(&tbl, NULL, &dummy,
                              0, NE_TASK_PRIORITY_NORMAL, &h),
              NE_TASK_ERR_NULL);
    ASSERT_EQ(ne_task_create(&tbl, entry_set_flag, &dummy,
                              0, NE_TASK_PRIORITY_NORMAL, NULL),
              NE_TASK_ERR_NULL);

    ne_task_table_free(&tbl);
    TEST_PASS();
}

static void test_task_create_table_full(void)
{
    NETaskTable  tbl;
    NETaskHandle h;
    int          dummy;

    TEST_BEGIN("task create into full table returns NE_TASK_ERR_FULL");

    ASSERT_EQ(ne_task_table_init(&tbl, 2), NE_TASK_OK);

    ASSERT_EQ(ne_task_create(&tbl, entry_set_flag, &dummy,
                              0, NE_TASK_PRIORITY_NORMAL, &h),
              NE_TASK_OK);
    ASSERT_EQ(ne_task_create(&tbl, entry_set_flag, &dummy,
                              0, NE_TASK_PRIORITY_NORMAL, &h),
              NE_TASK_OK);

    /* Table is full; third create must fail. */
    ASSERT_EQ(ne_task_create(&tbl, entry_set_flag, &dummy,
                              0, NE_TASK_PRIORITY_NORMAL, &h),
              NE_TASK_ERR_FULL);
    ASSERT_EQ((long long)h, (long long)NE_TASK_HANDLE_INVALID);

    ne_task_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_task_destroy
 * ===================================================================== */

static void test_task_destroy_basic(void)
{
    NETaskTable  tbl;
    NETaskHandle h;
    int          dummy;

    TEST_BEGIN("task destroy removes task and decrements count");

    ASSERT_EQ(ne_task_table_init(&tbl, 4), NE_TASK_OK);
    ASSERT_EQ(ne_task_create(&tbl, entry_set_flag, &dummy,
                              0, NE_TASK_PRIORITY_NORMAL, &h),
              NE_TASK_OK);
    ASSERT_EQ(tbl.count, (uint16_t)1);

    ASSERT_EQ(ne_task_destroy(&tbl, h), NE_TASK_OK);
    ASSERT_EQ(tbl.count, (uint16_t)0);
    ASSERT_NULL(ne_task_get(&tbl, h));

    ne_task_table_free(&tbl);
    TEST_PASS();
}

static void test_task_destroy_bad_handle(void)
{
    NETaskTable tbl;

    TEST_BEGIN("task destroy with bad/invalid handle returns error");

    ASSERT_EQ(ne_task_table_init(&tbl, 4), NE_TASK_OK);
    ASSERT_EQ(ne_task_destroy(&tbl, NE_TASK_HANDLE_INVALID),
              NE_TASK_ERR_BAD_HANDLE);
    ASSERT_EQ(ne_task_destroy(&tbl, (NETaskHandle)99),
              NE_TASK_ERR_NOT_FOUND);

    ne_task_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * Cooperative scheduling – run to completion
 * ===================================================================== */

static void test_task_run_to_completion(void)
{
    NETaskTable  tbl;
    NETaskHandle h;
    int          flag  = 0;
    int          ran;

    TEST_BEGIN("task runs to completion and transitions to TERMINATED");

    ASSERT_EQ(ne_task_table_init(&tbl, 4), NE_TASK_OK);
    ASSERT_EQ(ne_task_create(&tbl, entry_set_flag, &flag,
                              0, NE_TASK_PRIORITY_NORMAL, &h),
              NE_TASK_OK);

    ran = ne_task_table_run(&tbl);
    ASSERT_EQ(ran, 1);
    ASSERT_EQ(flag, 1);   /* entry function executed */

    {
        NETaskDescriptor *t = ne_task_get(&tbl, h);
        ASSERT_NOT_NULL(t);
        ASSERT_EQ(t->state, (uint8_t)NE_TASK_STATE_TERMINATED);
    }

    ne_task_table_free(&tbl);
    TEST_PASS();
}

static void test_task_run_multiple_to_completion(void)
{
    NETaskTable  tbl;
    NETaskHandle h1, h2;
    int          f1 = 0, f2 = 0;
    int          ran;

    TEST_BEGIN("two tasks both run to completion in one pass");

    ASSERT_EQ(ne_task_table_init(&tbl, 4), NE_TASK_OK);
    ASSERT_EQ(ne_task_create(&tbl, entry_set_flag, &f1,
                              0, NE_TASK_PRIORITY_NORMAL, &h1),
              NE_TASK_OK);
    ASSERT_EQ(ne_task_create(&tbl, entry_set_flag, &f2,
                              0, NE_TASK_PRIORITY_NORMAL, &h2),
              NE_TASK_OK);

    ran = ne_task_table_run(&tbl);
    ASSERT_EQ(ran, 2);
    ASSERT_EQ(f1,  1);
    ASSERT_EQ(f2,  1);

    ne_task_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * Cooperative scheduling – yield and resume
 * ===================================================================== */

static void test_task_yield_and_resume(void)
{
    NETaskTable  tbl;
    NETaskHandle h;
    int          ctr = 0;
    int         *args[2];

    TEST_BEGIN("task yield suspends; second run pass resumes and completes");

    ASSERT_EQ(ne_task_table_init(&tbl, 4), NE_TASK_OK);

    args[0] = (int *)(void *)&tbl;
    args[1] = &ctr;

    ASSERT_EQ(ne_task_create(&tbl, entry_yield_once, args,
                              0, NE_TASK_PRIORITY_NORMAL, &h),
              NE_TASK_OK);

    /* First pass: task runs up to the yield point. */
    ne_task_table_run(&tbl);
    ASSERT_EQ(ctr, 0); /* not yet incremented */
    {
        NETaskDescriptor *t = ne_task_get(&tbl, h);
        ASSERT_NOT_NULL(t);
        ASSERT_EQ(t->state, (uint8_t)NE_TASK_STATE_YIELDED);
    }

    /* Second pass: task resumes, increments ctr, then terminates. */
    ne_task_table_run(&tbl);
    ASSERT_EQ(ctr, 1);
    {
        NETaskDescriptor *t = ne_task_get(&tbl, h);
        ASSERT_NOT_NULL(t);
        ASSERT_EQ(t->state, (uint8_t)NE_TASK_STATE_TERMINATED);
    }

    ne_task_table_free(&tbl);
    TEST_PASS();
}

static void test_task_yield_twice(void)
{
    NETaskTable  tbl;
    NETaskHandle h;
    int          ctr = 0;
    int         *args[2];

    TEST_BEGIN("task that yields twice requires three run passes");

    ASSERT_EQ(ne_task_table_init(&tbl, 4), NE_TASK_OK);

    args[0] = (int *)(void *)&tbl;
    args[1] = &ctr;

    ASSERT_EQ(ne_task_create(&tbl, entry_yield_twice, args,
                              0, NE_TASK_PRIORITY_NORMAL, &h),
              NE_TASK_OK);

    ne_task_table_run(&tbl);
    ASSERT_EQ(ctr, 0);

    ne_task_table_run(&tbl);
    ASSERT_EQ(ctr, 1);

    ne_task_table_run(&tbl);
    ASSERT_EQ(ctr, 2);
    {
        NETaskDescriptor *t = ne_task_get(&tbl, h);
        ASSERT_NOT_NULL(t);
        ASSERT_EQ(t->state, (uint8_t)NE_TASK_STATE_TERMINATED);
    }

    ne_task_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * Task priority ordering
 * ===================================================================== */

static void test_task_priority_order(void)
{
    NETaskTable  tbl;
    NETaskHandle hL, hN, hH;
    int          log[3];
    int          idx = 0;
    PriorityArg  argL, argN, argH;

    TEST_BEGIN("HIGH priority task runs before NORMAL and LOW");

    ASSERT_EQ(ne_task_table_init(&tbl, 8), NE_TASK_OK);

    argL.tbl = &tbl; argL.log = log; argL.idx = &idx; argL.id = 0; /* LOW */
    argN.tbl = &tbl; argN.log = log; argN.idx = &idx; argN.id = 1; /* NORMAL */
    argH.tbl = &tbl; argH.log = log; argH.idx = &idx; argH.id = 2; /* HIGH */

    ASSERT_EQ(ne_task_create(&tbl, entry_priority_record, &argL,
                              0, NE_TASK_PRIORITY_LOW, &hL),
              NE_TASK_OK);
    ASSERT_EQ(ne_task_create(&tbl, entry_priority_record, &argN,
                              0, NE_TASK_PRIORITY_NORMAL, &hN),
              NE_TASK_OK);
    ASSERT_EQ(ne_task_create(&tbl, entry_priority_record, &argH,
                              0, NE_TASK_PRIORITY_HIGH, &hH),
              NE_TASK_OK);

    ne_task_table_run(&tbl);

    /* All three ran; HIGH (id=2) must be first, LOW (id=0) must be last. */
    ASSERT_EQ(idx,    3);
    ASSERT_EQ(log[0], 2); /* HIGH */
    ASSERT_EQ(log[2], 0); /* LOW */

    (void)hL; (void)hN; (void)hH;
    ne_task_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * Memory ownership tracking
 * ===================================================================== */

static void test_own_mem_basic(void)
{
    NETaskTable  tbl;
    NETaskHandle h;
    int          dummy;

    TEST_BEGIN("task_own_mem adds handle; task_disown_mem removes it");

    ASSERT_EQ(ne_task_table_init(&tbl, 4), NE_TASK_OK);
    ASSERT_EQ(ne_task_create(&tbl, entry_set_flag, &dummy,
                              0, NE_TASK_PRIORITY_NORMAL, &h),
              NE_TASK_OK);

    ASSERT_EQ(ne_task_own_mem(&tbl, h, (uint16_t)42), NE_TASK_OK);
    {
        NETaskDescriptor *t = ne_task_get(&tbl, h);
        ASSERT_NOT_NULL(t);
        ASSERT_EQ(t->owned_mem_count, (uint16_t)1);
        ASSERT_EQ(t->owned_mem[0],    (uint16_t)42);
    }

    ASSERT_EQ(ne_task_disown_mem(&tbl, h, (uint16_t)42), NE_TASK_OK);
    {
        NETaskDescriptor *t = ne_task_get(&tbl, h);
        ASSERT_NOT_NULL(t);
        ASSERT_EQ(t->owned_mem_count, (uint16_t)0);
    }

    ne_task_table_free(&tbl);
    TEST_PASS();
}

static void test_own_mem_duplicate_ignored(void)
{
    NETaskTable  tbl;
    NETaskHandle h;
    int          dummy;

    TEST_BEGIN("task_own_mem ignores duplicate handle entries");

    ASSERT_EQ(ne_task_table_init(&tbl, 4), NE_TASK_OK);
    ASSERT_EQ(ne_task_create(&tbl, entry_set_flag, &dummy,
                              0, NE_TASK_PRIORITY_NORMAL, &h),
              NE_TASK_OK);

    ASSERT_EQ(ne_task_own_mem(&tbl, h, (uint16_t)7), NE_TASK_OK);
    ASSERT_EQ(ne_task_own_mem(&tbl, h, (uint16_t)7), NE_TASK_OK);
    {
        NETaskDescriptor *t = ne_task_get(&tbl, h);
        ASSERT_NOT_NULL(t);
        ASSERT_EQ(t->owned_mem_count, (uint16_t)1);
    }

    ne_task_table_free(&tbl);
    TEST_PASS();
}

static void test_own_mem_errors(void)
{
    NETaskTable tbl;

    TEST_BEGIN("task_own_mem / disown_mem return errors for bad args");

    ASSERT_EQ(ne_task_table_init(&tbl, 4), NE_TASK_OK);
    ASSERT_EQ(ne_task_own_mem(NULL,  (NETaskHandle)1, 1), NE_TASK_ERR_NULL);
    ASSERT_EQ(ne_task_own_mem(&tbl, NE_TASK_HANDLE_INVALID, 1),
              NE_TASK_ERR_BAD_HANDLE);
    ASSERT_EQ(ne_task_own_mem(&tbl, (NETaskHandle)99, 1),
              NE_TASK_ERR_NOT_FOUND);
    ASSERT_EQ(ne_task_disown_mem(NULL, (NETaskHandle)1, 1), NE_TASK_ERR_NULL);

    ne_task_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * ne_task_strerror
 * ===================================================================== */

static void test_task_strerror(void)
{
    TEST_BEGIN("task strerror returns non-NULL for all known codes");
    ASSERT_NOT_NULL(ne_task_strerror(NE_TASK_OK));
    ASSERT_NOT_NULL(ne_task_strerror(NE_TASK_ERR_NULL));
    ASSERT_NOT_NULL(ne_task_strerror(NE_TASK_ERR_ALLOC));
    ASSERT_NOT_NULL(ne_task_strerror(NE_TASK_ERR_FULL));
    ASSERT_NOT_NULL(ne_task_strerror(NE_TASK_ERR_BAD_HANDLE));
    ASSERT_NOT_NULL(ne_task_strerror(NE_TASK_ERR_NOT_FOUND));
    ASSERT_NOT_NULL(ne_task_strerror(NE_TASK_ERR_STATE));
    ASSERT_NOT_NULL(ne_task_strerror(-999));
    TEST_PASS();
}

/* =========================================================================
 * GMEM table – init / free
 * ===================================================================== */

static void test_gmem_table_init_free(void)
{
    NEGMemTable tbl;

    TEST_BEGIN("gmem table init and free (default capacity)");

    ASSERT_EQ(ne_gmem_table_init(&tbl, NE_GMEM_TABLE_CAP), NE_MEM_OK);
    ASSERT_NOT_NULL(tbl.blocks);
    ASSERT_EQ(tbl.capacity,    (uint16_t)NE_GMEM_TABLE_CAP);
    ASSERT_EQ(tbl.count,       (uint16_t)0);
    ASSERT_EQ(tbl.next_handle, (uint16_t)1);

    ne_gmem_table_free(&tbl);
    ASSERT_NULL(tbl.blocks);
    ASSERT_EQ(tbl.capacity, (uint16_t)0);

    TEST_PASS();
}

static void test_gmem_table_init_null(void)
{
    TEST_BEGIN("gmem table init with NULL / zero capacity returns error");
    ASSERT_EQ(ne_gmem_table_init(NULL, 4), NE_MEM_ERR_NULL);
    {
        NEGMemTable t2;
        ASSERT_EQ(ne_gmem_table_init(&t2, 0), NE_MEM_ERR_NULL);
    }
    TEST_PASS();
}

static void test_gmem_table_free_null(void)
{
    TEST_BEGIN("gmem table free on NULL is safe");
    ne_gmem_table_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * GMEM alloc / free
 * ===================================================================== */

static void test_gmem_alloc_basic(void)
{
    NEGMemTable  tbl;
    NEGMemHandle h;

    TEST_BEGIN("gmem_alloc returns valid handle and sets block metadata");

    ASSERT_EQ(ne_gmem_table_init(&tbl, 8), NE_MEM_OK);

    h = ne_gmem_alloc(&tbl, NE_GMEM_FIXED, 256u, 0u);
    ASSERT_NE((long long)h, (long long)NE_GMEM_HANDLE_INVALID);
    ASSERT_EQ(tbl.count, (uint16_t)1);
    ASSERT_EQ(ne_gmem_size(&tbl, h), (uint32_t)256);

    ne_gmem_table_free(&tbl);
    TEST_PASS();
}

static void test_gmem_alloc_zeroinit(void)
{
    NEGMemTable  tbl;
    NEGMemHandle h;
    void        *p;
    uint8_t     *buf;
    int          all_zero;
    uint32_t     i;

    TEST_BEGIN("gmem_alloc with NE_GMEM_ZEROINIT zero-initialises buffer");

    ASSERT_EQ(ne_gmem_table_init(&tbl, 8), NE_MEM_OK);

    h = ne_gmem_alloc(&tbl, NE_GMEM_ZEROINIT, 64u, 0u);
    ASSERT_NE((long long)h, (long long)NE_GMEM_HANDLE_INVALID);

    p = ne_gmem_lock(&tbl, h);
    ASSERT_NOT_NULL(p);

    buf      = (uint8_t *)p;
    all_zero = 1;
    for (i = 0; i < 64u; i++) {
        if (buf[i] != 0) { all_zero = 0; break; }
    }
    ASSERT_EQ(all_zero, 1);

    ne_gmem_unlock(&tbl, h);
    ne_gmem_table_free(&tbl);
    TEST_PASS();
}

static void test_gmem_alloc_null_zero(void)
{
    NEGMemTable tbl;

    TEST_BEGIN("gmem_alloc returns invalid handle on NULL tbl or zero size");

    ASSERT_EQ(ne_gmem_table_init(&tbl, 8), NE_MEM_OK);

    ASSERT_EQ((long long)ne_gmem_alloc(NULL, NE_GMEM_FIXED, 1u, 0u),
              (long long)NE_GMEM_HANDLE_INVALID);
    ASSERT_EQ((long long)ne_gmem_alloc(&tbl, NE_GMEM_FIXED, 0u, 0u),
              (long long)NE_GMEM_HANDLE_INVALID);

    ne_gmem_table_free(&tbl);
    TEST_PASS();
}

static void test_gmem_free_basic(void)
{
    NEGMemTable  tbl;
    NEGMemHandle h;

    TEST_BEGIN("gmem_free releases block and decrements count");

    ASSERT_EQ(ne_gmem_table_init(&tbl, 8), NE_MEM_OK);
    h = ne_gmem_alloc(&tbl, NE_GMEM_FIXED, 64u, 0u);
    ASSERT_NE((long long)h, (long long)NE_GMEM_HANDLE_INVALID);

    ASSERT_EQ(ne_gmem_free(&tbl, h), NE_MEM_OK);
    ASSERT_EQ(tbl.count, (uint16_t)0);
    ASSERT_EQ(ne_gmem_size(&tbl, h), (uint32_t)0); /* no longer found */

    ne_gmem_table_free(&tbl);
    TEST_PASS();
}

static void test_gmem_free_bad_handle(void)
{
    NEGMemTable tbl;

    TEST_BEGIN("gmem_free with bad handle returns error");

    ASSERT_EQ(ne_gmem_table_init(&tbl, 4), NE_MEM_OK);
    ASSERT_EQ(ne_gmem_free(&tbl, NE_GMEM_HANDLE_INVALID),
              NE_MEM_ERR_BAD_HANDLE);
    ASSERT_EQ(ne_gmem_free(&tbl, (NEGMemHandle)99),
              NE_MEM_ERR_NOT_FOUND);

    ne_gmem_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * GMEM lock / unlock
 * ===================================================================== */

static void test_gmem_lock_unlock(void)
{
    NEGMemTable  tbl;
    NEGMemHandle h;
    void        *p1, *p2;
    NEGMemBlock *b;

    TEST_BEGIN("gmem lock increments lock count; unlock decrements it");

    ASSERT_EQ(ne_gmem_table_init(&tbl, 8), NE_MEM_OK);
    h = ne_gmem_alloc(&tbl, NE_GMEM_FIXED, 8u, 0u);
    ASSERT_NE((long long)h, (long long)NE_GMEM_HANDLE_INVALID);

    p1 = ne_gmem_lock(&tbl, h);
    ASSERT_NOT_NULL(p1);
    p2 = ne_gmem_lock(&tbl, h);
    ASSERT_NOT_NULL(p2);
    ASSERT_EQ((long long)p1, (long long)p2); /* same pointer for fixed block */

    b = ne_gmem_find_block(&tbl, h);
    ASSERT_NOT_NULL(b);
    ASSERT_EQ(b->lock_count, (uint16_t)2);

    ASSERT_EQ(ne_gmem_unlock(&tbl, h), NE_MEM_OK);
    ASSERT_EQ(b->lock_count, (uint16_t)1);
    ASSERT_EQ(ne_gmem_unlock(&tbl, h), NE_MEM_OK);
    ASSERT_EQ(b->lock_count, (uint16_t)0);

    ne_gmem_table_free(&tbl);
    TEST_PASS();
}

static void test_gmem_lock_null(void)
{
    NEGMemTable tbl;

    TEST_BEGIN("gmem lock/unlock with bad args return NULL/error");

    ASSERT_EQ(ne_gmem_table_init(&tbl, 4), NE_MEM_OK);
    ASSERT_NULL(ne_gmem_lock(NULL, (NEGMemHandle)1));
    ASSERT_NULL(ne_gmem_lock(&tbl, NE_GMEM_HANDLE_INVALID));
    ASSERT_EQ(ne_gmem_unlock(NULL,  (NEGMemHandle)1), NE_MEM_ERR_NULL);
    ASSERT_EQ(ne_gmem_unlock(&tbl, NE_GMEM_HANDLE_INVALID),
              NE_MEM_ERR_BAD_HANDLE);

    ne_gmem_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * GMEM ownership / task teardown
 * ===================================================================== */

static void test_gmem_free_by_owner(void)
{
    NEGMemTable  tbl;
    NEGMemHandle h1, h2, h3;
    uint16_t     freed;

    TEST_BEGIN("gmem_free_by_owner frees only blocks owned by given task");

    ASSERT_EQ(ne_gmem_table_init(&tbl, 8), NE_MEM_OK);

    h1 = ne_gmem_alloc(&tbl, NE_GMEM_FIXED, 32u, /* owner */ 1u);
    h2 = ne_gmem_alloc(&tbl, NE_GMEM_FIXED, 32u, /* owner */ 1u);
    h3 = ne_gmem_alloc(&tbl, NE_GMEM_FIXED, 32u, /* owner */ 2u);

    ASSERT_NE((long long)h1, (long long)NE_GMEM_HANDLE_INVALID);
    ASSERT_NE((long long)h2, (long long)NE_GMEM_HANDLE_INVALID);
    ASSERT_NE((long long)h3, (long long)NE_GMEM_HANDLE_INVALID);
    ASSERT_EQ(tbl.count, (uint16_t)3);

    freed = ne_gmem_free_by_owner(&tbl, 1u);
    ASSERT_EQ(freed, (uint16_t)2);
    ASSERT_EQ(tbl.count, (uint16_t)1);

    /* h3 (owned by task 2) must still be present. */
    ASSERT_NE((long long)ne_gmem_find_block(&tbl, h3),
              (long long)NULL);

    ne_gmem_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * LMEM heap – init / free
 * ===================================================================== */

static void test_lmem_heap_init_free(void)
{
    NELMemHeap heap;

    TEST_BEGIN("lmem heap init and free");

    ASSERT_EQ(ne_lmem_heap_init(&heap), NE_MEM_OK);
    ASSERT_EQ(heap.count,        (uint16_t)0);
    ASSERT_EQ(heap.next_handle,  (uint16_t)1);

    ne_lmem_heap_free(&heap);
    ASSERT_EQ(heap.count,       (uint16_t)0);
    ASSERT_EQ(heap.next_handle, (uint16_t)0);

    TEST_PASS();
}

static void test_lmem_heap_init_null(void)
{
    TEST_BEGIN("lmem heap init with NULL returns error");
    ASSERT_EQ(ne_lmem_heap_init(NULL), NE_MEM_ERR_NULL);
    TEST_PASS();
}

static void test_lmem_heap_free_null(void)
{
    TEST_BEGIN("lmem heap free on NULL is safe");
    ne_lmem_heap_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * LMEM alloc / free
 * ===================================================================== */

static void test_lmem_alloc_basic(void)
{
    NELMemHeap   heap;
    NELMemHandle h;

    TEST_BEGIN("lmem_alloc returns valid handle");

    ASSERT_EQ(ne_lmem_heap_init(&heap), NE_MEM_OK);

    h = ne_lmem_alloc(&heap, NE_LMEM_FIXED, 128u);
    ASSERT_NE((long long)h, (long long)NE_LMEM_HANDLE_INVALID);
    ASSERT_EQ(heap.count, (uint16_t)1);
    ASSERT_EQ(ne_lmem_size(&heap, h), (uint16_t)128);

    ne_lmem_heap_free(&heap);
    TEST_PASS();
}

static void test_lmem_alloc_zeroinit(void)
{
    NELMemHeap   heap;
    NELMemHandle h;
    void        *p;
    uint8_t     *buf;
    int          all_zero;
    uint16_t     i;

    TEST_BEGIN("lmem_alloc with NE_LMEM_ZEROINIT zero-initialises buffer");

    ASSERT_EQ(ne_lmem_heap_init(&heap), NE_MEM_OK);

    h = ne_lmem_alloc(&heap, NE_LMEM_ZEROINIT, 32u);
    ASSERT_NE((long long)h, (long long)NE_LMEM_HANDLE_INVALID);

    p = ne_lmem_lock(&heap, h);
    ASSERT_NOT_NULL(p);

    buf      = (uint8_t *)p;
    all_zero = 1;
    for (i = 0; i < 32u; i++) {
        if (buf[i] != 0) { all_zero = 0; break; }
    }
    ASSERT_EQ(all_zero, 1);

    ne_lmem_unlock(&heap, h);
    ne_lmem_heap_free(&heap);
    TEST_PASS();
}

static void test_lmem_alloc_null_zero(void)
{
    NELMemHeap heap;

    TEST_BEGIN("lmem_alloc returns invalid handle on NULL heap or zero size");

    ASSERT_EQ(ne_lmem_heap_init(&heap), NE_MEM_OK);
    ASSERT_EQ((long long)ne_lmem_alloc(NULL, NE_LMEM_FIXED, 1u),
              (long long)NE_LMEM_HANDLE_INVALID);
    ASSERT_EQ((long long)ne_lmem_alloc(&heap, NE_LMEM_FIXED, 0u),
              (long long)NE_LMEM_HANDLE_INVALID);

    ne_lmem_heap_free(&heap);
    TEST_PASS();
}

static void test_lmem_free_basic(void)
{
    NELMemHeap   heap;
    NELMemHandle h;

    TEST_BEGIN("lmem_free releases block and decrements count");

    ASSERT_EQ(ne_lmem_heap_init(&heap), NE_MEM_OK);
    h = ne_lmem_alloc(&heap, NE_LMEM_FIXED, 64u);
    ASSERT_NE((long long)h, (long long)NE_LMEM_HANDLE_INVALID);

    ASSERT_EQ(ne_lmem_free(&heap, h), NE_MEM_OK);
    ASSERT_EQ(heap.count, (uint16_t)0);
    ASSERT_EQ(ne_lmem_size(&heap, h), (uint16_t)0);

    ne_lmem_heap_free(&heap);
    TEST_PASS();
}

static void test_lmem_free_bad_handle(void)
{
    NELMemHeap heap;

    TEST_BEGIN("lmem_free with bad handle returns error");

    ASSERT_EQ(ne_lmem_heap_init(&heap), NE_MEM_OK);
    ASSERT_EQ(ne_lmem_free(&heap, NE_LMEM_HANDLE_INVALID),
              NE_MEM_ERR_BAD_HANDLE);
    ASSERT_EQ(ne_lmem_free(&heap, (NELMemHandle)99),
              NE_MEM_ERR_NOT_FOUND);

    ne_lmem_heap_free(&heap);
    TEST_PASS();
}

/* =========================================================================
 * LMEM lock / unlock
 * ===================================================================== */

static void test_lmem_lock_unlock(void)
{
    NELMemHeap   heap;
    NELMemHandle h;
    void        *p;

    TEST_BEGIN("lmem lock returns pointer; unlock decrements lock count");

    ASSERT_EQ(ne_lmem_heap_init(&heap), NE_MEM_OK);
    h = ne_lmem_alloc(&heap, NE_LMEM_FIXED, 16u);
    ASSERT_NE((long long)h, (long long)NE_LMEM_HANDLE_INVALID);

    p = ne_lmem_lock(&heap, h);
    ASSERT_NOT_NULL(p);

    ASSERT_EQ(ne_lmem_unlock(&heap, h), NE_MEM_OK);

    ne_lmem_heap_free(&heap);
    TEST_PASS();
}

/* =========================================================================
 * ne_mem_strerror
 * ===================================================================== */

static void test_mem_strerror(void)
{
    TEST_BEGIN("mem strerror returns non-NULL for all known codes");
    ASSERT_NOT_NULL(ne_mem_strerror(NE_MEM_OK));
    ASSERT_NOT_NULL(ne_mem_strerror(NE_MEM_ERR_NULL));
    ASSERT_NOT_NULL(ne_mem_strerror(NE_MEM_ERR_ALLOC));
    ASSERT_NOT_NULL(ne_mem_strerror(NE_MEM_ERR_FULL));
    ASSERT_NOT_NULL(ne_mem_strerror(NE_MEM_ERR_BAD_HANDLE));
    ASSERT_NOT_NULL(ne_mem_strerror(NE_MEM_ERR_NOT_FOUND));
    ASSERT_NOT_NULL(ne_mem_strerror(NE_MEM_ERR_LOCKED));
    ASSERT_NOT_NULL(ne_mem_strerror(NE_MEM_ERR_ZERO_SIZE));
    ASSERT_NOT_NULL(ne_mem_strerror(NE_MEM_ERR_LMEM_FULL));
    ASSERT_NOT_NULL(ne_mem_strerror(-999));
    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== NE Task and Memory Management Tests (Step 6) ===\n\n");

    printf("--- Task table ---\n");
    test_task_table_init_free();
    test_task_table_init_null();
    test_task_table_init_zero_cap();
    test_task_table_free_null();

    printf("\n--- Task create / destroy ---\n");
    test_task_create_basic();
    test_task_create_null_args();
    test_task_create_table_full();
    test_task_destroy_basic();
    test_task_destroy_bad_handle();

    printf("\n--- Cooperative scheduling ---\n");
    test_task_run_to_completion();
    test_task_run_multiple_to_completion();
    test_task_yield_and_resume();
    test_task_yield_twice();
    test_task_priority_order();

    printf("\n--- Memory ownership tracking ---\n");
    test_own_mem_basic();
    test_own_mem_duplicate_ignored();
    test_own_mem_errors();
    test_task_strerror();

    printf("\n--- GMEM table ---\n");
    test_gmem_table_init_free();
    test_gmem_table_init_null();
    test_gmem_table_free_null();

    printf("\n--- GMEM alloc / free ---\n");
    test_gmem_alloc_basic();
    test_gmem_alloc_zeroinit();
    test_gmem_alloc_null_zero();
    test_gmem_free_basic();
    test_gmem_free_bad_handle();

    printf("\n--- GMEM lock / unlock ---\n");
    test_gmem_lock_unlock();
    test_gmem_lock_null();

    printf("\n--- GMEM ownership / teardown ---\n");
    test_gmem_free_by_owner();

    printf("\n--- LMEM heap ---\n");
    test_lmem_heap_init_free();
    test_lmem_heap_init_null();
    test_lmem_heap_free_null();

    printf("\n--- LMEM alloc / free ---\n");
    test_lmem_alloc_basic();
    test_lmem_alloc_zeroinit();
    test_lmem_alloc_null_zero();
    test_lmem_free_basic();
    test_lmem_free_bad_handle();

    printf("\n--- LMEM lock / unlock ---\n");
    test_lmem_lock_unlock();
    test_mem_strerror();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

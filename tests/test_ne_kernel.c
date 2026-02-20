/*
 * test_ne_kernel.c - Tests for Phase 2: KERNEL.EXE API stubs
 *
 * Verifies:
 *   - Kernel context initialisation and teardown
 *   - Export catalog enumeration and classification
 *   - Export registration into the import/export resolution table
 *   - File I/O: _lopen, _lclose, _lread, _lwrite, _llseek
 *   - Module APIs: GetModuleHandle, GetModuleFileName, GetProcAddress,
 *                  LoadLibrary, FreeLibrary
 *   - Memory APIs: GlobalAlloc/Free/Lock/Unlock/ReAlloc,
 *                  LocalAlloc/Free/Lock/Unlock
 *   - Task/process APIs: GetCurrentTask, Yield, InitTask, WaitEvent,
 *                        PostEvent
 *   - String/resource stubs: LoadString, FindResource, LoadResource,
 *                            LockResource
 *   - Atom APIs: GlobalAddAtom, GlobalFindAtom, GlobalGetAtomName,
 *                GlobalDeleteAtom
 */

#include "../src/ne_kernel.h"
#include "../src/ne_driver.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

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

/* -------------------------------------------------------------------------
 * Helper: initialise all subsystem tables and kernel context for a test
 * ---------------------------------------------------------------------- */

static void setup_kernel(NEGMemTable     *gmem,
                         NELMemHeap      *lmem,
                         NETaskTable     *tasks,
                         NEModuleTable   *modules,
                         NEKernelContext *ctx)
{
    ne_gmem_table_init(gmem,   NE_GMEM_TABLE_CAP);
    ne_lmem_heap_init(lmem);
    ne_task_table_init(tasks,  NE_TASK_TABLE_CAP);
    ne_mod_table_init(modules, NE_MOD_TABLE_CAP);
    ne_kernel_init(ctx, gmem, lmem, tasks, modules);
}

static void teardown_kernel(NEGMemTable     *gmem,
                            NELMemHeap      *lmem,
                            NETaskTable     *tasks,
                            NEModuleTable   *modules,
                            NEKernelContext *ctx)
{
    ne_kernel_free(ctx);
    ne_mod_table_free(modules);
    ne_task_table_free(tasks);
    ne_lmem_heap_free(lmem);
    ne_gmem_table_free(gmem);
}

/* =========================================================================
 * Kernel context tests
 * ===================================================================== */

static void test_kernel_init_free(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("kernel init and free");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_NE(ctx.initialized, 0);
    ASSERT_NOT_NULL(ctx.gmem);
    ASSERT_NOT_NULL(ctx.lmem);
    ASSERT_NOT_NULL(ctx.tasks);
    ASSERT_NOT_NULL(ctx.modules);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ctx.initialized, 0);
    TEST_PASS();
}

static void test_kernel_init_null_args(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("kernel init with NULL args returns error");

    ne_gmem_table_init(&gmem, NE_GMEM_TABLE_CAP);
    ne_lmem_heap_init(&lmem);
    ne_task_table_init(&tasks, NE_TASK_TABLE_CAP);
    ne_mod_table_init(&modules, NE_MOD_TABLE_CAP);

    ASSERT_EQ(ne_kernel_init(NULL, &gmem, &lmem, &tasks, &modules),
              NE_KERNEL_ERR_NULL);
    ASSERT_EQ(ne_kernel_init(&ctx, NULL, &lmem, &tasks, &modules),
              NE_KERNEL_ERR_NULL);
    ASSERT_EQ(ne_kernel_init(&ctx, &gmem, NULL, &tasks, &modules),
              NE_KERNEL_ERR_NULL);
    ASSERT_EQ(ne_kernel_init(&ctx, &gmem, &lmem, NULL, &modules),
              NE_KERNEL_ERR_NULL);
    ASSERT_EQ(ne_kernel_init(&ctx, &gmem, &lmem, &tasks, NULL),
              NE_KERNEL_ERR_NULL);

    ne_mod_table_free(&modules);
    ne_task_table_free(&tasks);
    ne_lmem_heap_free(&lmem);
    ne_gmem_table_free(&gmem);
    TEST_PASS();
}

static void test_kernel_free_null_safe(void)
{
    TEST_BEGIN("kernel free on NULL is safe");
    ne_kernel_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * Export catalog tests
 * ===================================================================== */

static void test_catalog_returns_entries(void)
{
    const NEKernelExportInfo *cat;
    uint16_t count;

    TEST_BEGIN("export catalog returns non-empty list");

    ASSERT_EQ(ne_kernel_get_export_catalog(&cat, &count), NE_KERNEL_OK);
    ASSERT_NOT_NULL(cat);
    ASSERT_NE(count, (uint16_t)0u);
    TEST_PASS();
}

static void test_catalog_null_args(void)
{
    const NEKernelExportInfo *cat;
    uint16_t count;

    TEST_BEGIN("export catalog NULL args return error");

    ASSERT_EQ(ne_kernel_get_export_catalog(NULL, &count),
              NE_KERNEL_ERR_NULL);
    ASSERT_EQ(ne_kernel_get_export_catalog(&cat, NULL),
              NE_KERNEL_ERR_NULL);
    TEST_PASS();
}

static void test_catalog_classification(void)
{
    const NEKernelExportInfo *cat;
    uint16_t count;
    uint16_t i;
    int has_critical  = 0;
    int has_secondary = 0;

    TEST_BEGIN("export catalog has critical and secondary entries");

    ne_kernel_get_export_catalog(&cat, &count);

    for (i = 0; i < count; i++) {
        if (cat[i].classification == NE_KERNEL_CLASS_CRITICAL)
            has_critical = 1;
        if (cat[i].classification == NE_KERNEL_CLASS_SECONDARY)
            has_secondary = 1;
    }

    ASSERT_NE(has_critical, 0);
    ASSERT_NE(has_secondary, 0);
    TEST_PASS();
}

/* =========================================================================
 * Export registration tests
 * ===================================================================== */

static void test_register_exports(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("register exports populates export table");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_register_exports(&ctx), NE_KERNEL_OK);
    ASSERT_NE(ctx.exports.count, (uint16_t)0u);
    ASSERT_NOT_NULL(ctx.exports.entries);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_register_exports_resolvable(void)
{
    NEGMemTable          gmem;
    NELMemHeap           lmem;
    NETaskTable          tasks;
    NEModuleTable        modules;
    NEKernelContext      ctx;
    const NEExportEntry *entry;

    TEST_BEGIN("registered exports resolvable by ordinal and name");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    ne_kernel_register_exports(&ctx);

    /* Look up by ordinal */
    entry = ne_export_find_by_ordinal(&ctx.exports, NE_KERNEL_ORD_GLOBAL_ALLOC);
    ASSERT_NOT_NULL(entry);
    ASSERT_STR_EQ(entry->name, "GlobalAlloc");

    /* Look up by name */
    entry = ne_export_find_by_name(&ctx.exports, "_lopen");
    ASSERT_NOT_NULL(entry);
    ASSERT_EQ(entry->ordinal, (uint16_t)NE_KERNEL_ORD_LOPEN);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

/* =========================================================================
 * File I/O tests
 * ===================================================================== */

#define TEST_FILE_NAME "KRNLTEST.TMP"

static void test_file_io_write_read(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    int             hFile;
    int             n;
    char            buf[64];
    FILE           *fp;
    const char     *data = "Hello, WinDOS!";

    TEST_BEGIN("file I/O: write, seek, read round-trip");

    /* Create the file with fopen so _lopen can open it */
    fp = fopen(TEST_FILE_NAME, "wb");
    if (!fp) TEST_FAIL("cannot create temp file");
    fclose(fp);

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    /* Open for read/write */
    hFile = ne_kernel_lopen(&ctx, TEST_FILE_NAME, NE_KERNEL_OF_READWRITE);
    ASSERT_NE(hFile, NE_KERNEL_HFILE_ERROR);

    /* Write */
    n = ne_kernel_lwrite(&ctx, hFile, data, (uint16_t)strlen(data));
    ASSERT_EQ(n, (int)strlen(data));

    /* Seek back to beginning */
    ASSERT_EQ(ne_kernel_llseek(&ctx, hFile, 0L, NE_KERNEL_FILE_BEGIN), 0L);

    /* Read back */
    memset(buf, 0, sizeof(buf));
    n = ne_kernel_lread(&ctx, hFile, buf, (uint16_t)strlen(data));
    ASSERT_EQ(n, (int)strlen(data));
    ASSERT_STR_EQ(buf, data);

    /* Close */
    ASSERT_EQ(ne_kernel_lclose(&ctx, hFile), NE_KERNEL_OK);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    remove(TEST_FILE_NAME);
    TEST_PASS();
}

static void test_file_io_open_nonexistent(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("file I/O: open non-existent file returns error");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_lopen(&ctx, "NOSUCHFILE.XXX", NE_KERNEL_OF_READ),
              NE_KERNEL_HFILE_ERROR);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_file_io_null_args(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("file I/O: NULL args return errors");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_lopen(NULL, "x", 0), NE_KERNEL_HFILE_ERROR);
    ASSERT_EQ(ne_kernel_lopen(&ctx, NULL, 0), NE_KERNEL_HFILE_ERROR);
    ASSERT_EQ(ne_kernel_lclose(NULL, 0), NE_KERNEL_ERR_IO);
    ASSERT_EQ(ne_kernel_lread(NULL, 0, NULL, 0), NE_KERNEL_HFILE_ERROR);
    ASSERT_EQ(ne_kernel_lwrite(NULL, 0, NULL, 0), NE_KERNEL_HFILE_ERROR);
    ASSERT_EQ(ne_kernel_llseek(NULL, 0, 0, 0), (long)NE_KERNEL_HFILE_ERROR);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_file_io_llseek(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    int             hFile;
    long            pos;
    FILE           *fp;

    TEST_BEGIN("file I/O: llseek FILE_BEGIN/CURRENT/END");

    /* Create a 10-byte file */
    fp = fopen(TEST_FILE_NAME, "wb");
    if (!fp) TEST_FAIL("cannot create temp file");
    fwrite("0123456789", 1, 10, fp);
    fclose(fp);

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    hFile = ne_kernel_lopen(&ctx, TEST_FILE_NAME, NE_KERNEL_OF_READ);
    ASSERT_NE(hFile, NE_KERNEL_HFILE_ERROR);

    /* Seek to position 5 from beginning */
    pos = ne_kernel_llseek(&ctx, hFile, 5L, NE_KERNEL_FILE_BEGIN);
    ASSERT_EQ(pos, 5L);

    /* Seek forward 3 from current */
    pos = ne_kernel_llseek(&ctx, hFile, 3L, NE_KERNEL_FILE_CURRENT);
    ASSERT_EQ(pos, 8L);

    /* Seek to 2 bytes before end */
    pos = ne_kernel_llseek(&ctx, hFile, -2L, NE_KERNEL_FILE_END);
    ASSERT_EQ(pos, 8L);

    ne_kernel_lclose(&ctx, hFile);
    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    remove(TEST_FILE_NAME);
    TEST_PASS();
}

/* =========================================================================
 * Module API tests
 * ===================================================================== */

static void test_module_handle_not_found(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("GetModuleHandle: unknown name returns 0");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_get_module_handle(&ctx, "NOTLOADED"), (uint16_t)0u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_module_filename_not_found(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    char            buf[32];

    TEST_BEGIN("GetModuleFileName: invalid handle returns NOT_FOUND");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_get_module_filename(&ctx, 999u, buf, sizeof(buf)),
              NE_KERNEL_ERR_NOT_FOUND);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_get_proc_address(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    uint32_t        addr;

    TEST_BEGIN("GetProcAddress: finds KERNEL export by name");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    ne_kernel_register_exports(&ctx);

    addr = ne_kernel_get_proc_address(&ctx, 0, "GlobalAlloc");
    ASSERT_NE(addr, (uint32_t)0u);

    addr = ne_kernel_get_proc_address(&ctx, 0, "NoSuchFunc");
    ASSERT_EQ(addr, (uint32_t)0u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_load_library_unknown(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("LoadLibrary: unknown module returns 0");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_load_library(&ctx, "NOTLOADED"), (uint16_t)0u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_free_library_invalid(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("FreeLibrary: invalid handle does not crash");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ne_kernel_free_library(&ctx, 0);
    ne_kernel_free_library(&ctx, 999u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

/* =========================================================================
 * Global memory API tests
 * ===================================================================== */

static void test_global_alloc_free(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    NEGMemHandle    h;

    TEST_BEGIN("GlobalAlloc/GlobalFree round-trip");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    h = ne_kernel_global_alloc(&ctx, NE_GMEM_FIXED, 128);
    ASSERT_NE(h, NE_GMEM_HANDLE_INVALID);

    ASSERT_EQ(ne_kernel_global_free(&ctx, h), NE_KERNEL_OK);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_global_lock_unlock(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    NEGMemHandle    h;
    void           *ptr;

    TEST_BEGIN("GlobalLock/GlobalUnlock round-trip");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    h = ne_kernel_global_alloc(&ctx, NE_GMEM_ZEROINIT, 64);
    ASSERT_NE(h, NE_GMEM_HANDLE_INVALID);

    ptr = ne_kernel_global_lock(&ctx, h);
    ASSERT_NOT_NULL(ptr);

    ASSERT_EQ(ne_kernel_global_unlock(&ctx, h), NE_MEM_OK);

    ne_kernel_global_free(&ctx, h);
    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_global_realloc(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    NEGMemHandle    h1, h2;
    uint8_t        *ptr;

    TEST_BEGIN("GlobalReAlloc preserves data and changes size");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    h1 = ne_kernel_global_alloc(&ctx, NE_GMEM_FIXED, 32);
    ASSERT_NE(h1, NE_GMEM_HANDLE_INVALID);

    /* Write a known pattern */
    ptr = (uint8_t *)ne_kernel_global_lock(&ctx, h1);
    ASSERT_NOT_NULL(ptr);
    memset(ptr, 0xAB, 32);
    ne_kernel_global_unlock(&ctx, h1);

    /* Realloc to larger size */
    h2 = ne_kernel_global_realloc(&ctx, h1, 64, NE_GMEM_FIXED);
    ASSERT_NE(h2, NE_GMEM_HANDLE_INVALID);

    /* Verify data preserved */
    ptr = (uint8_t *)ne_kernel_global_lock(&ctx, h2);
    ASSERT_NOT_NULL(ptr);
    ASSERT_EQ(ptr[0], (uint8_t)0xAB);
    ASSERT_EQ(ptr[31], (uint8_t)0xAB);
    ne_kernel_global_unlock(&ctx, h2);

    ne_kernel_global_free(&ctx, h2);
    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_global_null_ctx(void)
{
    TEST_BEGIN("global memory APIs: NULL ctx return errors");

    ASSERT_EQ(ne_kernel_global_alloc(NULL, 0, 32), NE_GMEM_HANDLE_INVALID);
    ASSERT_EQ(ne_kernel_global_free(NULL, 1), NE_KERNEL_ERR_NULL);
    ASSERT_NULL(ne_kernel_global_lock(NULL, 1));
    ASSERT_EQ(ne_kernel_global_unlock(NULL, 1), NE_KERNEL_ERR_NULL);
    ASSERT_EQ(ne_kernel_global_realloc(NULL, 1, 64, 0), NE_GMEM_HANDLE_INVALID);

    TEST_PASS();
}

/* =========================================================================
 * Local memory API tests
 * ===================================================================== */

static void test_local_alloc_free(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    NELMemHandle    h;

    TEST_BEGIN("LocalAlloc/LocalFree round-trip");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    h = ne_kernel_local_alloc(&ctx, NE_LMEM_FIXED, 64);
    ASSERT_NE(h, NE_LMEM_HANDLE_INVALID);

    ASSERT_EQ(ne_kernel_local_free(&ctx, h), NE_MEM_OK);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_local_lock_unlock(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    NELMemHandle    h;
    void           *ptr;

    TEST_BEGIN("LocalLock/LocalUnlock round-trip");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    h = ne_kernel_local_alloc(&ctx, NE_LMEM_ZEROINIT, 32);
    ASSERT_NE(h, NE_LMEM_HANDLE_INVALID);

    ptr = ne_kernel_local_lock(&ctx, h);
    ASSERT_NOT_NULL(ptr);

    ASSERT_EQ(ne_kernel_local_unlock(&ctx, h), NE_MEM_OK);

    ne_kernel_local_free(&ctx, h);
    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_local_null_ctx(void)
{
    TEST_BEGIN("local memory APIs: NULL ctx return errors");

    ASSERT_EQ(ne_kernel_local_alloc(NULL, 0, 32), NE_LMEM_HANDLE_INVALID);
    ASSERT_EQ(ne_kernel_local_free(NULL, 1), NE_KERNEL_ERR_NULL);
    ASSERT_NULL(ne_kernel_local_lock(NULL, 1));
    ASSERT_EQ(ne_kernel_local_unlock(NULL, 1), NE_KERNEL_ERR_NULL);

    TEST_PASS();
}

/* =========================================================================
 * Task / process API tests
 * ===================================================================== */

static void test_get_current_task_no_active(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("GetCurrentTask: no active task returns 0");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_get_current_task(&ctx), (uint16_t)0u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_init_task(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("InitTask: returns OK");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_init_task(&ctx), NE_KERNEL_OK);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_wait_event_post_event(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("WaitEvent/PostEvent: stubs return OK");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_wait_event(&ctx, 1u), NE_KERNEL_OK);
    ASSERT_EQ(ne_kernel_post_event(&ctx, 1u), NE_KERNEL_OK);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_task_null_ctx(void)
{
    TEST_BEGIN("task APIs: NULL ctx return errors/zeros");

    ASSERT_EQ(ne_kernel_get_current_task(NULL), (uint16_t)0u);
    ASSERT_EQ(ne_kernel_init_task(NULL), NE_KERNEL_ERR_INIT);
    ASSERT_EQ(ne_kernel_wait_event(NULL, 1u), NE_KERNEL_ERR_NULL);
    ASSERT_EQ(ne_kernel_post_event(NULL, 1u), NE_KERNEL_ERR_NULL);

    TEST_PASS();
}

/* =========================================================================
 * String / resource stub tests
 * ===================================================================== */

static void test_load_string_stub(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    char            buf[64];

    TEST_BEGIN("LoadString stub returns 0");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_load_string(&ctx, 1u, 100u, buf, sizeof(buf)), 0);
    ASSERT_STR_EQ(buf, "");

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_find_resource_stub(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("FindResource stub returns 0");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_find_resource(&ctx, 1u, "TEST", "RT_DIALOG"),
              (uint32_t)0u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_load_resource_stub(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("LoadResource stub returns 0");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_load_resource(&ctx, 1u, 1u), (uint16_t)0u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_lock_resource_stub(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("LockResource stub returns NULL");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_NULL(ne_kernel_lock_resource(&ctx, 1u));

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

/* =========================================================================
 * Atom API tests
 * ===================================================================== */

static void test_atom_add_and_find(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    uint16_t        a1, a2, found;

    TEST_BEGIN("GlobalAddAtom and GlobalFindAtom");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    a1 = ne_kernel_global_add_atom(&ctx, "TestAtom1");
    ASSERT_NE(a1, NE_KERNEL_ATOM_INVALID);

    a2 = ne_kernel_global_add_atom(&ctx, "TestAtom2");
    ASSERT_NE(a2, NE_KERNEL_ATOM_INVALID);
    ASSERT_NE(a1, a2);

    found = ne_kernel_global_find_atom(&ctx, "TestAtom1");
    ASSERT_EQ(found, a1);

    found = ne_kernel_global_find_atom(&ctx, "TestAtom2");
    ASSERT_EQ(found, a2);

    /* Not found */
    found = ne_kernel_global_find_atom(&ctx, "NoSuch");
    ASSERT_EQ(found, NE_KERNEL_ATOM_INVALID);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_atom_add_duplicate(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    uint16_t        a1, a2;

    TEST_BEGIN("GlobalAddAtom: duplicate returns same atom");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    a1 = ne_kernel_global_add_atom(&ctx, "Dup");
    a2 = ne_kernel_global_add_atom(&ctx, "Dup");
    ASSERT_EQ(a1, a2);
    ASSERT_EQ(ctx.atom_count, (uint16_t)1u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_atom_get_name(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    uint16_t        a;
    char            buf[64];

    TEST_BEGIN("GlobalGetAtomName: retrieves correct name");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    a = ne_kernel_global_add_atom(&ctx, "MyAtom");
    ASSERT_NE(a, NE_KERNEL_ATOM_INVALID);

    ASSERT_EQ(ne_kernel_global_get_atom_name(&ctx, a, buf, sizeof(buf)),
              NE_KERNEL_OK);
    ASSERT_STR_EQ(buf, "MyAtom");

    /* Unknown atom */
    ASSERT_EQ(ne_kernel_global_get_atom_name(&ctx, 0xFFFFu, buf, sizeof(buf)),
              NE_KERNEL_ERR_NOT_FOUND);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_atom_delete(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    uint16_t        a;

    TEST_BEGIN("GlobalDeleteAtom: removes atom from table");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    a = ne_kernel_global_add_atom(&ctx, "Deleted");
    ASSERT_NE(a, NE_KERNEL_ATOM_INVALID);
    ASSERT_EQ(ctx.atom_count, (uint16_t)1u);

    ASSERT_EQ(ne_kernel_global_delete_atom(&ctx, a), NE_KERNEL_OK);
    ASSERT_EQ(ctx.atom_count, (uint16_t)0u);

    /* Find should now fail */
    ASSERT_EQ(ne_kernel_global_find_atom(&ctx, "Deleted"),
              NE_KERNEL_ATOM_INVALID);

    /* Delete non-existent atom */
    ASSERT_EQ(ne_kernel_global_delete_atom(&ctx, a), NE_KERNEL_ERR_NOT_FOUND);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_atom_null_args(void)
{
    TEST_BEGIN("atom APIs: NULL args return errors/invalid");

    ASSERT_EQ(ne_kernel_global_add_atom(NULL, "x"), NE_KERNEL_ATOM_INVALID);
    ASSERT_EQ(ne_kernel_global_find_atom(NULL, "x"), NE_KERNEL_ATOM_INVALID);
    ASSERT_EQ(ne_kernel_global_get_atom_name(NULL, 1, NULL, 0),
              NE_KERNEL_ERR_NULL);
    ASSERT_EQ(ne_kernel_global_delete_atom(NULL, 1), NE_KERNEL_ERR_NULL);

    TEST_PASS();
}

/* =========================================================================
 * Error string test
 * ===================================================================== */

static void test_strerror(void)
{
    TEST_BEGIN("strerror returns non-NULL for all known codes");

    ASSERT_NOT_NULL(ne_kernel_strerror(NE_KERNEL_OK));
    ASSERT_NOT_NULL(ne_kernel_strerror(NE_KERNEL_ERR_NULL));
    ASSERT_NOT_NULL(ne_kernel_strerror(NE_KERNEL_ERR_INIT));
    ASSERT_NOT_NULL(ne_kernel_strerror(NE_KERNEL_ERR_IO));
    ASSERT_NOT_NULL(ne_kernel_strerror(NE_KERNEL_ERR_FULL));
    ASSERT_NOT_NULL(ne_kernel_strerror(NE_KERNEL_ERR_NOT_FOUND));
    ASSERT_NOT_NULL(ne_kernel_strerror(-999));

    TEST_PASS();
}

/* =========================================================================
 * Phase A: Critical KERNEL.EXE API tests
 * ===================================================================== */

static void test_get_version(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("GetVersion returns 0x0A03 (Windows 3.10)");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_get_version(&ctx), (uint16_t)0x0A03u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_get_version_null_ctx(void)
{
    TEST_BEGIN("GetVersion: NULL ctx returns 0");
    ASSERT_EQ(ne_kernel_get_version(NULL), (uint16_t)0u);
    TEST_PASS();
}

static void test_get_win_flags(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("GetWinFlags returns 0 (real mode 8086)");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_get_win_flags(&ctx), (uint32_t)0u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_get_windows_directory(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    char            buf[64];
    int             len;

    TEST_BEGIN("GetWindowsDirectory returns C:\\WINDOWS");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    len = ne_kernel_get_windows_directory(&ctx, buf, sizeof(buf));
    ASSERT_NE(len, 0);
    ASSERT_STR_EQ(buf, "C:\\WINDOWS");

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_get_system_directory(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    char            buf[64];
    int             len;

    TEST_BEGIN("GetSystemDirectory returns C:\\WINDOWS\\SYSTEM");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    len = ne_kernel_get_system_directory(&ctx, buf, sizeof(buf));
    ASSERT_NE(len, 0);
    ASSERT_STR_EQ(buf, "C:\\WINDOWS\\SYSTEM");

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_get_dos_environment(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("GetDOSEnvironment returns NULL (stub)");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_NULL(ne_kernel_get_dos_environment(&ctx));

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_win_exec_stub(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("WinExec stub returns 0");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_win_exec(&ctx, "NOTEPAD.EXE", 1), (uint16_t)0u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_exit_windows_stub(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("ExitWindows stub returns 0");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_exit_windows(&ctx, 0), 0);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_get_tick_count_no_driver(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("GetTickCount without driver returns 0");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_get_tick_count(&ctx), (uint32_t)0u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_get_tick_count_with_driver(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    NEDrvContext    drv;

    TEST_BEGIN("GetTickCount with driver delegates correctly");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ne_drv_init(&drv);
    ne_drv_tmr_install(&drv);
    ne_drv_tmr_tick(&drv, 100);

    ne_kernel_set_driver(&ctx, &drv);
    ASSERT_EQ(ne_kernel_get_tick_count(&ctx), (uint32_t)100u);

    ne_drv_free(&drv);
    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_catch_throw(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    NECatchBuf      cbuf;
    int             val;

    TEST_BEGIN("Catch/Throw: setjmp/longjmp round-trip");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    val = ne_kernel_catch(&ctx, &cbuf);
    if (val == 0) {
        /* First return from Catch */
        ne_kernel_throw(&ctx, &cbuf, 42);
        /* Safety: should never reach here; throw always jumps */
        TEST_FAIL("throw did not jump");
    }

    /* Returned via Throw */
    ASSERT_EQ(val, 42);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_make_proc_instance(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    void           *proc;
    void           *result;

    TEST_BEGIN("MakeProcInstance returns pointer as-is (real mode)");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    proc = (void *)&test_make_proc_instance;
    result = ne_kernel_make_proc_instance(&ctx, proc, 1u);
    ASSERT_EQ((long)(uintptr_t)result, (long)(uintptr_t)proc);

    /* FreeProcInstance is a no-op; just verify it doesn't crash */
    ne_kernel_free_proc_instance(&ctx, result);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_open_file_exist(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    NEOfStruct      ofs;
    int             result;
    FILE           *fp;

    TEST_BEGIN("OpenFile: OF_EXIST on existing file succeeds");

    /* Create a temp file */
    fp = fopen("OPENTEST.TMP", "wb");
    if (!fp) TEST_FAIL("cannot create temp file");
    fwrite("test", 1, 4, fp);
    fclose(fp);

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    result = ne_kernel_open_file(&ctx, "OPENTEST.TMP", &ofs, NE_OF_EXIST);
    ASSERT_NE(result, NE_KERNEL_HFILE_ERROR);
    ASSERT_EQ(ofs.nErrCode, (uint16_t)0u);
    ASSERT_STR_EQ(ofs.szPathName, "OPENTEST.TMP");

    /* Non-existent file */
    result = ne_kernel_open_file(&ctx, "NOFILE.XXX", &ofs, NE_OF_EXIST);
    ASSERT_EQ(result, NE_KERNEL_HFILE_ERROR);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    remove("OPENTEST.TMP");
    TEST_PASS();
}

static void test_open_file_read(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    NEOfStruct      ofs;
    int             hFile;
    char            buf[16];
    FILE           *fp;

    TEST_BEGIN("OpenFile: OF_READ opens file for reading");

    fp = fopen("OPENTEST.TMP", "wb");
    if (!fp) TEST_FAIL("cannot create temp file");
    fwrite("hello", 1, 5, fp);
    fclose(fp);

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    hFile = ne_kernel_open_file(&ctx, "OPENTEST.TMP", &ofs, NE_OF_READ);
    ASSERT_NE(hFile, NE_KERNEL_HFILE_ERROR);

    memset(buf, 0, sizeof(buf));
    ASSERT_EQ(ne_kernel_lread(&ctx, hFile, buf, 5), 5);
    ASSERT_STR_EQ(buf, "hello");

    ne_kernel_lclose(&ctx, hFile);
    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    remove("OPENTEST.TMP");
    TEST_PASS();
}

static void test_open_file_delete(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    NEOfStruct      ofs;
    int             result;
    FILE           *fp;

    TEST_BEGIN("OpenFile: OF_DELETE removes the file");

    fp = fopen("DELTEST.TMP", "wb");
    if (!fp) TEST_FAIL("cannot create temp file");
    fwrite("x", 1, 1, fp);
    fclose(fp);

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    result = ne_kernel_open_file(&ctx, "DELTEST.TMP", &ofs, NE_OF_DELETE);
    ASSERT_NE(result, NE_KERNEL_HFILE_ERROR);

    /* Verify the file is gone */
    result = ne_kernel_open_file(&ctx, "DELTEST.TMP", &ofs, NE_OF_EXIST);
    ASSERT_EQ(result, NE_KERNEL_HFILE_ERROR);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_output_debug_string(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("OutputDebugString does not crash");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ne_kernel_output_debug_string(&ctx, "test debug message");
    ne_kernel_output_debug_string(&ctx, NULL);
    ne_kernel_output_debug_string(NULL, "should not crash");

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_set_error_mode(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    uint16_t        prev;

    TEST_BEGIN("SetErrorMode stores and returns previous mode");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    prev = ne_kernel_set_error_mode(&ctx, 1u);
    ASSERT_EQ(prev, (uint16_t)0u);

    prev = ne_kernel_set_error_mode(&ctx, 2u);
    ASSERT_EQ(prev, (uint16_t)1u);

    ASSERT_EQ(ctx.error_mode, (uint16_t)2u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_get_last_error(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("GetLastError returns stored error code");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_get_last_error(&ctx), (uint16_t)0u);

    ctx.last_error = 42u;
    ASSERT_EQ(ne_kernel_get_last_error(&ctx), (uint16_t)42u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void dummy_task_entry(void *arg) { (void)arg; }

static void test_is_task(void)
{
    NEGMemTable      gmem;
    NELMemHeap       lmem;
    NETaskTable      tasks;
    NEModuleTable    modules;
    NEKernelContext  ctx;
    NETaskHandle     h;

    TEST_BEGIN("IsTask: returns 1 for valid task, 0 for invalid");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    /* No tasks yet */
    ASSERT_EQ(ne_kernel_is_task(&ctx, 1u), 0);

    /* Create a task */
    ne_task_create(ctx.tasks, dummy_task_entry, NULL, 256, 0, &h);
    ASSERT_EQ(ne_kernel_is_task(&ctx, h), 1);

    /* Invalid handle */
    ASSERT_EQ(ne_kernel_is_task(&ctx, 999u), 0);

    ne_task_destroy(ctx.tasks, h);
    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_get_num_tasks(void)
{
    NEGMemTable      gmem;
    NELMemHeap       lmem;
    NETaskTable      tasks;
    NEModuleTable    modules;
    NEKernelContext  ctx;
    NETaskHandle     h1, h2;

    TEST_BEGIN("GetNumTasks: returns correct count");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_EQ(ne_kernel_get_num_tasks(&ctx), (uint16_t)0u);

    ne_task_create(ctx.tasks, dummy_task_entry, NULL, 256, 0, &h1);
    ASSERT_EQ(ne_kernel_get_num_tasks(&ctx), (uint16_t)1u);

    ne_task_create(ctx.tasks, dummy_task_entry, NULL, 256, 0, &h2);
    ASSERT_EQ(ne_kernel_get_num_tasks(&ctx), (uint16_t)2u);

    ne_task_destroy(ctx.tasks, h1);
    ASSERT_EQ(ne_kernel_get_num_tasks(&ctx), (uint16_t)1u);

    ne_task_destroy(ctx.tasks, h2);
    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_set_driver(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    int             dummy;

    TEST_BEGIN("set_driver attaches and detaches driver");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ASSERT_NULL(ctx.driver);
    ASSERT_EQ(ne_kernel_set_driver(&ctx, &dummy), NE_KERNEL_OK);
    ASSERT_NOT_NULL(ctx.driver);
    ASSERT_EQ(ne_kernel_set_driver(&ctx, NULL), NE_KERNEL_OK);
    ASSERT_NULL(ctx.driver);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_phase_a_null_ctx(void)
{
    char buf[32];

    TEST_BEGIN("Phase A APIs: NULL ctx return errors/zeros");

    ASSERT_EQ(ne_kernel_get_win_flags(NULL), (uint32_t)0u);
    ASSERT_EQ(ne_kernel_get_windows_directory(NULL, buf, sizeof(buf)), 0);
    ASSERT_EQ(ne_kernel_get_system_directory(NULL, buf, sizeof(buf)), 0);
    ASSERT_NULL(ne_kernel_get_dos_environment(NULL));
    ASSERT_EQ(ne_kernel_win_exec(NULL, "x", 0), (uint16_t)0u);
    ASSERT_EQ(ne_kernel_exit_windows(NULL, 0), 0);
    ASSERT_EQ(ne_kernel_get_tick_count(NULL), (uint32_t)0u);
    ASSERT_NULL(ne_kernel_make_proc_instance(NULL, NULL, 0));
    ASSERT_EQ(ne_kernel_set_error_mode(NULL, 0), (uint16_t)0u);
    ASSERT_EQ(ne_kernel_get_last_error(NULL), (uint16_t)0u);
    ASSERT_EQ(ne_kernel_is_task(NULL, 1u), 0);
    ASSERT_EQ(ne_kernel_get_num_tasks(NULL), (uint16_t)0u);
    ASSERT_EQ(ne_kernel_set_driver(NULL, NULL), NE_KERNEL_ERR_INIT);

    TEST_PASS();
}

/* =========================================================================
 * Phase B: INI File and Profile API tests
 * ===================================================================== */

#define PHASE_B_TEST_INI  "PHASE_B_TEST.INI"
#define PHASE_B_TEST_INI2 "PHASE_B_TEST2.INI"

/* Helper: create a test INI file with known content */
static void create_test_ini(const char *path, const char *content)
{
    FILE *fp = fopen(path, "w");
    if (fp) {
        fputs(content, fp);
        fclose(fp);
    }
}

/* Helper: remove test INI file */
static void remove_test_ini(const char *path)
{
    remove(path);
}

static void test_get_private_profile_string_basic(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    char            buf[64];
    int             len;

    TEST_BEGIN("GetPrivateProfileString: basic read");

    create_test_ini(PHASE_B_TEST_INI,
        "[Settings]\n"
        "Color=Blue\n"
        "Size=42\n"
        "[Other]\n"
        "Name=Test\n");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    len = ne_kernel_get_private_profile_string(
        &ctx, "Settings", "Color", "Red",
        buf, sizeof(buf), PHASE_B_TEST_INI);
    ASSERT_STR_EQ(buf, "Blue");
    ASSERT_EQ(len, 4);

    len = ne_kernel_get_private_profile_string(
        &ctx, "Other", "Name", "",
        buf, sizeof(buf), PHASE_B_TEST_INI);
    ASSERT_STR_EQ(buf, "Test");

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    remove_test_ini(PHASE_B_TEST_INI);
    TEST_PASS();
}

static void test_get_private_profile_string_default(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    char            buf[64];
    int             len;

    TEST_BEGIN("GetPrivateProfileString: default on missing key");

    create_test_ini(PHASE_B_TEST_INI,
        "[Settings]\n"
        "Color=Blue\n");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    len = ne_kernel_get_private_profile_string(
        &ctx, "Settings", "Missing", "DefaultVal",
        buf, sizeof(buf), PHASE_B_TEST_INI);
    ASSERT_STR_EQ(buf, "DefaultVal");
    ASSERT_EQ(len, 10);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    remove_test_ini(PHASE_B_TEST_INI);
    TEST_PASS();
}

static void test_get_private_profile_string_no_file(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    char            buf[64];
    int             len;

    TEST_BEGIN("GetPrivateProfileString: default on missing file");

    remove_test_ini("NOEXIST_B.INI");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    len = ne_kernel_get_private_profile_string(
        &ctx, "Sec", "Key", "Fallback",
        buf, sizeof(buf), "NOEXIST_B.INI");
    ASSERT_STR_EQ(buf, "Fallback");
    ASSERT_EQ(len, 8);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_get_private_profile_int_basic(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    uint16_t        val;

    TEST_BEGIN("GetPrivateProfileInt: basic read");

    create_test_ini(PHASE_B_TEST_INI,
        "[Numbers]\n"
        "Count=123\n");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    val = ne_kernel_get_private_profile_int(
        &ctx, "Numbers", "Count", 0, PHASE_B_TEST_INI);
    ASSERT_EQ(val, (uint16_t)123u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    remove_test_ini(PHASE_B_TEST_INI);
    TEST_PASS();
}

static void test_get_private_profile_int_default(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    uint16_t        val;

    TEST_BEGIN("GetPrivateProfileInt: default on missing key");

    create_test_ini(PHASE_B_TEST_INI,
        "[Numbers]\n"
        "Count=123\n");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    val = ne_kernel_get_private_profile_int(
        &ctx, "Numbers", "Missing", 99, PHASE_B_TEST_INI);
    ASSERT_EQ(val, (uint16_t)99u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    remove_test_ini(PHASE_B_TEST_INI);
    TEST_PASS();
}

static void test_write_private_profile_string_create(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    char            buf[64];
    int             rc;

    TEST_BEGIN("WritePrivateProfileString: creates new file");

    remove_test_ini(PHASE_B_TEST_INI2);

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    rc = ne_kernel_write_private_profile_string(
        &ctx, "App", "Key1", "Value1", PHASE_B_TEST_INI2);
    ASSERT_NE(rc, 0);

    /* Read it back */
    ne_kernel_get_private_profile_string(
        &ctx, "App", "Key1", "",
        buf, sizeof(buf), PHASE_B_TEST_INI2);
    ASSERT_STR_EQ(buf, "Value1");

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    remove_test_ini(PHASE_B_TEST_INI2);
    TEST_PASS();
}

static void test_write_private_profile_string_update(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    char            buf[64];

    TEST_BEGIN("WritePrivateProfileString: update existing key");

    create_test_ini(PHASE_B_TEST_INI,
        "[App]\n"
        "Key1=OldValue\n");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ne_kernel_write_private_profile_string(
        &ctx, "App", "Key1", "NewValue", PHASE_B_TEST_INI);

    ne_kernel_get_private_profile_string(
        &ctx, "App", "Key1", "",
        buf, sizeof(buf), PHASE_B_TEST_INI);
    ASSERT_STR_EQ(buf, "NewValue");

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    remove_test_ini(PHASE_B_TEST_INI);
    TEST_PASS();
}

static void test_write_private_profile_string_delete_key(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    char            buf[64];
    int             len;

    TEST_BEGIN("WritePrivateProfileString: delete key (NULL value)");

    create_test_ini(PHASE_B_TEST_INI,
        "[App]\n"
        "Keep=yes\n"
        "Delete=me\n");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ne_kernel_write_private_profile_string(
        &ctx, "App", "Delete", NULL, PHASE_B_TEST_INI);

    /* Deleted key should return default */
    len = ne_kernel_get_private_profile_string(
        &ctx, "App", "Delete", "gone",
        buf, sizeof(buf), PHASE_B_TEST_INI);
    ASSERT_STR_EQ(buf, "gone");

    /* Other key should still exist */
    ne_kernel_get_private_profile_string(
        &ctx, "App", "Keep", "",
        buf, sizeof(buf), PHASE_B_TEST_INI);
    ASSERT_STR_EQ(buf, "yes");

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    remove_test_ini(PHASE_B_TEST_INI);
    TEST_PASS();
}

static void test_write_private_profile_string_delete_section(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    char            buf[64];

    TEST_BEGIN("WritePrivateProfileString: delete section (NULL key)");

    create_test_ini(PHASE_B_TEST_INI,
        "[Keep]\n"
        "A=1\n"
        "[Remove]\n"
        "B=2\n"
        "C=3\n");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ne_kernel_write_private_profile_string(
        &ctx, "Remove", NULL, NULL, PHASE_B_TEST_INI);

    /* Removed section key should return default */
    ne_kernel_get_private_profile_string(
        &ctx, "Remove", "B", "def",
        buf, sizeof(buf), PHASE_B_TEST_INI);
    ASSERT_STR_EQ(buf, "def");

    /* Kept section should survive */
    ne_kernel_get_private_profile_string(
        &ctx, "Keep", "A", "",
        buf, sizeof(buf), PHASE_B_TEST_INI);
    ASSERT_STR_EQ(buf, "1");

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    remove_test_ini(PHASE_B_TEST_INI);
    TEST_PASS();
}

static void test_get_profile_string_default(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    char            buf[64];
    int             len;

    TEST_BEGIN("GetProfileString: returns default (WIN.INI)");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    len = ne_kernel_get_profile_string(
        &ctx, "windows", "NullPort", "None",
        buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "None");
    ASSERT_EQ(len, 4);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_get_profile_int_default(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    uint16_t        val;

    TEST_BEGIN("GetProfileInt: returns default (WIN.INI)");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    val = ne_kernel_get_profile_int(
        &ctx, "windows", "BorderWidth", 5);
    ASSERT_EQ(val, (uint16_t)5u);

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_write_profile_string(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;

    TEST_BEGIN("WriteProfileString: does not crash");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    /* May fail since WIN.INI path may not exist; just test
       it doesn't crash */
    (void)ne_kernel_write_profile_string(
        &ctx, "TestSec", "TestKey", "TestVal");

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    TEST_PASS();
}

static void test_ini_case_insensitive(void)
{
    NEGMemTable     gmem;
    NELMemHeap      lmem;
    NETaskTable     tasks;
    NEModuleTable   modules;
    NEKernelContext ctx;
    char            buf[64];

    TEST_BEGIN("INI: case-insensitive section and key lookup");

    create_test_ini(PHASE_B_TEST_INI,
        "[MySection]\n"
        "MyKey=MyValue\n");

    setup_kernel(&gmem, &lmem, &tasks, &modules, &ctx);

    ne_kernel_get_private_profile_string(
        &ctx, "MYSECTION", "MYKEY", "fail",
        buf, sizeof(buf), PHASE_B_TEST_INI);
    ASSERT_STR_EQ(buf, "MyValue");

    ne_kernel_get_private_profile_string(
        &ctx, "mysection", "mykey", "fail",
        buf, sizeof(buf), PHASE_B_TEST_INI);
    ASSERT_STR_EQ(buf, "MyValue");

    teardown_kernel(&gmem, &lmem, &tasks, &modules, &ctx);
    remove_test_ini(PHASE_B_TEST_INI);
    TEST_PASS();
}

static void test_ini_null_ctx(void)
{
    char buf[32];

    TEST_BEGIN("INI APIs: NULL ctx return defaults/errors");

    ASSERT_EQ(ne_kernel_get_private_profile_string(
        NULL, "s", "k", "d", buf, sizeof(buf), "f"), 0);
    ASSERT_EQ(ne_kernel_get_private_profile_int(
        NULL, "s", "k", 7, "f"), (uint16_t)7u);
    ASSERT_EQ(ne_kernel_write_private_profile_string(
        NULL, "s", "k", "v", "f"), 0);
    ASSERT_EQ(ne_kernel_get_profile_string(
        NULL, "s", "k", "d", buf, sizeof(buf)), 0);
    ASSERT_EQ(ne_kernel_get_profile_int(
        NULL, "s", "k", 3), (uint16_t)3u);
    ASSERT_EQ(ne_kernel_write_profile_string(
        NULL, "s", "k", "v"), 0);

    TEST_PASS();
}

int main(void)
{
    printf("=== WinDOS KERNEL.EXE API Stub Tests (Phase 2) ===\n\n");

    /* --- Kernel context --- */
    printf("--- Kernel context ---\n");
    test_kernel_init_free();
    test_kernel_init_null_args();
    test_kernel_free_null_safe();

    /* --- Export catalog --- */
    printf("\n--- Export catalog ---\n");
    test_catalog_returns_entries();
    test_catalog_null_args();
    test_catalog_classification();

    /* --- Export registration --- */
    printf("\n--- Export registration ---\n");
    test_register_exports();
    test_register_exports_resolvable();

    /* --- File I/O --- */
    printf("\n--- File I/O ---\n");
    test_file_io_write_read();
    test_file_io_open_nonexistent();
    test_file_io_null_args();
    test_file_io_llseek();

    /* --- Module APIs --- */
    printf("\n--- Module APIs ---\n");
    test_module_handle_not_found();
    test_module_filename_not_found();
    test_get_proc_address();
    test_load_library_unknown();
    test_free_library_invalid();

    /* --- Global memory --- */
    printf("\n--- Global memory ---\n");
    test_global_alloc_free();
    test_global_lock_unlock();
    test_global_realloc();
    test_global_null_ctx();

    /* --- Local memory --- */
    printf("\n--- Local memory ---\n");
    test_local_alloc_free();
    test_local_lock_unlock();
    test_local_null_ctx();

    /* --- Task / process --- */
    printf("\n--- Task / process ---\n");
    test_get_current_task_no_active();
    test_init_task();
    test_wait_event_post_event();
    test_task_null_ctx();

    /* --- String / resource stubs --- */
    printf("\n--- String / resource stubs ---\n");
    test_load_string_stub();
    test_find_resource_stub();
    test_load_resource_stub();
    test_lock_resource_stub();

    /* --- Atom APIs --- */
    printf("\n--- Atom APIs ---\n");
    test_atom_add_and_find();
    test_atom_add_duplicate();
    test_atom_get_name();
    test_atom_delete();
    test_atom_null_args();

    /* --- Error strings --- */
    printf("\n--- Error strings ---\n");
    test_strerror();

    /* --- Phase A APIs --- */
    printf("\n--- Phase A APIs ---\n");
    test_get_version();
    test_get_version_null_ctx();
    test_get_win_flags();
    test_get_windows_directory();
    test_get_system_directory();
    test_get_dos_environment();
    test_win_exec_stub();
    test_exit_windows_stub();
    test_get_tick_count_no_driver();
    test_get_tick_count_with_driver();
    test_catch_throw();
    test_make_proc_instance();
    test_open_file_exist();
    test_open_file_read();
    test_open_file_delete();
    test_output_debug_string();
    test_set_error_mode();
    test_get_last_error();
    test_is_task();
    test_get_num_tasks();
    test_set_driver();
    test_phase_a_null_ctx();

    /* --- Phase B APIs --- */
    printf("\n--- Phase B APIs ---\n");
    test_get_private_profile_string_basic();
    test_get_private_profile_string_default();
    test_get_private_profile_string_no_file();
    test_get_private_profile_int_basic();
    test_get_private_profile_int_default();
    test_write_private_profile_string_create();
    test_write_private_profile_string_update();
    test_write_private_profile_string_delete_key();
    test_write_private_profile_string_delete_section();
    test_get_profile_string_default();
    test_get_profile_int_default();
    test_write_profile_string();
    test_ini_case_insensitive();
    test_ini_null_ctx();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

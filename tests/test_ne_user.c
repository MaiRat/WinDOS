/*
 * test_ne_user.c - Tests for Phase 3: USER.EXE subsystem
 *
 * Verifies:
 *   - USER context initialisation and teardown
 *   - Window class registration
 *   - Window creation, destruction, visibility, and painting
 *   - Message queue: post, get, peek, quit
 *   - SendMessage / DispatchMessage / DefWindowProc
 *   - Full message loop lifecycle
 *   - Error string coverage
 */

#include "../src/ne_user.h"

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

/* -------------------------------------------------------------------------
 * Test window procedures
 * ---------------------------------------------------------------------- */

static uint32_t test_wndproc(uint16_t hwnd, uint16_t msg,
                             uint16_t wParam, uint32_t lParam)
{
    (void)hwnd; (void)wParam; (void)lParam;
    if (msg == WM_CREATE)  return 1;
    if (msg == WM_DESTROY) return 2;
    if (msg == WM_PAINT)   return 3;
    return 0;
}

static int      g_wndproc_call_count = 0;
static uint16_t g_last_msg           = 0;

static uint32_t tracking_wndproc(uint16_t hwnd, uint16_t msg,
                                 uint16_t wParam, uint32_t lParam)
{
    (void)hwnd; (void)wParam; (void)lParam;
    g_wndproc_call_count++;
    g_last_msg = msg;
    return 42;
}

/* =========================================================================
 * Context tests
 * ===================================================================== */

static void test_user_init_free(void)
{
    NEUserContext ctx;
    TEST_BEGIN("init sets initialized=1, free sets it to 0");
    ASSERT_EQ(ne_user_init(&ctx), NE_USER_OK);
    ASSERT_EQ(ctx.initialized, 1);
    ne_user_free(&ctx);
    ASSERT_EQ(ctx.initialized, 0);
    TEST_PASS();
}

static void test_user_init_null(void)
{
    TEST_BEGIN("init with NULL returns ERR_NULL");
    ASSERT_EQ(ne_user_init(NULL), NE_USER_ERR_NULL);
    TEST_PASS();
}

static void test_user_free_null_safe(void)
{
    TEST_BEGIN("free(NULL) does not crash");
    ne_user_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * Class registration tests
 * ===================================================================== */

static void test_register_class(void)
{
    NEUserContext ctx;
    TEST_BEGIN("register a class, class_count is 1");
    ne_user_init(&ctx);
    ASSERT_EQ(ne_user_register_class(&ctx, "TestClass", test_wndproc, 0),
              NE_USER_OK);
    ASSERT_EQ(ctx.class_count, 1);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_register_class_duplicate(void)
{
    NEUserContext ctx;
    TEST_BEGIN("duplicate class registration is idempotent");
    ne_user_init(&ctx);
    ASSERT_EQ(ne_user_register_class(&ctx, "Dup", test_wndproc, 0),
              NE_USER_OK);
    ASSERT_EQ(ne_user_register_class(&ctx, "Dup", test_wndproc, 0),
              NE_USER_OK);
    ASSERT_EQ(ctx.class_count, 1);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_register_class_null_args(void)
{
    NEUserContext ctx;
    TEST_BEGIN("register_class NULL / empty args return errors");
    ne_user_init(&ctx);
    ASSERT_EQ(ne_user_register_class(NULL, "X", test_wndproc, 0),
              NE_USER_ERR_NULL);
    ASSERT_EQ(ne_user_register_class(&ctx, NULL, test_wndproc, 0),
              NE_USER_ERR_NULL);
    ASSERT_EQ(ne_user_register_class(&ctx, "", test_wndproc, 0),
              NE_USER_ERR_NULL);
    ASSERT_EQ(ne_user_register_class(&ctx, "X", NULL, 0),
              NE_USER_ERR_NULL);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_register_class_full(void)
{
    NEUserContext ctx;
    char name[16];
    uint16_t i;
    TEST_BEGIN("class table full returns ERR_FULL");
    ne_user_init(&ctx);
    for (i = 0; i < NE_USER_WNDCLASS_CAP; i++) {
        sprintf(name, "cls%u", (unsigned)i);
        ASSERT_EQ(ne_user_register_class(&ctx, name, test_wndproc, 0),
                  NE_USER_OK);
    }
    ASSERT_EQ(ne_user_register_class(&ctx, "overflow", test_wndproc, 0),
              NE_USER_ERR_FULL);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Window management tests
 * ===================================================================== */

static void test_create_destroy_window(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    TEST_BEGIN("create window, verify hwnd; destroy, wnd_count=0");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Win", test_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Win", NE_USER_HWND_INVALID, 0);
    ASSERT_NE(hwnd, NE_USER_HWND_INVALID);
    ASSERT_EQ(ctx.wnd_count, 1);
    ASSERT_EQ(ne_user_destroy_window(&ctx, hwnd), NE_USER_OK);
    ASSERT_EQ(ctx.wnd_count, 0);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_create_window_unknown_class(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    TEST_BEGIN("create with unregistered class returns HWND_INVALID");
    ne_user_init(&ctx);
    hwnd = ne_user_create_window(&ctx, "NoSuch", NE_USER_HWND_INVALID, 0);
    ASSERT_EQ(hwnd, NE_USER_HWND_INVALID);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_create_window_sends_wm_create(void)
{
    NEUserContext ctx;
    TEST_BEGIN("create window sends WM_CREATE to wndproc");
    g_wndproc_call_count = 0;
    g_last_msg = 0;
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Track", tracking_wndproc, 0);
    ne_user_create_window(&ctx, "Track", NE_USER_HWND_INVALID, 0);
    ASSERT_EQ(g_last_msg, WM_CREATE);
    ASSERT_NE(g_wndproc_call_count, 0);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_destroy_window_sends_wm_destroy(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    TEST_BEGIN("destroy window sends WM_DESTROY to wndproc");
    g_wndproc_call_count = 0;
    g_last_msg = 0;
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Track2", tracking_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Track2", NE_USER_HWND_INVALID, 0);
    g_wndproc_call_count = 0;
    g_last_msg = 0;
    ne_user_destroy_window(&ctx, hwnd);
    ASSERT_EQ(g_last_msg, WM_DESTROY);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_show_window(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    NEUserWindow *wnd = NULL;
    uint16_t i;
    TEST_BEGIN("show_window SW_SHOW / SW_HIDE toggles visible");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Vis", test_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Vis", NE_USER_HWND_INVALID, 0);
    for (i = 0; i < NE_USER_WND_CAP; i++) {
        if (ctx.windows[i].active && ctx.windows[i].hwnd == hwnd) {
            wnd = &ctx.windows[i];
            break;
        }
    }
    ASSERT_NOT_NULL(wnd);
    ASSERT_EQ(wnd->visible, 0);
    ne_user_show_window(&ctx, hwnd, SW_SHOW);
    ASSERT_EQ(wnd->visible, 1);
    ne_user_show_window(&ctx, hwnd, SW_HIDE);
    ASSERT_EQ(wnd->visible, 0);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_update_window(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    NEUserWindow *wnd = NULL;
    uint16_t i;
    TEST_BEGIN("update_window clears needs_paint");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Upd", test_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Upd", NE_USER_HWND_INVALID, 0);
    ne_user_show_window(&ctx, hwnd, SW_SHOW);
    for (i = 0; i < NE_USER_WND_CAP; i++) {
        if (ctx.windows[i].active && ctx.windows[i].hwnd == hwnd) {
            wnd = &ctx.windows[i];
            break;
        }
    }
    ASSERT_NOT_NULL(wnd);
    ASSERT_EQ(wnd->needs_paint, 1);
    ne_user_update_window(&ctx, hwnd);
    ASSERT_EQ(wnd->needs_paint, 0);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Message loop tests
 * ===================================================================== */

static void test_post_and_get_message(void)
{
    NEUserContext ctx;
    NEUserMsg msg;
    int ret;
    TEST_BEGIN("post WM_USER then get_message returns matching msg");
    ne_user_init(&ctx);
    ASSERT_EQ(ne_user_post_message(&ctx, 1, WM_USER, 0xAB, 0xCDEF),
              NE_USER_OK);
    ret = ne_user_get_message(&ctx, &msg);
    ASSERT_EQ(ret, 1);
    ASSERT_EQ(msg.hwnd, 1);
    ASSERT_EQ(msg.message, WM_USER);
    ASSERT_EQ(msg.wParam, 0xAB);
    ASSERT_EQ(msg.lParam, 0xCDEF);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_get_message_empty_queue(void)
{
    NEUserContext ctx;
    NEUserMsg msg;
    TEST_BEGIN("get_message on empty queue returns 0");
    ne_user_init(&ctx);
    ASSERT_EQ(ne_user_get_message(&ctx, &msg), 0);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_peek_message_no_remove(void)
{
    NEUserContext ctx;
    NEUserMsg msg;
    TEST_BEGIN("peek without remove leaves message in queue");
    ne_user_init(&ctx);
    ne_user_post_message(&ctx, 1, WM_USER, 0, 0);
    ASSERT_EQ(ne_user_peek_message(&ctx, &msg, 0), 1);
    ASSERT_EQ(ne_user_peek_message(&ctx, &msg, 0), 1);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_peek_message_with_remove(void)
{
    NEUserContext ctx;
    NEUserMsg msg;
    TEST_BEGIN("peek with remove dequeues message");
    ne_user_init(&ctx);
    ne_user_post_message(&ctx, 1, WM_USER, 0, 0);
    ASSERT_EQ(ne_user_peek_message(&ctx, &msg, 1), 1);
    ASSERT_EQ(ne_user_peek_message(&ctx, &msg, 0), 0);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_post_quit_message(void)
{
    NEUserContext ctx;
    NEUserMsg msg;
    TEST_BEGIN("post quit, get_message returns 0 with WM_QUIT");
    ne_user_init(&ctx);
    ASSERT_EQ(ne_user_post_quit_message(&ctx, 0), NE_USER_OK);
    ASSERT_EQ(ne_user_get_message(&ctx, &msg), 0);
    ASSERT_EQ(msg.message, WM_QUIT);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_dispatch_message(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    NEUserMsg msg;
    TEST_BEGIN("dispatch_message invokes tracking_wndproc");
    g_wndproc_call_count = 0;
    g_last_msg = 0;
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Disp", tracking_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Disp", NE_USER_HWND_INVALID, 0);
    /* Reset after WM_CREATE */
    g_wndproc_call_count = 0;
    g_last_msg = 0;
    ne_user_post_message(&ctx, hwnd, WM_USER, 0, 0);
    ASSERT_EQ(ne_user_get_message(&ctx, &msg), 1);
    ne_user_dispatch_message(&ctx, &msg);
    ASSERT_EQ(g_last_msg, WM_USER);
    ASSERT_EQ(g_wndproc_call_count, 1);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * SendMessage / PostMessage / DefWindowProc tests
 * ===================================================================== */

static void test_send_message(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    uint32_t ret;
    TEST_BEGIN("send_message returns wndproc result (42)");
    g_wndproc_call_count = 0;
    g_last_msg = 0;
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Send", tracking_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Send", NE_USER_HWND_INVALID, 0);
    g_wndproc_call_count = 0;
    g_last_msg = 0;
    ret = ne_user_send_message(&ctx, hwnd, WM_USER, 0, 0);
    ASSERT_EQ(ret, 42u);
    ASSERT_EQ(g_last_msg, WM_USER);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_post_message_full_queue(void)
{
    NEUserContext ctx;
    uint16_t i;
    TEST_BEGIN("full queue returns ERR_FULL on post");
    ne_user_init(&ctx);
    for (i = 0; i < NE_USER_MSG_QUEUE_CAP; i++) {
        ASSERT_EQ(ne_user_post_message(&ctx, 0, WM_USER, 0, 0),
                  NE_USER_OK);
    }
    ASSERT_EQ(ne_user_post_message(&ctx, 0, WM_USER, 0, 0),
              NE_USER_ERR_FULL);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_def_window_proc(void)
{
    NEUserContext ctx;
    TEST_BEGIN("def_window_proc returns 0");
    ne_user_init(&ctx);
    ASSERT_EQ(ne_user_def_window_proc(&ctx, 1, WM_USER, 0, 0), 0u);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Message loop + window lifecycle integration test
 * ===================================================================== */

static void test_message_loop_lifecycle(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    NEUserMsg msg;
    int loop_count = 0;
    TEST_BEGIN("full message loop lifecycle");
    g_wndproc_call_count = 0;
    g_last_msg = 0;
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Life", tracking_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Life", NE_USER_HWND_INVALID,
                                 WS_VISIBLE);
    ne_user_show_window(&ctx, hwnd, SW_SHOW);
    ne_user_update_window(&ctx, hwnd);

    /* Reset tracking after setup messages */
    g_wndproc_call_count = 0;
    g_last_msg = 0;

    ne_user_post_message(&ctx, hwnd, WM_USER, 0, 0);
    ne_user_post_quit_message(&ctx, 0);

    while (ne_user_get_message(&ctx, &msg)) {
        ne_user_translate_message(&ctx, &msg);
        ne_user_dispatch_message(&ctx, &msg);
        loop_count++;
    }
    /* WM_USER dispatched once; WM_QUIT terminates the loop */
    ASSERT_EQ(loop_count, 1);
    ASSERT_EQ(msg.message, WM_QUIT);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Error string test
 * ===================================================================== */

static void test_user_strerror(void)
{
    TEST_BEGIN("strerror returns non-NULL for all error codes");
    ASSERT_NOT_NULL(ne_user_strerror(NE_USER_OK));
    ASSERT_NOT_NULL(ne_user_strerror(NE_USER_ERR_NULL));
    ASSERT_NOT_NULL(ne_user_strerror(NE_USER_ERR_INIT));
    ASSERT_NOT_NULL(ne_user_strerror(NE_USER_ERR_FULL));
    ASSERT_NOT_NULL(ne_user_strerror(NE_USER_ERR_NOT_FOUND));
    ASSERT_NOT_NULL(ne_user_strerror(NE_USER_ERR_BAD_HANDLE));
    ASSERT_NOT_NULL(ne_user_strerror(-99));
    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== WinDOS USER.EXE Subsystem Tests (Phase 3) ===\n");

    /* --- Context tests --- */
    printf("\n--- Context tests ---\n");
    test_user_init_free();
    test_user_init_null();
    test_user_free_null_safe();

    /* --- Class registration tests --- */
    printf("\n--- Class registration tests ---\n");
    test_register_class();
    test_register_class_duplicate();
    test_register_class_null_args();
    test_register_class_full();

    /* --- Window management tests --- */
    printf("\n--- Window management tests ---\n");
    test_create_destroy_window();
    test_create_window_unknown_class();
    test_create_window_sends_wm_create();
    test_destroy_window_sends_wm_destroy();
    test_show_window();
    test_update_window();

    /* --- Message loop tests --- */
    printf("\n--- Message loop tests ---\n");
    test_post_and_get_message();
    test_get_message_empty_queue();
    test_peek_message_no_remove();
    test_peek_message_with_remove();
    test_post_quit_message();
    test_dispatch_message();

    /* --- SendMessage / PostMessage / DefWindowProc tests --- */
    printf("\n--- SendMessage / PostMessage / DefWindowProc tests ---\n");
    test_send_message();
    test_post_message_full_queue();
    test_def_window_proc();

    /* --- Message loop + window lifecycle integration --- */
    printf("\n--- Message loop + window lifecycle integration ---\n");
    test_message_loop_lifecycle();

    /* --- Error strings --- */
    printf("\n--- Error strings ---\n");
    test_user_strerror();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

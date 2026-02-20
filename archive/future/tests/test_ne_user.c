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

static uint32_t simple_wndproc(uint16_t hwnd, uint16_t msg,
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
    ASSERT_EQ(ne_user_register_class(&ctx, "TestClass", simple_wndproc, 0),
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
    ASSERT_EQ(ne_user_register_class(&ctx, "Dup", simple_wndproc, 0),
              NE_USER_OK);
    ASSERT_EQ(ne_user_register_class(&ctx, "Dup", simple_wndproc, 0),
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
    ASSERT_EQ(ne_user_register_class(NULL, "X", simple_wndproc, 0),
              NE_USER_ERR_NULL);
    ASSERT_EQ(ne_user_register_class(&ctx, NULL, simple_wndproc, 0),
              NE_USER_ERR_NULL);
    ASSERT_EQ(ne_user_register_class(&ctx, "", simple_wndproc, 0),
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
        ASSERT_EQ(ne_user_register_class(&ctx, name, simple_wndproc, 0),
                  NE_USER_OK);
    }
    ASSERT_EQ(ne_user_register_class(&ctx, "overflow", simple_wndproc, 0),
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
    ne_user_register_class(&ctx, "Win", simple_wndproc, 0);
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
    ne_user_register_class(&ctx, "Vis", simple_wndproc, 0);
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
    ne_user_register_class(&ctx, "Upd", simple_wndproc, 0);
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
 * Phase D tests
 * ===================================================================== */

/* --- MessageBox tests --- */

static void test_message_box_ok(void)
{
    NEUserContext ctx;
    TEST_BEGIN("MessageBox MB_OK returns IDOK");
    ne_user_init(&ctx);
    ASSERT_EQ(ne_user_message_box(&ctx, 0, "Hello", "Title", MB_OK), IDOK);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_message_box_yesno(void)
{
    NEUserContext ctx;
    TEST_BEGIN("MessageBox MB_YESNO returns IDYES");
    ne_user_init(&ctx);
    ASSERT_EQ(ne_user_message_box(&ctx, 0, "Q?", "Ask", MB_YESNO), IDYES);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* --- Dialog tests --- */

static uint32_t dlg_proc(uint16_t hwnd, uint16_t msg,
                         uint16_t wParam, uint32_t lParam)
{
    (void)hwnd; (void)wParam; (void)lParam;
    if (msg == WM_CREATE) return 1;
    return 0;
}

static void test_create_end_dialog(void)
{
    NEUserContext ctx;
    NEUserHWND dlg;
    TEST_BEGIN("CreateDialog creates window, EndDialog destroys it");
    ne_user_init(&ctx);
    dlg = ne_user_create_dialog(&ctx, "TestDlg", NE_USER_HWND_INVALID,
                                dlg_proc);
    ASSERT_NE(dlg, NE_USER_HWND_INVALID);
    ASSERT_EQ(ctx.wnd_count, 1);
    ASSERT_EQ(ne_user_end_dialog(&ctx, dlg, 0), NE_USER_OK);
    ASSERT_EQ(ctx.wnd_count, 0);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_dialog_box(void)
{
    NEUserContext ctx;
    TEST_BEGIN("DialogBox runs and returns 0");
    ne_user_init(&ctx);
    ASSERT_EQ(ne_user_dialog_box(&ctx, "DlgTmpl", NE_USER_HWND_INVALID,
                                 dlg_proc), 0);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* --- Capture tests --- */

static void test_set_release_capture(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd, prev;
    TEST_BEGIN("SetCapture / ReleaseCapture tracks capture");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Cap", simple_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Cap", NE_USER_HWND_INVALID, 0);
    prev = ne_user_set_capture(&ctx, hwnd);
    ASSERT_EQ(prev, NE_USER_HWND_INVALID);
    ASSERT_EQ(ctx.capture_hwnd, hwnd);
    ne_user_release_capture(&ctx);
    ASSERT_EQ(ctx.capture_hwnd, NE_USER_HWND_INVALID);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* --- Rectangle tests --- */

static void test_get_client_rect(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    NEUserRect rect;
    TEST_BEGIN("GetClientRect returns (0,0,width,height)");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Rect", simple_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Rect", NE_USER_HWND_INVALID, 0);
    ASSERT_EQ(ne_user_get_client_rect(&ctx, hwnd, &rect), NE_USER_OK);
    ASSERT_EQ(rect.left, 0);
    ASSERT_EQ(rect.top, 0);
    ASSERT_EQ(rect.right, 100);   /* default width */
    ASSERT_EQ(rect.bottom, 50);   /* default height */
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_get_window_rect(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    NEUserRect rect;
    TEST_BEGIN("GetWindowRect returns (x,y,x+w,y+h)");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "WR", simple_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "WR", NE_USER_HWND_INVALID, 0);
    ne_user_move_window(&ctx, hwnd, 10, 20, 200, 150, 0);
    ASSERT_EQ(ne_user_get_window_rect(&ctx, hwnd, &rect), NE_USER_OK);
    ASSERT_EQ(rect.left, 10);
    ASSERT_EQ(rect.top, 20);
    ASSERT_EQ(rect.right, 210);
    ASSERT_EQ(rect.bottom, 170);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* --- MoveWindow / SetWindowPos tests --- */

static void test_move_window(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    NEUserRect rect;
    TEST_BEGIN("MoveWindow updates position and size");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Mv", simple_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Mv", NE_USER_HWND_INVALID, 0);
    ASSERT_EQ(ne_user_move_window(&ctx, hwnd, 5, 10, 300, 200, 0),
              NE_USER_OK);
    ne_user_get_client_rect(&ctx, hwnd, &rect);
    ASSERT_EQ(rect.right, 300);
    ASSERT_EQ(rect.bottom, 200);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_set_window_pos(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    NEUserRect rect;
    TEST_BEGIN("SetWindowPos updates position and size");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "SP", simple_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "SP", NE_USER_HWND_INVALID, 0);
    ASSERT_EQ(ne_user_set_window_pos(&ctx, hwnd, 0, 15, 25, 400, 300, 0),
              NE_USER_OK);
    ne_user_get_window_rect(&ctx, hwnd, &rect);
    ASSERT_EQ(rect.left, 15);
    ASSERT_EQ(rect.top, 25);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* --- Window text tests --- */

static void test_set_get_window_text(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    char buf[64];
    int len;
    TEST_BEGIN("SetWindowText / GetWindowText round-trip");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Txt", simple_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Txt", NE_USER_HWND_INVALID, 0);
    ASSERT_EQ(ne_user_set_window_text(&ctx, hwnd, "Hello WinDOS"),
              NE_USER_OK);
    len = ne_user_get_window_text(&ctx, hwnd, buf, sizeof(buf));
    ASSERT_EQ(len, 12);
    ASSERT_STR_EQ(buf, "Hello WinDOS");
    ne_user_free(&ctx);
    TEST_PASS();
}

/* --- EnableWindow / IsWindowEnabled / IsWindowVisible tests --- */

static void test_enable_window(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    TEST_BEGIN("EnableWindow toggles enabled state");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "En", simple_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "En", NE_USER_HWND_INVALID, 0);
    ASSERT_EQ(ne_user_is_window_enabled(&ctx, hwnd), 1);
    ne_user_enable_window(&ctx, hwnd, 0);
    ASSERT_EQ(ne_user_is_window_enabled(&ctx, hwnd), 0);
    ne_user_enable_window(&ctx, hwnd, 1);
    ASSERT_EQ(ne_user_is_window_enabled(&ctx, hwnd), 1);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_is_window_visible(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    TEST_BEGIN("IsWindowVisible reflects show/hide state");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "IV", simple_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "IV", NE_USER_HWND_INVALID, 0);
    ASSERT_EQ(ne_user_is_window_visible(&ctx, hwnd), 0);
    ne_user_show_window(&ctx, hwnd, SW_SHOW);
    ASSERT_EQ(ne_user_is_window_visible(&ctx, hwnd), 1);
    ne_user_show_window(&ctx, hwnd, SW_HIDE);
    ASSERT_EQ(ne_user_is_window_visible(&ctx, hwnd), 0);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* --- Focus tests --- */

static void test_set_get_focus(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd, prev;
    TEST_BEGIN("SetFocus / GetFocus track input focus");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Foc", simple_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Foc", NE_USER_HWND_INVALID, 0);
    ASSERT_EQ(ne_user_get_focus(&ctx), NE_USER_HWND_INVALID);
    prev = ne_user_set_focus(&ctx, hwnd);
    ASSERT_EQ(prev, NE_USER_HWND_INVALID);
    ASSERT_EQ(ne_user_get_focus(&ctx), hwnd);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* --- Invalidate / Validate tests --- */

static void test_invalidate_validate_rect(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    NEUserWindow *wnd = NULL;
    uint16_t i;
    TEST_BEGIN("InvalidateRect / ValidateRect toggle needs_paint");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Inv", simple_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Inv", NE_USER_HWND_INVALID, 0);
    for (i = 0; i < NE_USER_WND_CAP; i++) {
        if (ctx.windows[i].active && ctx.windows[i].hwnd == hwnd) {
            wnd = &ctx.windows[i];
            break;
        }
    }
    ASSERT_NOT_NULL(wnd);
    ASSERT_EQ(wnd->needs_paint, 0);
    ne_user_invalidate_rect(&ctx, hwnd, NULL, 1);
    ASSERT_EQ(wnd->needs_paint, 1);
    ne_user_validate_rect(&ctx, hwnd, NULL);
    ASSERT_EQ(wnd->needs_paint, 0);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* --- ScrollWindow test --- */

static void test_scroll_window(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    NEUserWindow *wnd = NULL;
    uint16_t i;
    TEST_BEGIN("ScrollWindow marks window for repaint");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Scr", simple_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Scr", NE_USER_HWND_INVALID, 0);
    for (i = 0; i < NE_USER_WND_CAP; i++) {
        if (ctx.windows[i].active && ctx.windows[i].hwnd == hwnd) {
            wnd = &ctx.windows[i];
            break;
        }
    }
    ASSERT_NOT_NULL(wnd);
    wnd->needs_paint = 0;
    ne_user_scroll_window(&ctx, hwnd, 10, 20, NULL, NULL);
    ASSERT_EQ(wnd->needs_paint, 1);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* --- Timer tests --- */

static void test_set_kill_timer(void)
{
    NEUserContext ctx;
    TEST_BEGIN("SetTimer returns id, KillTimer succeeds");
    ne_user_init(&ctx);
    ASSERT_NE(ne_user_set_timer(&ctx, 1, 100, 1000), (uint16_t)0);
    ASSERT_EQ(ne_user_kill_timer(&ctx, 1, 100), NE_USER_OK);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* --- Clipboard tests --- */

static void test_clipboard_roundtrip(void)
{
    NEUserContext ctx;
    const char *data = "clip data";
    char buf[32];
    uint16_t out_size = 0;
    TEST_BEGIN("Clipboard open/set/get/close round-trip");
    ne_user_init(&ctx);
    ASSERT_EQ(ne_user_open_clipboard(&ctx, 1), NE_USER_OK);
    ASSERT_EQ(ne_user_set_clipboard_data(&ctx, 1, data, 9), NE_USER_OK);
    ASSERT_EQ(ne_user_get_clipboard_data(&ctx, 1, buf, sizeof(buf),
                                         &out_size), NE_USER_OK);
    ASSERT_EQ(out_size, 9);
    ASSERT_EQ(ne_user_close_clipboard(&ctx), NE_USER_OK);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_clipboard_double_open(void)
{
    NEUserContext ctx;
    TEST_BEGIN("Clipboard double-open returns ERR_FULL");
    ne_user_init(&ctx);
    ASSERT_EQ(ne_user_open_clipboard(&ctx, 1), NE_USER_OK);
    ASSERT_EQ(ne_user_open_clipboard(&ctx, 2), NE_USER_ERR_FULL);
    ne_user_close_clipboard(&ctx);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* --- Caret tests --- */

static void test_caret_lifecycle(void)
{
    NEUserContext ctx;
    TEST_BEGIN("Caret create/position/show/hide/destroy lifecycle");
    ne_user_init(&ctx);
    ASSERT_EQ(ne_user_create_caret(&ctx, 1, 2, 16), NE_USER_OK);
    ASSERT_EQ(ctx.caret.created, 1);
    ASSERT_EQ(ne_user_set_caret_pos(&ctx, 10, 20), NE_USER_OK);
    ASSERT_EQ(ctx.caret.x, 10);
    ASSERT_EQ(ctx.caret.y, 20);
    ASSERT_EQ(ne_user_show_caret(&ctx, 1), NE_USER_OK);
    ASSERT_EQ(ctx.caret.visible, 1);
    ASSERT_EQ(ne_user_hide_caret(&ctx, 1), NE_USER_OK);
    ASSERT_EQ(ctx.caret.visible, 0);
    ASSERT_EQ(ne_user_destroy_caret(&ctx), NE_USER_OK);
    ASSERT_EQ(ctx.caret.created, 0);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* --- Key state tests --- */

static void test_key_state(void)
{
    NEUserContext ctx;
    TEST_BEGIN("GetKeyState returns 0x8000 when key is down");
    ne_user_init(&ctx);
    ASSERT_EQ(ne_user_get_key_state(&ctx, 0x41), 0);
    ctx.key_state[0x41] = 1;
    ASSERT_NE(ne_user_get_key_state(&ctx, 0x41), 0);
    ASSERT_NE(ne_user_get_async_key_state(&ctx, 0x41), 0);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* --- Menu tests --- */

static void test_menu_create_append_destroy(void)
{
    NEUserContext ctx;
    uint16_t hmenu;
    TEST_BEGIN("CreateMenu / AppendMenu / DestroyMenu lifecycle");
    ne_user_init(&ctx);
    hmenu = ne_user_create_menu(&ctx);
    ASSERT_NE(hmenu, (uint16_t)0);
    ASSERT_EQ(ctx.menu_count, 1);
    ASSERT_EQ(ne_user_append_menu(&ctx, hmenu, 0, 101, "File"), NE_USER_OK);
    ASSERT_EQ(ne_user_append_menu(&ctx, hmenu, 0, 102, "Edit"), NE_USER_OK);
    ASSERT_EQ(ne_user_destroy_menu(&ctx, hmenu), NE_USER_OK);
    ASSERT_EQ(ctx.menu_count, 0);
    ne_user_free(&ctx);
    TEST_PASS();
}

static void test_menu_set_get(void)
{
    NEUserContext ctx;
    NEUserHWND hwnd;
    uint16_t hmenu;
    TEST_BEGIN("SetMenu / GetMenu attach menu to window");
    ne_user_init(&ctx);
    ne_user_register_class(&ctx, "Mn", simple_wndproc, 0);
    hwnd = ne_user_create_window(&ctx, "Mn", NE_USER_HWND_INVALID, 0);
    hmenu = ne_user_create_menu(&ctx);
    ASSERT_EQ(ne_user_set_menu(&ctx, hwnd, hmenu), NE_USER_OK);
    ASSERT_EQ(ne_user_get_menu(&ctx, hwnd), hmenu);
    ne_user_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== WinDOS USER.EXE Subsystem Tests (Phase 3 + Phase D) ===\n");

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

    /* --- Phase D: MessageBox tests --- */
    printf("\n--- Phase D: MessageBox tests ---\n");
    test_message_box_ok();
    test_message_box_yesno();

    /* --- Phase D: Dialog tests --- */
    printf("\n--- Phase D: Dialog tests ---\n");
    test_create_end_dialog();
    test_dialog_box();

    /* --- Phase D: Capture tests --- */
    printf("\n--- Phase D: Capture tests ---\n");
    test_set_release_capture();

    /* --- Phase D: Rectangle tests --- */
    printf("\n--- Phase D: Rectangle tests ---\n");
    test_get_client_rect();
    test_get_window_rect();

    /* --- Phase D: MoveWindow / SetWindowPos tests --- */
    printf("\n--- Phase D: MoveWindow / SetWindowPos tests ---\n");
    test_move_window();
    test_set_window_pos();

    /* --- Phase D: Window text tests --- */
    printf("\n--- Phase D: Window text tests ---\n");
    test_set_get_window_text();

    /* --- Phase D: Window state tests --- */
    printf("\n--- Phase D: Window state tests ---\n");
    test_enable_window();
    test_is_window_visible();

    /* --- Phase D: Focus tests --- */
    printf("\n--- Phase D: Focus tests ---\n");
    test_set_get_focus();

    /* --- Phase D: Invalidate / Validate tests --- */
    printf("\n--- Phase D: Invalidate / Validate tests ---\n");
    test_invalidate_validate_rect();

    /* --- Phase D: ScrollWindow tests --- */
    printf("\n--- Phase D: ScrollWindow tests ---\n");
    test_scroll_window();

    /* --- Phase D: Timer tests --- */
    printf("\n--- Phase D: Timer tests ---\n");
    test_set_kill_timer();

    /* --- Phase D: Clipboard tests --- */
    printf("\n--- Phase D: Clipboard tests ---\n");
    test_clipboard_roundtrip();
    test_clipboard_double_open();

    /* --- Phase D: Caret tests --- */
    printf("\n--- Phase D: Caret tests ---\n");
    test_caret_lifecycle();

    /* --- Phase D: Key state tests --- */
    printf("\n--- Phase D: Key state tests ---\n");
    test_key_state();

    /* --- Phase D: Menu tests --- */
    printf("\n--- Phase D: Menu tests ---\n");
    test_menu_create_append_destroy();
    test_menu_set_get();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

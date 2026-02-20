/*
 * test_ne_driver.c - Tests for Phase 4: Device Driver Integration
 *
 * Verifies:
 *   - Driver context initialisation and teardown
 *   - Keyboard driver: scan code translation, event queue
 *   - Timer driver: tick count, SetTimer, KillTimer, expiry check
 *   - Display driver: text buffer, cursor, string output
 *   - Mouse driver: event queue, position tracking
 *   - Driver coexistence verification
 *   - Error string coverage
 */

#include "../src/ne_driver.h"

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

static void test_drv_init_free(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("init sets initialized=1, free sets it to 0");
    ASSERT_EQ(ne_drv_init(&ctx), NE_DRV_OK);
    ASSERT_EQ(ctx.initialized, 1);
    ne_drv_free(&ctx);
    ASSERT_EQ(ctx.initialized, 0);
    TEST_PASS();
}

static void test_drv_init_null(void)
{
    TEST_BEGIN("init with NULL returns ERR_NULL");
    ASSERT_EQ(ne_drv_init(NULL), NE_DRV_ERR_NULL);
    TEST_PASS();
}

static void test_drv_free_null_safe(void)
{
    TEST_BEGIN("free(NULL) does not crash");
    ne_drv_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * Keyboard driver tests
 * ===================================================================== */

static void test_kbd_install_uninstall(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("kbd install/uninstall sets installed flag");
    ne_drv_init(&ctx);
    ASSERT_EQ(ne_drv_kbd_install(&ctx), NE_DRV_OK);
    ASSERT_EQ(ctx.kbd.installed, 1);
    ASSERT_EQ(ne_drv_kbd_uninstall(&ctx), NE_DRV_OK);
    ASSERT_EQ(ctx.kbd.installed, 0);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_kbd_scancode_translation(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("scan code 0x1E maps to VK_A");
    ne_drv_init(&ctx);
    ne_drv_kbd_install(&ctx);
    ASSERT_EQ(ne_drv_scancode_to_vk(&ctx, 0x1E), VK_A);
    ASSERT_EQ(ne_drv_scancode_to_vk(&ctx, 0x10), VK_Q);
    ASSERT_EQ(ne_drv_scancode_to_vk(&ctx, 0x01), VK_ESCAPE);
    ASSERT_EQ(ne_drv_scancode_to_vk(&ctx, 0x39), VK_SPACE);
    ASSERT_EQ(ne_drv_scancode_to_vk(&ctx, 0x1C), VK_RETURN);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_kbd_push_pop_keydown(void)
{
    NEDrvContext ctx;
    NEDrvKeyEvent evt;
    TEST_BEGIN("push scan code 0x1E -> pop WM_KEYDOWN VK_A");
    ne_drv_init(&ctx);
    ne_drv_kbd_install(&ctx);
    ASSERT_EQ(ne_drv_kbd_push_scancode(&ctx, 0x1E), NE_DRV_OK);
    ASSERT_EQ(ne_drv_kbd_pending(&ctx), 1);
    ASSERT_EQ(ne_drv_kbd_pop_event(&ctx, &evt), NE_DRV_OK);
    ASSERT_EQ(evt.scan_code, 0x1E);
    ASSERT_EQ(evt.vk, VK_A);
    ASSERT_EQ(evt.message, WM_KEYDOWN);
    ASSERT_EQ(ne_drv_kbd_pending(&ctx), 0);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_kbd_push_pop_keyup(void)
{
    NEDrvContext ctx;
    NEDrvKeyEvent evt;
    TEST_BEGIN("push scan code 0x9E (release A) -> WM_KEYUP VK_A");
    ne_drv_init(&ctx);
    ne_drv_kbd_install(&ctx);
    ASSERT_EQ(ne_drv_kbd_push_scancode(&ctx, 0x9E), NE_DRV_OK);
    ASSERT_EQ(ne_drv_kbd_pop_event(&ctx, &evt), NE_DRV_OK);
    ASSERT_EQ(evt.vk, VK_A);
    ASSERT_EQ(evt.message, WM_KEYUP);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_kbd_queue_full(void)
{
    NEDrvContext ctx;
    uint16_t i;
    TEST_BEGIN("keyboard event queue full returns ERR_FULL");
    ne_drv_init(&ctx);
    ne_drv_kbd_install(&ctx);
    for (i = 0; i < NE_DRV_KEY_EVENT_CAP; i++) {
        ASSERT_EQ(ne_drv_kbd_push_scancode(&ctx, 0x1E), NE_DRV_OK);
    }
    ASSERT_EQ(ne_drv_kbd_push_scancode(&ctx, 0x1E), NE_DRV_ERR_FULL);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_kbd_pop_empty(void)
{
    NEDrvContext ctx;
    NEDrvKeyEvent evt;
    TEST_BEGIN("pop from empty keyboard queue returns ERR_NOT_FOUND");
    ne_drv_init(&ctx);
    ne_drv_kbd_install(&ctx);
    ASSERT_EQ(ne_drv_kbd_pop_event(&ctx, &evt), NE_DRV_ERR_NOT_FOUND);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_kbd_multiple_keys(void)
{
    NEDrvContext ctx;
    NEDrvKeyEvent evt;
    TEST_BEGIN("push multiple keys, pop in FIFO order");
    ne_drv_init(&ctx);
    ne_drv_kbd_install(&ctx);
    ne_drv_kbd_push_scancode(&ctx, 0x1E); /* A */
    ne_drv_kbd_push_scancode(&ctx, 0x30); /* B */
    ne_drv_kbd_push_scancode(&ctx, 0x2E); /* C */
    ASSERT_EQ(ne_drv_kbd_pending(&ctx), 3);
    ne_drv_kbd_pop_event(&ctx, &evt);
    ASSERT_EQ(evt.vk, VK_A);
    ne_drv_kbd_pop_event(&ctx, &evt);
    ASSERT_EQ(evt.vk, VK_B);
    ne_drv_kbd_pop_event(&ctx, &evt);
    ASSERT_EQ(evt.vk, VK_C);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_kbd_function_keys(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("function key scan codes map to VK_F1..VK_F10");
    ne_drv_init(&ctx);
    ne_drv_kbd_install(&ctx);
    ASSERT_EQ(ne_drv_scancode_to_vk(&ctx, 0x3B), VK_F1);
    ASSERT_EQ(ne_drv_scancode_to_vk(&ctx, 0x44), VK_F10);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_kbd_arrow_keys(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("arrow key scan codes map to VK_UP/DOWN/LEFT/RIGHT");
    ne_drv_init(&ctx);
    ne_drv_kbd_install(&ctx);
    ASSERT_EQ(ne_drv_scancode_to_vk(&ctx, 0x48), VK_UP);
    ASSERT_EQ(ne_drv_scancode_to_vk(&ctx, 0x50), VK_DOWN);
    ASSERT_EQ(ne_drv_scancode_to_vk(&ctx, 0x4B), VK_LEFT);
    ASSERT_EQ(ne_drv_scancode_to_vk(&ctx, 0x4D), VK_RIGHT);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_kbd_null_args(void)
{
    NEDrvKeyEvent evt;
    NEDrvContext ctx;
    TEST_BEGIN("keyboard NULL arg checks");
    ASSERT_EQ(ne_drv_kbd_install(NULL), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_kbd_uninstall(NULL), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_kbd_push_scancode(NULL, 0x1E), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_kbd_pop_event(NULL, &evt), NE_DRV_ERR_NULL);
    ne_drv_init(&ctx);
    ASSERT_EQ(ne_drv_kbd_pop_event(&ctx, NULL), NE_DRV_ERR_NULL);
    ne_drv_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Timer driver tests
 * ===================================================================== */

static void test_tmr_install_uninstall(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("timer install/uninstall sets installed flag");
    ne_drv_init(&ctx);
    ASSERT_EQ(ne_drv_tmr_install(&ctx), NE_DRV_OK);
    ASSERT_EQ(ctx.tmr.installed, 1);
    ASSERT_EQ(ne_drv_tmr_uninstall(&ctx), NE_DRV_OK);
    ASSERT_EQ(ctx.tmr.installed, 0);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_tmr_tick_count(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("tick count advances monotonically");
    ne_drv_init(&ctx);
    ne_drv_tmr_install(&ctx);
    ASSERT_EQ(ne_drv_get_tick_count(&ctx), 0u);
    ne_drv_tmr_tick(&ctx, 100);
    ASSERT_EQ(ne_drv_get_tick_count(&ctx), 100u);
    ne_drv_tmr_tick(&ctx, 50);
    ASSERT_EQ(ne_drv_get_tick_count(&ctx), 150u);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_tmr_set_kill(void)
{
    NEDrvContext ctx;
    uint16_t id;
    TEST_BEGIN("SetTimer returns non-zero ID; KillTimer removes it");
    ne_drv_init(&ctx);
    ne_drv_tmr_install(&ctx);
    id = ne_drv_set_timer(&ctx, 1, 1000);
    ASSERT_NE(id, 0);
    ASSERT_EQ(ctx.tmr.timer_count, 1);
    ASSERT_EQ(ne_drv_kill_timer(&ctx, id), NE_DRV_OK);
    ASSERT_EQ(ctx.tmr.timer_count, 0);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_tmr_kill_not_found(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("KillTimer with unknown ID returns ERR_NOT_FOUND");
    ne_drv_init(&ctx);
    ne_drv_tmr_install(&ctx);
    ASSERT_EQ(ne_drv_kill_timer(&ctx, 99), NE_DRV_ERR_NOT_FOUND);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_tmr_kill_bad_id(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("KillTimer with ID 0 returns ERR_BAD_ID");
    ne_drv_init(&ctx);
    ne_drv_tmr_install(&ctx);
    ASSERT_EQ(ne_drv_kill_timer(&ctx, 0), NE_DRV_ERR_BAD_ID);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_tmr_set_zero_interval(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("SetTimer with 0 interval returns 0 (failure)");
    ne_drv_init(&ctx);
    ne_drv_tmr_install(&ctx);
    ASSERT_EQ(ne_drv_set_timer(&ctx, 1, 0), 0);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_tmr_expiry_check(void)
{
    NEDrvContext ctx;
    uint16_t id;
    uint16_t ids[4], hwnds[4];
    uint16_t n;
    TEST_BEGIN("timer expiry detection after tick advance");
    ne_drv_init(&ctx);
    ne_drv_tmr_install(&ctx);
    id = ne_drv_set_timer(&ctx, 5, 100);
    ASSERT_NE(id, 0);

    /* Before timer fires */
    n = ne_drv_tmr_check_expired(&ctx, ids, hwnds, 4);
    ASSERT_EQ(n, 0);

    /* Advance past the timer interval */
    ne_drv_tmr_tick(&ctx, 100);
    n = ne_drv_tmr_check_expired(&ctx, ids, hwnds, 4);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(ids[0], id);
    ASSERT_EQ(hwnds[0], 5);

    /* Timer should have been rescheduled; not expired again yet */
    n = ne_drv_tmr_check_expired(&ctx, ids, hwnds, 4);
    ASSERT_EQ(n, 0);

    /* Advance again for periodic fire */
    ne_drv_tmr_tick(&ctx, 100);
    n = ne_drv_tmr_check_expired(&ctx, ids, hwnds, 4);
    ASSERT_EQ(n, 1);

    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_tmr_multiple_timers(void)
{
    NEDrvContext ctx;
    uint16_t id1, id2;
    uint16_t ids[4], hwnds[4];
    uint16_t n;
    TEST_BEGIN("multiple timers with different intervals");
    ne_drv_init(&ctx);
    ne_drv_tmr_install(&ctx);
    id1 = ne_drv_set_timer(&ctx, 1, 50);
    id2 = ne_drv_set_timer(&ctx, 2, 100);
    ASSERT_NE(id1, 0);
    ASSERT_NE(id2, 0);
    ASSERT_NE(id1, id2);

    /* Advance 50 ms - only first timer fires */
    ne_drv_tmr_tick(&ctx, 50);
    n = ne_drv_tmr_check_expired(&ctx, ids, hwnds, 4);
    ASSERT_EQ(n, 1);
    ASSERT_EQ(ids[0], id1);

    /* Advance another 50 ms - both fire */
    ne_drv_tmr_tick(&ctx, 50);
    n = ne_drv_tmr_check_expired(&ctx, ids, hwnds, 4);
    ASSERT_EQ(n, 2);

    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_tmr_table_full(void)
{
    NEDrvContext ctx;
    uint16_t i;
    TEST_BEGIN("timer table full returns 0 from SetTimer");
    ne_drv_init(&ctx);
    ne_drv_tmr_install(&ctx);
    for (i = 0; i < NE_DRV_TIMER_CAP; i++) {
        ASSERT_NE(ne_drv_set_timer(&ctx, 1, 100), 0);
    }
    ASSERT_EQ(ne_drv_set_timer(&ctx, 1, 100), 0);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_tmr_null_args(void)
{
    TEST_BEGIN("timer NULL arg checks");
    ASSERT_EQ(ne_drv_tmr_install(NULL), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_tmr_uninstall(NULL), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_get_tick_count(NULL), 0u);
    ASSERT_EQ(ne_drv_set_timer(NULL, 1, 100), 0);
    ASSERT_EQ(ne_drv_kill_timer(NULL, 1), NE_DRV_ERR_NULL);
    TEST_PASS();
}

/* =========================================================================
 * Display driver tests
 * ===================================================================== */

static void test_disp_install_uninstall(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("display install/uninstall sets installed flag");
    ne_drv_init(&ctx);
    ASSERT_EQ(ne_drv_disp_install(&ctx), NE_DRV_OK);
    ASSERT_EQ(ctx.disp.installed, 1);
    ASSERT_EQ(ne_drv_disp_uninstall(&ctx), NE_DRV_OK);
    ASSERT_EQ(ctx.disp.installed, 0);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_disp_clear(void)
{
    NEDrvContext ctx;
    char     ch;
    uint8_t  attr;
    TEST_BEGIN("display clear fills buffer with spaces");
    ne_drv_init(&ctx);
    ne_drv_disp_install(&ctx);
    ne_drv_disp_getchar(&ctx, 0, 0, &ch, &attr);
    ASSERT_EQ(ch, ' ');
    ASSERT_EQ(attr, 0x07);
    ne_drv_disp_getchar(&ctx, 24, 79, &ch, &attr);
    ASSERT_EQ(ch, ' ');
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_disp_putchar_getchar(void)
{
    NEDrvContext ctx;
    char     ch;
    uint8_t  attr;
    TEST_BEGIN("putchar/getchar round-trip");
    ne_drv_init(&ctx);
    ne_drv_disp_install(&ctx);
    ASSERT_EQ(ne_drv_disp_putchar(&ctx, 5, 10, 'H', 0x0F), NE_DRV_OK);
    ASSERT_EQ(ne_drv_disp_getchar(&ctx, 5, 10, &ch, &attr), NE_DRV_OK);
    ASSERT_EQ(ch, 'H');
    ASSERT_EQ(attr, 0x0F);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_disp_putchar_out_of_range(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("putchar out-of-range returns ERR_BAD_ID");
    ne_drv_init(&ctx);
    ne_drv_disp_install(&ctx);
    ASSERT_EQ(ne_drv_disp_putchar(&ctx, 25, 0, 'X', 0x07), NE_DRV_ERR_BAD_ID);
    ASSERT_EQ(ne_drv_disp_putchar(&ctx, 0, 80, 'X', 0x07), NE_DRV_ERR_BAD_ID);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_disp_cursor(void)
{
    NEDrvContext ctx;
    uint8_t row, col;
    TEST_BEGIN("set_cursor/get_cursor round-trip");
    ne_drv_init(&ctx);
    ne_drv_disp_install(&ctx);
    ASSERT_EQ(ne_drv_disp_set_cursor(&ctx, 12, 40), NE_DRV_OK);
    ASSERT_EQ(ne_drv_disp_get_cursor(&ctx, &row, &col), NE_DRV_OK);
    ASSERT_EQ(row, 12);
    ASSERT_EQ(col, 40);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_disp_cursor_out_of_range(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("set_cursor out-of-range returns ERR_BAD_ID");
    ne_drv_init(&ctx);
    ne_drv_disp_install(&ctx);
    ASSERT_EQ(ne_drv_disp_set_cursor(&ctx, 25, 0), NE_DRV_ERR_BAD_ID);
    ASSERT_EQ(ne_drv_disp_set_cursor(&ctx, 0, 80), NE_DRV_ERR_BAD_ID);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_disp_write_string(void)
{
    NEDrvContext ctx;
    char    ch;
    uint8_t attr;
    uint8_t row, col;
    TEST_BEGIN("write_string writes chars and advances cursor");
    ne_drv_init(&ctx);
    ne_drv_disp_install(&ctx);
    ne_drv_disp_set_cursor(&ctx, 0, 0);
    ASSERT_EQ(ne_drv_disp_write_string(&ctx, "Hi", 0x0E), NE_DRV_OK);
    ne_drv_disp_getchar(&ctx, 0, 0, &ch, &attr);
    ASSERT_EQ(ch, 'H');
    ASSERT_EQ(attr, 0x0E);
    ne_drv_disp_getchar(&ctx, 0, 1, &ch, &attr);
    ASSERT_EQ(ch, 'i');
    ne_drv_disp_get_cursor(&ctx, &row, &col);
    ASSERT_EQ(row, 0);
    ASSERT_EQ(col, 2);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_disp_null_args(void)
{
    char    ch;
    uint8_t attr;
    uint8_t row, col;
    TEST_BEGIN("display NULL arg checks");
    ASSERT_EQ(ne_drv_disp_install(NULL), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_disp_uninstall(NULL), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_disp_clear(NULL), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_disp_putchar(NULL, 0, 0, 'X', 7), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_disp_getchar(NULL, 0, 0, &ch, &attr), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_disp_set_cursor(NULL, 0, 0), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_disp_get_cursor(NULL, &row, &col), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_disp_write_string(NULL, "X", 7), NE_DRV_ERR_NULL);
    TEST_PASS();
}

/* =========================================================================
 * Mouse driver tests
 * ===================================================================== */

static void test_mouse_install_uninstall(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("mouse install/uninstall sets installed flag");
    ne_drv_init(&ctx);
    ASSERT_EQ(ne_drv_mouse_install(&ctx), NE_DRV_OK);
    ASSERT_EQ(ctx.mouse.installed, 1);
    ASSERT_EQ(ne_drv_mouse_uninstall(&ctx), NE_DRV_OK);
    ASSERT_EQ(ctx.mouse.installed, 0);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_mouse_push_pop(void)
{
    NEDrvContext ctx;
    NEDrvMouseEvent evt;
    TEST_BEGIN("push/pop mouse move event");
    ne_drv_init(&ctx);
    ne_drv_mouse_install(&ctx);
    ASSERT_EQ(ne_drv_mouse_push_event(&ctx, WM_MOUSEMOVE, 100, 200, 0),
              NE_DRV_OK);
    ASSERT_EQ(ne_drv_mouse_pending(&ctx), 1);
    ASSERT_EQ(ne_drv_mouse_pop_event(&ctx, &evt), NE_DRV_OK);
    ASSERT_EQ(evt.message, WM_MOUSEMOVE);
    ASSERT_EQ(evt.x, 100);
    ASSERT_EQ(evt.y, 200);
    ASSERT_EQ(evt.buttons, 0);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_mouse_button_events(void)
{
    NEDrvContext ctx;
    NEDrvMouseEvent evt;
    TEST_BEGIN("push/pop left button down/up events");
    ne_drv_init(&ctx);
    ne_drv_mouse_install(&ctx);
    ne_drv_mouse_push_event(&ctx, WM_LBUTTONDOWN, 50, 60, 1);
    ne_drv_mouse_push_event(&ctx, WM_LBUTTONUP, 50, 60, 0);
    ASSERT_EQ(ne_drv_mouse_pending(&ctx), 2);
    ne_drv_mouse_pop_event(&ctx, &evt);
    ASSERT_EQ(evt.message, WM_LBUTTONDOWN);
    ASSERT_EQ(evt.buttons, 1);
    ne_drv_mouse_pop_event(&ctx, &evt);
    ASSERT_EQ(evt.message, WM_LBUTTONUP);
    ASSERT_EQ(evt.buttons, 0);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_mouse_position(void)
{
    NEDrvContext ctx;
    int16_t x, y;
    uint16_t buttons;
    TEST_BEGIN("get_position reflects last pushed event");
    ne_drv_init(&ctx);
    ne_drv_mouse_install(&ctx);
    ne_drv_mouse_push_event(&ctx, WM_MOUSEMOVE, 320, 240, 0);
    ASSERT_EQ(ne_drv_mouse_get_position(&ctx, &x, &y, &buttons), NE_DRV_OK);
    ASSERT_EQ(x, 320);
    ASSERT_EQ(y, 240);
    ASSERT_EQ(buttons, 0);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_mouse_queue_full(void)
{
    NEDrvContext ctx;
    uint16_t i;
    TEST_BEGIN("mouse event queue full returns ERR_FULL");
    ne_drv_init(&ctx);
    ne_drv_mouse_install(&ctx);
    for (i = 0; i < NE_DRV_MOUSE_EVENT_CAP; i++) {
        ASSERT_EQ(ne_drv_mouse_push_event(&ctx, WM_MOUSEMOVE, 0, 0, 0),
                  NE_DRV_OK);
    }
    ASSERT_EQ(ne_drv_mouse_push_event(&ctx, WM_MOUSEMOVE, 0, 0, 0),
              NE_DRV_ERR_FULL);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_mouse_pop_empty(void)
{
    NEDrvContext ctx;
    NEDrvMouseEvent evt;
    TEST_BEGIN("pop from empty mouse queue returns ERR_NOT_FOUND");
    ne_drv_init(&ctx);
    ne_drv_mouse_install(&ctx);
    ASSERT_EQ(ne_drv_mouse_pop_event(&ctx, &evt), NE_DRV_ERR_NOT_FOUND);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_mouse_null_args(void)
{
    NEDrvContext ctx;
    NEDrvMouseEvent evt;
    int16_t x, y;
    uint16_t buttons;
    TEST_BEGIN("mouse NULL arg checks");
    ASSERT_EQ(ne_drv_mouse_install(NULL), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_mouse_uninstall(NULL), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_mouse_push_event(NULL, WM_MOUSEMOVE, 0, 0, 0),
              NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_mouse_pop_event(NULL, &evt), NE_DRV_ERR_NULL);
    ne_drv_init(&ctx);
    ASSERT_EQ(ne_drv_mouse_pop_event(&ctx, NULL), NE_DRV_ERR_NULL);
    ASSERT_EQ(ne_drv_mouse_get_position(NULL, &x, &y, &buttons),
              NE_DRV_ERR_NULL);
    ne_drv_free(&ctx);
    TEST_PASS();
}

/* =========================================================================
 * Driver coexistence tests
 * ===================================================================== */

static void test_coexistence_all_drivers(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("coexistence passes with kbd+tmr+disp installed");
    ne_drv_init(&ctx);
    ne_drv_kbd_install(&ctx);
    ne_drv_tmr_install(&ctx);
    ne_drv_disp_install(&ctx);
    ASSERT_EQ(ne_drv_verify_coexistence(&ctx), NE_DRV_OK);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_coexistence_with_mouse(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("coexistence passes with all four drivers installed");
    ne_drv_init(&ctx);
    ne_drv_kbd_install(&ctx);
    ne_drv_tmr_install(&ctx);
    ne_drv_disp_install(&ctx);
    ne_drv_mouse_install(&ctx);
    ASSERT_EQ(ne_drv_verify_coexistence(&ctx), NE_DRV_OK);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_coexistence_missing_driver(void)
{
    NEDrvContext ctx;
    TEST_BEGIN("coexistence fails when keyboard not installed");
    ne_drv_init(&ctx);
    ne_drv_tmr_install(&ctx);
    ne_drv_disp_install(&ctx);
    ASSERT_NE(ne_drv_verify_coexistence(&ctx), NE_DRV_OK);
    ne_drv_free(&ctx);
    TEST_PASS();
}

static void test_coexistence_null(void)
{
    TEST_BEGIN("coexistence with NULL returns ERR_NULL");
    ASSERT_EQ(ne_drv_verify_coexistence(NULL), NE_DRV_ERR_NULL);
    TEST_PASS();
}

/* =========================================================================
 * Error string test
 * ===================================================================== */

static void test_drv_strerror(void)
{
    TEST_BEGIN("strerror returns non-NULL for all known codes");
    ASSERT_NOT_NULL(ne_drv_strerror(NE_DRV_OK));
    ASSERT_NOT_NULL(ne_drv_strerror(NE_DRV_ERR_NULL));
    ASSERT_NOT_NULL(ne_drv_strerror(NE_DRV_ERR_INIT));
    ASSERT_NOT_NULL(ne_drv_strerror(NE_DRV_ERR_FULL));
    ASSERT_NOT_NULL(ne_drv_strerror(NE_DRV_ERR_NOT_FOUND));
    ASSERT_NOT_NULL(ne_drv_strerror(NE_DRV_ERR_BAD_ID));
    ASSERT_NOT_NULL(ne_drv_strerror(-99));
    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== WinDOS Device Driver Tests (Phase 4) ===\n");

    /* --- Context tests --- */
    printf("\n--- Context tests ---\n");
    test_drv_init_free();
    test_drv_init_null();
    test_drv_free_null_safe();

    /* --- Keyboard driver tests --- */
    printf("\n--- Keyboard driver tests ---\n");
    test_kbd_install_uninstall();
    test_kbd_scancode_translation();
    test_kbd_push_pop_keydown();
    test_kbd_push_pop_keyup();
    test_kbd_queue_full();
    test_kbd_pop_empty();
    test_kbd_multiple_keys();
    test_kbd_function_keys();
    test_kbd_arrow_keys();
    test_kbd_null_args();

    /* --- Timer driver tests --- */
    printf("\n--- Timer driver tests ---\n");
    test_tmr_install_uninstall();
    test_tmr_tick_count();
    test_tmr_set_kill();
    test_tmr_kill_not_found();
    test_tmr_kill_bad_id();
    test_tmr_set_zero_interval();
    test_tmr_expiry_check();
    test_tmr_multiple_timers();
    test_tmr_table_full();
    test_tmr_null_args();

    /* --- Display driver tests --- */
    printf("\n--- Display driver tests ---\n");
    test_disp_install_uninstall();
    test_disp_clear();
    test_disp_putchar_getchar();
    test_disp_putchar_out_of_range();
    test_disp_cursor();
    test_disp_cursor_out_of_range();
    test_disp_write_string();
    test_disp_null_args();

    /* --- Mouse driver tests --- */
    printf("\n--- Mouse driver tests ---\n");
    test_mouse_install_uninstall();
    test_mouse_push_pop();
    test_mouse_button_events();
    test_mouse_position();
    test_mouse_queue_full();
    test_mouse_pop_empty();
    test_mouse_null_args();

    /* --- Driver coexistence tests --- */
    printf("\n--- Driver coexistence tests ---\n");
    test_coexistence_all_drivers();
    test_coexistence_with_mouse();
    test_coexistence_missing_driver();
    test_coexistence_null();

    /* --- Error strings --- */
    printf("\n--- Error strings ---\n");
    test_drv_strerror();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

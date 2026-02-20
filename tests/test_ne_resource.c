/*
 * test_ne_resource.c - Tests for Phase 5: Resource Manager
 *
 * Verifies:
 *   - Resource table initialisation and teardown
 *   - Resource add, find by ID, find by string name
 *   - Resource enumeration (EnumResourceTypes, EnumResourceNames)
 *   - Accelerator table loading and translation
 *   - Dialog template loading, DialogBox and CreateDialog stubs
 *   - Menu resource loading and TrackPopupMenu stub
 *   - Error handling and error strings
 */

#include "../src/ne_resource.h"

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
 * Resource table tests
 * ===================================================================== */

static void test_res_table_init_free(void)
{
    NEResTable tbl;
    TEST_BEGIN("init sets initialized=1, free sets it to 0");
    ASSERT_EQ(ne_res_table_init(&tbl, 32), NE_RES_OK);
    ASSERT_EQ(tbl.initialized, 1);
    ASSERT_EQ(tbl.capacity, 32);
    ASSERT_EQ(tbl.count, 0);
    ne_res_table_free(&tbl);
    ASSERT_EQ(tbl.initialized, 0);
    TEST_PASS();
}

static void test_res_table_init_null(void)
{
    TEST_BEGIN("init with NULL returns ERR_NULL");
    ASSERT_EQ(ne_res_table_init(NULL, 16), NE_RES_ERR_NULL);
    TEST_PASS();
}

static void test_res_table_init_zero_cap(void)
{
    NEResTable tbl;
    TEST_BEGIN("init with zero capacity returns ERR_NULL");
    ASSERT_EQ(ne_res_table_init(&tbl, 0), NE_RES_ERR_NULL);
    TEST_PASS();
}

static void test_res_table_free_null_safe(void)
{
    TEST_BEGIN("free(NULL) does not crash");
    ne_res_table_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * Resource add / find tests
 * ===================================================================== */

static const uint8_t g_dummy_data[] = { 0x01, 0x02, 0x03, 0x04 };

static void test_res_add_by_id(void)
{
    NEResTable  tbl;
    NEResHandle h;
    NEResEntry *e;
    TEST_BEGIN("add by ordinal ID; find_by_id returns entry");
    ne_res_table_init(&tbl, 16);
    h = ne_res_add(&tbl, RT_BITMAP, 101, NULL, g_dummy_data, 4);
    ASSERT_NE(h, NE_RES_HANDLE_INVALID);
    ASSERT_EQ(tbl.count, 1);
    e = ne_res_find_by_id(&tbl, RT_BITMAP, 101);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->type_id, RT_BITMAP);
    ASSERT_EQ(e->name_id, 101);
    ASSERT_EQ(e->raw_size, 4u);
    ne_res_table_free(&tbl);
    TEST_PASS();
}

static void test_res_add_by_name(void)
{
    NEResTable  tbl;
    NEResHandle h;
    NEResEntry *e;
    TEST_BEGIN("add by string name; find_by_name returns entry");
    ne_res_table_init(&tbl, 16);
    h = ne_res_add(&tbl, RT_RCDATA, 0, "MYDATA", g_dummy_data, 4);
    ASSERT_NE(h, NE_RES_HANDLE_INVALID);
    e = ne_res_find_by_name(&tbl, RT_RCDATA, "MYDATA");
    ASSERT_NOT_NULL(e);
    ASSERT_STR_EQ(e->name_str, "MYDATA");
    ne_res_table_free(&tbl);
    TEST_PASS();
}

static void test_res_add_multiple_types(void)
{
    NEResTable  tbl;
    NEResHandle h1, h2, h3;
    TEST_BEGIN("add resources of different types; all findable");
    ne_res_table_init(&tbl, 16);
    h1 = ne_res_add(&tbl, RT_MENU,        1, NULL, g_dummy_data, 4);
    h2 = ne_res_add(&tbl, RT_DIALOG,      2, NULL, g_dummy_data, 4);
    h3 = ne_res_add(&tbl, RT_ACCELERATOR, 3, NULL, g_dummy_data, 4);
    ASSERT_NE(h1, NE_RES_HANDLE_INVALID);
    ASSERT_NE(h2, NE_RES_HANDLE_INVALID);
    ASSERT_NE(h3, NE_RES_HANDLE_INVALID);
    ASSERT_EQ(tbl.count, 3);
    ASSERT_NOT_NULL(ne_res_find_by_id(&tbl, RT_MENU, 1));
    ASSERT_NOT_NULL(ne_res_find_by_id(&tbl, RT_DIALOG, 2));
    ASSERT_NOT_NULL(ne_res_find_by_id(&tbl, RT_ACCELERATOR, 3));
    ne_res_table_free(&tbl);
    TEST_PASS();
}

static void test_res_find_not_found(void)
{
    NEResTable tbl;
    TEST_BEGIN("find_by_id returns NULL for unknown resource");
    ne_res_table_init(&tbl, 16);
    ASSERT_NULL(ne_res_find_by_id(&tbl, RT_BITMAP, 999));
    ASSERT_NULL(ne_res_find_by_name(&tbl, RT_BITMAP, "NONE"));
    ne_res_table_free(&tbl);
    TEST_PASS();
}

static void test_res_table_full(void)
{
    NEResTable  tbl;
    NEResHandle h;
    int         i;
    TEST_BEGIN("add beyond capacity returns HANDLE_INVALID");
    ne_res_table_init(&tbl, 4);
    for (i = 0; i < 4; i++) {
        h = ne_res_add(&tbl, RT_RCDATA, (uint16_t)(i+1), NULL,
                       g_dummy_data, 4);
        ASSERT_NE(h, NE_RES_HANDLE_INVALID);
    }
    h = ne_res_add(&tbl, RT_RCDATA, 5, NULL, g_dummy_data, 4);
    ASSERT_EQ(h, NE_RES_HANDLE_INVALID);
    ne_res_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * Resource enumeration tests
 * ===================================================================== */

/* Callback state for enum_types tests */
static uint16_t g_enum_types[16];
static int      g_enum_type_count;

static int enum_types_cb(uint16_t type_id, void *user_data)
{
    (void)user_data;
    if (g_enum_type_count < 16)
        g_enum_types[g_enum_type_count++] = type_id;
    return 1; /* continue */
}

static void test_res_enum_types_basic(void)
{
    NEResTable tbl;
    int        n;
    TEST_BEGIN("enum_types returns one callback per unique type");
    ne_res_table_init(&tbl, 16);
    ne_res_add(&tbl, RT_MENU, 1, NULL, g_dummy_data, 4);
    ne_res_add(&tbl, RT_MENU, 2, NULL, g_dummy_data, 4); /* same type */
    ne_res_add(&tbl, RT_DIALOG, 1, NULL, g_dummy_data, 4);
    ne_res_add(&tbl, RT_ACCELERATOR, 1, NULL, g_dummy_data, 4);

    g_enum_type_count = 0;
    n = ne_res_enum_types(&tbl, enum_types_cb, NULL);
    ASSERT_EQ(n, 3); /* RT_MENU, RT_DIALOG, RT_ACCELERATOR */
    ASSERT_EQ(g_enum_type_count, 3);
    ne_res_table_free(&tbl);
    TEST_PASS();
}

static void test_res_enum_types_empty(void)
{
    NEResTable tbl;
    int        n;
    TEST_BEGIN("enum_types on empty table returns 0");
    ne_res_table_init(&tbl, 8);
    g_enum_type_count = 0;
    n = ne_res_enum_types(&tbl, enum_types_cb, NULL);
    ASSERT_EQ(n, 0);
    ne_res_table_free(&tbl);
    TEST_PASS();
}

static int stop_cb(uint16_t type_id, void *ud)
{
    (void)type_id; (void)ud;
    return 0; /* stop */
}

static void test_res_enum_types_early_stop(void)
{
    NEResTable tbl;
    int        n;

    TEST_BEGIN("enum_types stops when callback returns 0");
    ne_res_table_init(&tbl, 16);
    ne_res_add(&tbl, RT_MENU,   1, NULL, g_dummy_data, 4);
    ne_res_add(&tbl, RT_DIALOG, 1, NULL, g_dummy_data, 4);
    n = ne_res_enum_types(&tbl, stop_cb, NULL);
    ASSERT_EQ(n, 1);
    ne_res_table_free(&tbl);
    TEST_PASS();
}

/* Callback state for enum_names tests */
static uint16_t g_enum_names[16];
static int      g_enum_name_count;

static int enum_names_cb(const NEResEntry *e, void *ud)
{
    (void)ud;
    if (g_enum_name_count < 16)
        g_enum_names[g_enum_name_count++] = e->name_id;
    return 1;
}

static void test_res_enum_names_basic(void)
{
    NEResTable tbl;
    int        n;
    TEST_BEGIN("enum_names returns all entries of given type");
    ne_res_table_init(&tbl, 16);
    ne_res_add(&tbl, RT_MENU,   1, NULL, g_dummy_data, 4);
    ne_res_add(&tbl, RT_MENU,   2, NULL, g_dummy_data, 4);
    ne_res_add(&tbl, RT_MENU,   3, NULL, g_dummy_data, 4);
    ne_res_add(&tbl, RT_DIALOG, 1, NULL, g_dummy_data, 4); /* different type */

    g_enum_name_count = 0;
    n = ne_res_enum_names(&tbl, RT_MENU, enum_names_cb, NULL);
    ASSERT_EQ(n, 3);
    ASSERT_EQ(g_enum_name_count, 3);
    ne_res_table_free(&tbl);
    TEST_PASS();
}

static void test_res_enum_names_no_match(void)
{
    NEResTable tbl;
    int        n;
    TEST_BEGIN("enum_names returns 0 when no entries match type");
    ne_res_table_init(&tbl, 8);
    ne_res_add(&tbl, RT_MENU, 1, NULL, g_dummy_data, 4);
    g_enum_name_count = 0;
    n = ne_res_enum_names(&tbl, RT_DIALOG, enum_names_cb, NULL);
    ASSERT_EQ(n, 0);
    ne_res_table_free(&tbl);
    TEST_PASS();
}

static void test_res_enum_null_args(void)
{
    NEResTable tbl;
    TEST_BEGIN("enum functions return ERR_NULL for NULL args");
    ASSERT_EQ(ne_res_enum_types(NULL, enum_types_cb, NULL), NE_RES_ERR_NULL);
    ne_res_table_init(&tbl, 8);
    ASSERT_EQ(ne_res_enum_types(&tbl, NULL, NULL), NE_RES_ERR_NULL);
    ASSERT_EQ(ne_res_enum_names(NULL, RT_MENU, enum_names_cb, NULL),
              NE_RES_ERR_NULL);
    ASSERT_EQ(ne_res_enum_names(&tbl, RT_MENU, NULL, NULL), NE_RES_ERR_NULL);
    ne_res_table_free(&tbl);
    TEST_PASS();
}

/* =========================================================================
 * Accelerator table tests
 * ===================================================================== */

/*
 * Accelerator binary data:
 *   Entry 0: fVirt=FCONTROL|FVIRTKEY, key=0x43 (VK_C), cmd=0x0100
 *   Entry 1: fVirt=FALT|FVIRTKEY|FLASTKEY, key=0x78 (VK_F9), cmd=0x0200
 *
 * Each entry: [fVirt][key_lo][key_hi][cmd_lo][cmd_hi]
 */
static const uint8_t g_accel_raw[] = {
    /* Entry 0: Ctrl+C -> cmd 0x0100 */
    FCONTROL | FVIRTKEY,
    0x43, 0x00,  /* key = 0x0043 (VK_C) */
    0x00, 0x01,  /* cmd = 0x0100 */
    /* Entry 1: Alt+F9 -> cmd 0x0200  (FLASTKEY) */
    FALT | FVIRTKEY | FLASTKEY,
    0x78, 0x00,  /* key = 0x0078 (VK_F9) */
    0x00, 0x02   /* cmd = 0x0200 */
};

static void test_accel_load(void)
{
    NEAccelTable tbl;
    TEST_BEGIN("load_accel parses two entries correctly");
    memset(&tbl, 0, sizeof(tbl));
    ASSERT_EQ(ne_res_load_accel(&tbl, g_accel_raw, sizeof(g_accel_raw)),
              NE_RES_OK);
    ASSERT_EQ(tbl.count, 2);
    ASSERT_EQ(tbl.entries[0].key, 0x0043u);
    ASSERT_EQ(tbl.entries[0].cmd, 0x0100u);
    ASSERT_EQ(tbl.entries[1].key, 0x0078u);
    ASSERT_EQ(tbl.entries[1].cmd, 0x0200u);
    ne_res_accel_free(&tbl);
    TEST_PASS();
}

static void test_accel_translate_ctrl_c(void)
{
    NEAccelTable tbl;
    uint16_t     cmd = 0;
    TEST_BEGIN("translate Ctrl+C matches entry and returns cmd");
    ne_res_load_accel(&tbl, g_accel_raw, sizeof(g_accel_raw));
    ASSERT_EQ(ne_res_accel_translate(&tbl,
                                     FCONTROL | FVIRTKEY, 0x0043, &cmd),
              NE_RES_OK);
    ASSERT_EQ(cmd, 0x0100u);
    ne_res_accel_free(&tbl);
    TEST_PASS();
}

static void test_accel_translate_alt_f9(void)
{
    NEAccelTable tbl;
    uint16_t     cmd = 0;
    TEST_BEGIN("translate Alt+F9 matches entry and returns cmd");
    ne_res_load_accel(&tbl, g_accel_raw, sizeof(g_accel_raw));
    ASSERT_EQ(ne_res_accel_translate(&tbl,
                                     FALT | FVIRTKEY, 0x0078, &cmd),
              NE_RES_OK);
    ASSERT_EQ(cmd, 0x0200u);
    ne_res_accel_free(&tbl);
    TEST_PASS();
}

static void test_accel_translate_no_match(void)
{
    NEAccelTable tbl;
    uint16_t     cmd = 0xFFFF;
    TEST_BEGIN("translate with no matching accelerator returns NOT_FOUND");
    ne_res_load_accel(&tbl, g_accel_raw, sizeof(g_accel_raw));
    ASSERT_EQ(ne_res_accel_translate(&tbl, FVIRTKEY, 0x0041 /* VK_A */, &cmd),
              NE_RES_ERR_NOT_FOUND);
    ASSERT_EQ(cmd, 0xFFFF); /* unchanged */
    ne_res_accel_free(&tbl);
    TEST_PASS();
}

static void test_accel_translate_modifier_mismatch(void)
{
    NEAccelTable tbl;
    uint16_t     cmd = 0;
    TEST_BEGIN("translate Ctrl+C with Shift modifier does not match");
    ne_res_load_accel(&tbl, g_accel_raw, sizeof(g_accel_raw));
    /* Ctrl+Shift+C should not match Ctrl+C */
    ASSERT_EQ(ne_res_accel_translate(&tbl,
                                     FCONTROL | FSHIFT | FVIRTKEY,
                                     0x0043, &cmd),
              NE_RES_ERR_NOT_FOUND);
    ne_res_accel_free(&tbl);
    TEST_PASS();
}

static void test_accel_load_too_small(void)
{
    NEAccelTable tbl;
    uint8_t      tiny[3] = { 0x01, 0x41, 0x00 };
    TEST_BEGIN("load_accel with < 5 bytes returns ERR_BAD_DATA");
    memset(&tbl, 0, sizeof(tbl));
    ASSERT_EQ(ne_res_load_accel(&tbl, tiny, sizeof(tiny)),
              NE_RES_ERR_BAD_DATA);
    TEST_PASS();
}

static void test_accel_null_args(void)
{
    NEAccelTable tbl;
    uint16_t     cmd;
    TEST_BEGIN("accelerator NULL arg checks");
    memset(&tbl, 0, sizeof(tbl));
    ASSERT_EQ(ne_res_load_accel(NULL, g_accel_raw, sizeof(g_accel_raw)),
              NE_RES_ERR_NULL);
    ASSERT_EQ(ne_res_load_accel(&tbl, NULL, 10), NE_RES_ERR_NULL);
    ASSERT_EQ(ne_res_accel_translate(NULL, 0, 0, &cmd), NE_RES_ERR_NULL);
    ne_res_load_accel(&tbl, g_accel_raw, sizeof(g_accel_raw));
    ASSERT_EQ(ne_res_accel_translate(&tbl, 0, 0, NULL), NE_RES_ERR_NULL);
    ne_res_accel_free(&tbl);
    TEST_PASS();
}

static void test_accel_free_null_safe(void)
{
    TEST_BEGIN("accel_free(NULL) does not crash");
    ne_res_accel_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * Dialog template tests
 * ===================================================================== */

/*
 * Build a minimal dialog template binary:
 *   style   (4 bytes LE): 0x00C80000
 *   count   (1 byte)    : 1
 *   x,y,cx,cy (8 bytes) : 10,20,200,100
 *   title   (string)    : "TestDlg\0"
 *   font    (string)    : "Arial\0"
 *   font_sz (2 bytes LE): 10
 *   item:
 *     style  (2 bytes LE): 0x0050
 *     x,y,cx,cy (8 bytes): 5,5,80,14
 *     id     (2 bytes LE): 101
 *     cls    (1 byte)    : 0x82 (edit)
 *     title  (string)    : "OK\0"
 */
static const uint8_t g_dlg_raw[] = {
    /* style */ 0x00, 0x00, 0xC8, 0x00,
    /* count */ 1,
    /* x  */    10, 0,
    /* y  */    20, 0,
    /* cx */    0xC8, 0,   /* 200 */
    /* cy */    100, 0,
    /* title */ 'T','e','s','t','D','l','g','\0',
    /* font  */ 'A','r','i','a','l','\0',
    /* fsize */ 10, 0,
    /* item style */ 0x50, 0x00,
    /* item x  */    5, 0,
    /* item y  */    5, 0,
    /* item cx */    80, 0,
    /* item cy */    14, 0,
    /* item id */    101, 0,
    /* item cls */   0x82,
    /* item title */ 'O','K','\0'
};

static void test_dlg_load_basic(void)
{
    NEDlgTemplate dlg;
    TEST_BEGIN("load_dialog parses header fields correctly");
    memset(&dlg, 0, sizeof(dlg));
    ASSERT_EQ(ne_res_load_dialog(&dlg, g_dlg_raw, sizeof(g_dlg_raw)),
              NE_RES_OK);
    ASSERT_EQ(dlg.x, 10);
    ASSERT_EQ(dlg.y, 20);
    ASSERT_EQ(dlg.cx, 200);
    ASSERT_EQ(dlg.cy, 100);
    ASSERT_STR_EQ(dlg.title, "TestDlg");
    ASSERT_STR_EQ(dlg.font_name, "Arial");
    ASSERT_EQ(dlg.font_size, 10u);
    ASSERT_EQ(dlg.item_count, 1u);
    TEST_PASS();
}

static void test_dlg_load_item(void)
{
    NEDlgTemplate dlg;
    TEST_BEGIN("load_dialog parses dialog item correctly");
    memset(&dlg, 0, sizeof(dlg));
    ne_res_load_dialog(&dlg, g_dlg_raw, sizeof(g_dlg_raw));
    ASSERT_EQ(dlg.items[0].x, 5);
    ASSERT_EQ(dlg.items[0].y, 5);
    ASSERT_EQ(dlg.items[0].cx, 80);
    ASSERT_EQ(dlg.items[0].cy, 14);
    ASSERT_EQ(dlg.items[0].id, 101u);
    ASSERT_EQ(dlg.items[0].cls, 0x82u);
    ASSERT_STR_EQ(dlg.items[0].title, "OK");
    TEST_PASS();
}

static void test_dlg_load_too_small(void)
{
    NEDlgTemplate dlg;
    uint8_t       tiny[4] = { 0 };
    TEST_BEGIN("load_dialog with < 13 bytes returns ERR_BAD_DATA");
    memset(&dlg, 0, sizeof(dlg));
    ASSERT_EQ(ne_res_load_dialog(&dlg, tiny, sizeof(tiny)),
              NE_RES_ERR_BAD_DATA);
    TEST_PASS();
}

static void test_dlg_null_args(void)
{
    NEDlgTemplate dlg;
    TEST_BEGIN("load_dialog NULL arg checks");
    ASSERT_EQ(ne_res_load_dialog(NULL, g_dlg_raw, sizeof(g_dlg_raw)),
              NE_RES_ERR_NULL);
    ASSERT_EQ(ne_res_load_dialog(&dlg, NULL, 20), NE_RES_ERR_NULL);
    TEST_PASS();
}

/* Simple dialog proc for stub tests */
static int g_dlg_initdlg_called = 0;
static uint32_t test_dlg_proc(uint16_t hwnd, uint16_t msg,
                               uint16_t wParam, uint32_t lParam)
{
    (void)hwnd; (void)wParam; (void)lParam;
    if (msg == 0x0110u) /* WM_INITDIALOG */
        g_dlg_initdlg_called++;
    return 1;
}

static void test_dlg_dialog_box(void)
{
    NEDlgTemplate dlg;
    int           ret;
    TEST_BEGIN("dialog_box calls dlg_proc with WM_INITDIALOG");
    memset(&dlg, 0, sizeof(dlg));
    ne_res_load_dialog(&dlg, g_dlg_raw, sizeof(g_dlg_raw));
    g_dlg_initdlg_called = 0;
    ret = ne_res_dialog_box(0, &dlg, test_dlg_proc);
    ASSERT_EQ(g_dlg_initdlg_called, 1);
    ASSERT_EQ(ret, 1);
    TEST_PASS();
}

static void test_dlg_create_dialog(void)
{
    NEDlgTemplate dlg;
    uint16_t      h;
    TEST_BEGIN("create_dialog returns non-zero handle");
    memset(&dlg, 0, sizeof(dlg));
    ne_res_load_dialog(&dlg, g_dlg_raw, sizeof(g_dlg_raw));
    g_dlg_initdlg_called = 0;
    h = ne_res_create_dialog(0, &dlg, test_dlg_proc);
    ASSERT_NE(h, 0);
    ASSERT_EQ(g_dlg_initdlg_called, 1);
    TEST_PASS();
}

static void test_dlg_dialog_box_null(void)
{
    NEDlgTemplate dlg;
    TEST_BEGIN("dialog_box/create_dialog with NULL args return error");
    ASSERT_EQ(ne_res_dialog_box(0, NULL, test_dlg_proc), -1);
    ASSERT_EQ(ne_res_dialog_box(0, &dlg, NULL), -1);
    ASSERT_EQ(ne_res_create_dialog(0, NULL, test_dlg_proc), 0);
    ASSERT_EQ(ne_res_create_dialog(0, &dlg, NULL), 0);
    TEST_PASS();
}

/* =========================================================================
 * Menu resource tests
 * ===================================================================== */

/*
 * Build a minimal menu binary:
 *   Item 0: flags=MF_STRING, id=101, text="File\0"
 *   Item 1: flags=MF_SEPARATOR, id=0, text="\0"
 *   Item 2: flags=MF_STRING|MF_END, id=9, text="Exit\0"
 */
static const uint8_t g_menu_raw[] = {
    /* Item 0 */
    MF_STRING & 0xFF, (MF_STRING >> 8) & 0xFF,
    101, 0,
    'F','i','l','e','\0',
    /* Item 1 */
    MF_SEPARATOR & 0xFF, (MF_SEPARATOR >> 8) & 0xFF,
    0, 0,
    '\0',
    /* Item 2 - MF_END */
    (MF_STRING | MF_END) & 0xFF, ((MF_STRING | MF_END) >> 8) & 0xFF,
    9, 0,
    'E','x','i','t','\0'
};

static void test_menu_load(void)
{
    NEMenu menu;
    TEST_BEGIN("load_menu parses three items correctly");
    memset(&menu, 0, sizeof(menu));
    ASSERT_EQ(ne_res_load_menu(&menu, g_menu_raw, sizeof(g_menu_raw)),
              NE_RES_OK);
    ASSERT_EQ(menu.count, 3u);
    ASSERT_EQ(menu.items[0].id, 101u);
    ASSERT_STR_EQ(menu.items[0].text, "File");
    ASSERT_EQ(menu.items[1].flags & MF_SEPARATOR, MF_SEPARATOR);
    ASSERT_EQ(menu.items[2].id, 9u);
    ASSERT_STR_EQ(menu.items[2].text, "Exit");
    ne_res_menu_free(&menu);
    TEST_PASS();
}

static void test_menu_load_too_small(void)
{
    NEMenu  menu;
    uint8_t tiny[2] = { 0x00, 0x00 };
    TEST_BEGIN("load_menu with < 4 bytes returns ERR_BAD_DATA");
    memset(&menu, 0, sizeof(menu));
    ASSERT_EQ(ne_res_load_menu(&menu, tiny, sizeof(tiny)),
              NE_RES_ERR_BAD_DATA);
    TEST_PASS();
}

/* Callback for TrackPopupMenu tests */
static int      g_popup_count;
static uint16_t g_popup_ids[8];

static void popup_cb(const NEMenuItem *item, void *ud)
{
    (void)ud;
    if (g_popup_count < 8)
        g_popup_ids[g_popup_count++] = item->id;
}

static void test_menu_track_popup(void)
{
    NEMenu menu;
    TEST_BEGIN("track_popup_menu calls callback for each item");
    memset(&menu, 0, sizeof(menu));
    ne_res_load_menu(&menu, g_menu_raw, sizeof(g_menu_raw));
    g_popup_count = 0;
    ASSERT_EQ(ne_res_track_popup_menu(&menu, 0, 0, 0, popup_cb, NULL), 0);
    ASSERT_EQ(g_popup_count, 3);
    ASSERT_EQ(g_popup_ids[0], 101u);
    ASSERT_EQ(g_popup_ids[2], 9u);
    ne_res_menu_free(&menu);
    TEST_PASS();
}

static void test_menu_track_popup_no_callback(void)
{
    NEMenu menu;
    TEST_BEGIN("track_popup_menu with NULL callback does not crash");
    memset(&menu, 0, sizeof(menu));
    ne_res_load_menu(&menu, g_menu_raw, sizeof(g_menu_raw));
    ASSERT_EQ(ne_res_track_popup_menu(&menu, 0, 0, 0, NULL, NULL), 0);
    ne_res_menu_free(&menu);
    TEST_PASS();
}

static void test_menu_null_args(void)
{
    NEMenu menu;
    TEST_BEGIN("menu NULL arg checks");
    ASSERT_EQ(ne_res_load_menu(NULL, g_menu_raw, sizeof(g_menu_raw)),
              NE_RES_ERR_NULL);
    ASSERT_EQ(ne_res_load_menu(&menu, NULL, 10), NE_RES_ERR_NULL);
    ASSERT_EQ(ne_res_track_popup_menu(NULL, 0, 0, 0, NULL, NULL),
              NE_RES_ERR_NULL);
    TEST_PASS();
}

static void test_menu_free_null_safe(void)
{
    TEST_BEGIN("menu_free(NULL) does not crash");
    ne_res_menu_free(NULL);
    TEST_PASS();
}

/* =========================================================================
 * Error string test
 * ===================================================================== */

static void test_res_strerror(void)
{
    TEST_BEGIN("strerror returns non-NULL for all known codes");
    ASSERT_NOT_NULL(ne_res_strerror(NE_RES_OK));
    ASSERT_NOT_NULL(ne_res_strerror(NE_RES_ERR_NULL));
    ASSERT_NOT_NULL(ne_res_strerror(NE_RES_ERR_ALLOC));
    ASSERT_NOT_NULL(ne_res_strerror(NE_RES_ERR_FULL));
    ASSERT_NOT_NULL(ne_res_strerror(NE_RES_ERR_NOT_FOUND));
    ASSERT_NOT_NULL(ne_res_strerror(NE_RES_ERR_BAD_TYPE));
    ASSERT_NOT_NULL(ne_res_strerror(NE_RES_ERR_BAD_DATA));
    ASSERT_NOT_NULL(ne_res_strerror(NE_RES_ERR_BAD_HANDLE));
    ASSERT_NOT_NULL(ne_res_strerror(NE_RES_ERR_DUP));
    ASSERT_NOT_NULL(ne_res_strerror(-99));
    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== WinDOS Resource Manager Tests (Phase 5) ===\n");

    printf("\n--- Resource table tests ---\n");
    test_res_table_init_free();
    test_res_table_init_null();
    test_res_table_init_zero_cap();
    test_res_table_free_null_safe();

    printf("\n--- Resource add/find tests ---\n");
    test_res_add_by_id();
    test_res_add_by_name();
    test_res_add_multiple_types();
    test_res_find_not_found();
    test_res_table_full();

    printf("\n--- Resource enumeration tests ---\n");
    test_res_enum_types_basic();
    test_res_enum_types_empty();
    test_res_enum_types_early_stop();
    test_res_enum_names_basic();
    test_res_enum_names_no_match();
    test_res_enum_null_args();

    printf("\n--- Accelerator table tests ---\n");
    test_accel_load();
    test_accel_translate_ctrl_c();
    test_accel_translate_alt_f9();
    test_accel_translate_no_match();
    test_accel_translate_modifier_mismatch();
    test_accel_load_too_small();
    test_accel_null_args();
    test_accel_free_null_safe();

    printf("\n--- Dialog template tests ---\n");
    test_dlg_load_basic();
    test_dlg_load_item();
    test_dlg_load_too_small();
    test_dlg_null_args();
    test_dlg_dialog_box();
    test_dlg_create_dialog();
    test_dlg_dialog_box_null();

    printf("\n--- Menu resource tests ---\n");
    test_menu_load();
    test_menu_load_too_small();
    test_menu_track_popup();
    test_menu_track_popup_no_callback();
    test_menu_null_args();
    test_menu_free_null_safe();

    printf("\n--- Error strings ---\n");
    test_res_strerror();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

/*
 * ne_resource.h - Phase 5 Dynamic Segment and Resource Management:
 *                 Resource Manager
 *
 * Implements the resource-level capabilities required by larger
 * Windows 3.1 applications:
 *
 *   1. Full resource enumeration:
 *        ne_res_enum_types()  – iterate over all unique type IDs.
 *        ne_res_enum_names()  – iterate over all name IDs for a type.
 *
 *   2. Accelerator table loading and translation:
 *        ne_res_load_accel()     – load a NEAccelTable from raw data.
 *        ne_res_accel_translate()– check whether a key event matches an
 *                                  accelerator and return the command ID.
 *
 *   3. Dialog template loading and stub entry points:
 *        ne_res_load_dialog()    – populate a NEDlgTemplate from raw data.
 *        ne_res_dialog_box()     – DialogBox stub (host: runs the wnd proc).
 *        ne_res_create_dialog()  – CreateDialog stub (host: returns handle).
 *
 *   4. Menu resource loading and stub entry points:
 *        ne_res_load_menu()      – populate a NEMenu from raw data.
 *        ne_res_track_popup_menu()– TrackPopupMenu stub.
 *
 * Resource entries are stored in a flat NEResTable.  Each entry
 * identifies its resource by a (type_id, name_id) pair; string names are
 * supported through a fixed-length name_str field.
 *
 * Raw resource data is expected as a caller-supplied byte buffer; the
 * load functions parse the buffer and allocate their own storage for the
 * parsed structures.
 *
 * Reference: Microsoft Windows 3.1 SDK – Resource Functions;
 *            Microsoft "New Executable" format specification.
 */

#ifndef NE_RESOURCE_H
#define NE_RESOURCE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_RES_OK               0
#define NE_RES_ERR_NULL        -1   /* NULL pointer argument               */
#define NE_RES_ERR_ALLOC       -2   /* memory allocation failure           */
#define NE_RES_ERR_FULL        -3   /* resource table at capacity          */
#define NE_RES_ERR_NOT_FOUND   -4   /* resource not found                  */
#define NE_RES_ERR_BAD_TYPE    -5   /* unrecognised resource type          */
#define NE_RES_ERR_BAD_DATA    -6   /* malformed resource data             */
#define NE_RES_ERR_BAD_HANDLE  -7   /* zero or otherwise invalid handle    */
#define NE_RES_ERR_DUP         -8   /* duplicate (type, name) entry        */

/* -------------------------------------------------------------------------
 * Windows 3.1 resource type identifiers (RT_* constants)
 * ---------------------------------------------------------------------- */
#define RT_CURSOR        1u
#define RT_BITMAP        2u
#define RT_ICON          3u
#define RT_MENU          4u
#define RT_DIALOG        5u
#define RT_STRING        6u
#define RT_FONTDIR       7u
#define RT_FONT          8u
#define RT_ACCELERATOR   9u
#define RT_RCDATA       10u
#define RT_GROUP_CURSOR 12u
#define RT_GROUP_ICON   14u

/* -------------------------------------------------------------------------
 * Accelerator virtual-flag bits (ACCEL.fVirt)
 * ---------------------------------------------------------------------- */
#define FVIRTKEY    0x01u  /* key is a virtual key code (VK_*)             */
#define FNOINVERT   0x02u  /* do not invert the top-level menu item        */
#define FSHIFT      0x04u  /* requires Shift                               */
#define FCONTROL    0x08u  /* requires Ctrl                                */
#define FALT        0x10u  /* requires Alt                                 */
#define FLASTKEY    0x80u  /* last entry in the accelerator table          */

/* -------------------------------------------------------------------------
 * Menu item flags (MENUITEM.flags)
 * ---------------------------------------------------------------------- */
#define MF_STRING       0x0000u  /* text menu item                         */
#define MF_SEPARATOR    0x0800u  /* horizontal dividing line               */
#define MF_POPUP        0x0010u  /* item opens a sub-menu                  */
#define MF_END          0x0080u  /* last item in the menu or sub-menu      */
#define MF_GRAYED       0x0001u  /* item is greyed and disabled            */
#define MF_DISABLED     0x0002u  /* item is disabled but not greyed        */
#define MF_CHECKED      0x0008u  /* item has a check mark                  */

/* -------------------------------------------------------------------------
 * Configuration constants
 * ---------------------------------------------------------------------- */
#define NE_RES_TABLE_CAP      128u  /* max entries in the resource table   */
#define NE_RES_NAME_MAX        64u  /* max resource name length incl. NUL  */
#define NE_RES_ACCEL_CAP       64u  /* max entries per accelerator table   */
#define NE_RES_DLG_ITEM_CAP    32u  /* max controls per dialog template    */
#define NE_RES_MENU_ITEM_CAP   64u  /* max items per menu                  */
#define NE_RES_FONT_NAME_MAX   32u  /* max dialog font name length         */

/* -------------------------------------------------------------------------
 * Resource handle type
 *
 * A non-zero uint16_t that identifies one entry in the resource table.
 * NE_RES_HANDLE_INVALID (0) is the null sentinel.
 * ---------------------------------------------------------------------- */
typedef uint16_t NEResHandle;

#define NE_RES_HANDLE_INVALID ((NEResHandle)0)

/* -------------------------------------------------------------------------
 * Resource table entry  (internal)
 * ---------------------------------------------------------------------- */
typedef struct {
    NEResHandle handle;                 /* 1-based handle (0 = free slot)   */
    uint16_t    type_id;                /* RT_* resource type               */
    uint16_t    name_id;                /* ordinal name (0 = string name)   */
    char        name_str[NE_RES_NAME_MAX]; /* string name (if name_id==0)  */
    const uint8_t *raw_data;            /* pointer to raw resource bytes    */
    uint32_t    raw_size;               /* byte count of raw_data           */
} NEResEntry;

/* -------------------------------------------------------------------------
 * Resource table
 *
 * Initialise with ne_res_table_init(); release with ne_res_table_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    NEResEntry *entries;   /* heap-allocated array [0..capacity-1]          */
    uint16_t    capacity;  /* total slots                                   */
    uint16_t    count;     /* number of active entries                      */
    uint16_t    next_handle; /* next handle value to assign (starts at 1)   */
    int         initialized;
} NEResTable;

/* -------------------------------------------------------------------------
 * Accelerator entry – mirrors the Windows ACCEL structure
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t  fVirt;    /* FVIRTKEY | FNOINVERT | FSHIFT | FCONTROL | FALT  */
    uint16_t key;      /* virtual key code (if FVIRTKEY) or ASCII char      */
    uint16_t cmd;      /* command identifier sent to the window procedure   */
} NEAccelEntry;

/* -------------------------------------------------------------------------
 * Accelerator table
 *
 * Loaded with ne_res_load_accel(); released with ne_res_accel_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    NEAccelEntry *entries;  /* heap-allocated array                         */
    uint16_t      count;    /* number of valid entries                      */
    uint16_t      capacity; /* allocated slots                              */
} NEAccelTable;

/* -------------------------------------------------------------------------
 * Dialog template item – one control in a dialog box
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t style;                   /* WS_* / control-class style flags   */
    int16_t  x;                       /* X position (dialog units)          */
    int16_t  y;                       /* Y position (dialog units)          */
    int16_t  cx;                      /* width  (dialog units)              */
    int16_t  cy;                      /* height (dialog units)              */
    uint16_t id;                      /* control identifier                 */
    uint16_t cls;                     /* predefined control class code      */
    char     title[NE_RES_NAME_MAX];  /* control text                       */
} NEDlgItem;

/* -------------------------------------------------------------------------
 * Dialog template – mirrors a simplified DLGTEMPLATE
 *
 * Loaded with ne_res_load_dialog(); released with ne_res_dialog_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    uint32_t style;                        /* WS_* dialog style flags       */
    int16_t  x;                            /* X position                    */
    int16_t  y;                            /* Y position                    */
    int16_t  cx;                           /* width                         */
    int16_t  cy;                           /* height                        */
    char     title[NE_RES_NAME_MAX];       /* dialog caption text           */
    char     font_name[NE_RES_FONT_NAME_MAX]; /* font face name             */
    uint16_t font_size;                    /* font point size               */
    uint16_t item_count;                   /* number of controls            */
    NEDlgItem items[NE_RES_DLG_ITEM_CAP];  /* control array                */
} NEDlgTemplate;

/* -------------------------------------------------------------------------
 * Menu item – one entry in a flat or popup menu
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t flags;                    /* MF_* flags                        */
    uint16_t id;                       /* command identifier (0 for popups) */
    char     text[NE_RES_NAME_MAX];    /* item text                         */
} NEMenuItem;

/* -------------------------------------------------------------------------
 * Menu – flat array of menu items
 *
 * Loaded with ne_res_load_menu(); released with ne_res_menu_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    NEMenuItem *items;    /* heap-allocated array                            */
    uint16_t    count;    /* number of valid items                           */
    uint16_t    capacity; /* allocated slots                                 */
} NEMenu;

/* =========================================================================
 * Public API – resource table
 * ===================================================================== */

/*
 * ne_res_table_init - initialise *tbl with 'capacity' pre-allocated slots.
 *
 * Returns NE_RES_OK on success; call ne_res_table_free() when done.
 */
int ne_res_table_init(NEResTable *tbl, uint16_t capacity);

/*
 * ne_res_table_free - release all resources owned by *tbl.
 *
 * Frees the entry array.  Raw resource data pointers are NOT freed because
 * they are owned by the caller.  Safe to call on NULL.
 */
void ne_res_table_free(NEResTable *tbl);

/*
 * ne_res_add - register a resource in *tbl.
 *
 * 'type_id'  : RT_* type constant.
 * 'name_id'  : ordinal name; pass 0 and set 'name_str' for string names.
 * 'name_str' : string name (may be NULL when name_id != 0).
 * 'raw_data' : pointer to raw resource bytes (owned by caller).
 * 'raw_size' : byte count of raw_data.
 *
 * Returns the NEResHandle assigned to the new entry, or
 * NE_RES_HANDLE_INVALID on failure.
 */
NEResHandle ne_res_add(NEResTable    *tbl,
                       uint16_t       type_id,
                       uint16_t       name_id,
                       const char    *name_str,
                       const uint8_t *raw_data,
                       uint32_t       raw_size);

/*
 * ne_res_find_by_id - look up a resource by type and ordinal name.
 *
 * Returns a pointer to the matching entry or NULL if not found.
 */
NEResEntry *ne_res_find_by_id(NEResTable *tbl,
                               uint16_t    type_id,
                               uint16_t    name_id);

/*
 * ne_res_find_by_name - look up a resource by type and string name.
 *
 * Returns a pointer to the matching entry or NULL if not found.
 */
NEResEntry *ne_res_find_by_name(NEResTable *tbl,
                                 uint16_t    type_id,
                                 const char *name_str);

/* =========================================================================
 * Public API – resource enumeration
 * ===================================================================== */

/*
 * ne_res_enum_types - enumerate all unique type IDs in *tbl.
 *
 * Calls 'callback(type_id, user_data)' once for each unique type found.
 * If the callback returns 0, enumeration stops early.
 *
 * Returns the total number of unique types found, or a negative
 * NE_RES_ERR_* code on error.
 */
int ne_res_enum_types(NEResTable *tbl,
                      int (*callback)(uint16_t type_id, void *user_data),
                      void *user_data);

/*
 * ne_res_enum_names - enumerate all entries of a given type.
 *
 * Calls 'callback(entry, user_data)' once for each entry whose type_id
 * matches 'type_id'.  If the callback returns 0, enumeration stops early.
 *
 * Returns the total number of entries found, or a negative
 * NE_RES_ERR_* code on error.
 */
int ne_res_enum_names(NEResTable *tbl,
                      uint16_t    type_id,
                      int (*callback)(const NEResEntry *entry,
                                      void             *user_data),
                      void *user_data);

/* =========================================================================
 * Public API – accelerator table
 * ===================================================================== */

/*
 * ne_res_load_accel - parse raw ACCEL resource data into *tbl.
 *
 * 'raw'  : pointer to the raw accelerator resource bytes.
 * 'size' : byte count of 'raw' (must be >= 5 for at least one entry).
 *
 * The binary layout expected is a sequence of 5-byte ACCEL records:
 *   Byte 0     : fVirt flags
 *   Bytes 1-2  : key (little-endian uint16_t)
 *   Bytes 3-4  : cmd (little-endian uint16_t)
 * The last record has bit 7 (FLASTKEY) set in fVirt.
 *
 * Returns NE_RES_OK on success; call ne_res_accel_free() when done.
 */
int ne_res_load_accel(NEAccelTable  *tbl,
                      const uint8_t *raw,
                      uint32_t       size);

/*
 * ne_res_accel_free - release all resources owned by *tbl.
 *
 * Safe to call on a zeroed or partially-initialised table and on NULL.
 */
void ne_res_accel_free(NEAccelTable *tbl);

/*
 * ne_res_accel_translate - check whether a key event matches an accelerator.
 *
 * 'fVirt'      : modifier flags of the key event (FSHIFT, FCONTROL, FALT,
 *                and FVIRTKEY if the key value is a virtual key code).
 * 'key'        : key value (virtual key code or ASCII character).
 * 'out_cmd'    : receives the command ID when a match is found.
 *
 * Returns NE_RES_OK and sets *out_cmd if a matching accelerator is found.
 * Returns NE_RES_ERR_NOT_FOUND if no accelerator matches.
 * Returns NE_RES_ERR_NULL on invalid arguments.
 */
int ne_res_accel_translate(const NEAccelTable *tbl,
                           uint8_t             fVirt,
                           uint16_t            key,
                           uint16_t           *out_cmd);

/* =========================================================================
 * Public API – dialog template
 * ===================================================================== */

/*
 * ne_res_load_dialog - parse raw DLGTEMPLATE resource data into *dlg.
 *
 * 'raw'  : pointer to the raw dialog resource bytes.
 * 'size' : byte count of 'raw'.
 *
 * The expected binary layout is a minimal packed DLGTEMPLATE:
 *   Bytes 0-3  : style (uint32_t, little-endian)
 *   Byte  4    : item count (uint8_t)
 *   Bytes 5-6  : x  (int16_t)
 *   Bytes 7-8  : y  (int16_t)
 *   Bytes 9-10 : cx (int16_t)
 *   Bytes 11-12: cy (int16_t)
 *   Variable   : title (NUL-terminated string)
 *   Variable   : font name (NUL-terminated string)
 *   2 bytes    : font size (uint16_t)
 *   Per item   : 13 bytes + title string (see ne_res_load_dialog_item)
 *
 * Returns NE_RES_OK on success.  *dlg is populated with parsed data.
 * No heap allocation is performed; all data is copied into fixed fields.
 *
 * Returns NE_RES_ERR_BAD_DATA if the buffer is too small or malformed.
 */
int ne_res_load_dialog(NEDlgTemplate *dlg,
                       const uint8_t *raw,
                       uint32_t       size);

/*
 * ne_res_dialog_box - DialogBox stub.
 *
 * Calls dlg_proc(hwnd_parent, WM_INITDIALOG, 0, 0) and returns the result.
 * On a real Windows 3.1 system this would create a modal dialog, run a
 * message loop, and return the value passed to EndDialog().
 *
 * 'hwnd_parent' : handle of the owner window (may be 0).
 * 'dlg'         : previously loaded dialog template.
 * 'dlg_proc'    : dialog procedure callback.
 *
 * Returns the value returned by dlg_proc for WM_INITDIALOG, or -1 on
 * error (NULL pointers).
 */
typedef uint32_t (*NEDlgProc)(uint16_t hwnd, uint16_t msg,
                               uint16_t wParam, uint32_t lParam);

int ne_res_dialog_box(uint16_t            hwnd_parent,
                      const NEDlgTemplate *dlg,
                      NEDlgProc            dlg_proc);

/*
 * ne_res_create_dialog - CreateDialog stub.
 *
 * Creates a modeless dialog by calling dlg_proc(hwnd_parent,
 * WM_INITDIALOG, 0, 0).  Returns a non-zero pseudo-handle (1) on success
 * or 0 on failure.
 */
uint16_t ne_res_create_dialog(uint16_t            hwnd_parent,
                               const NEDlgTemplate *dlg,
                               NEDlgProc            dlg_proc);

/* =========================================================================
 * Public API – menu resource
 * ===================================================================== */

/*
 * ne_res_load_menu - parse raw MENU resource data into *menu.
 *
 * 'raw'  : pointer to the raw menu resource bytes.
 * 'size' : byte count of 'raw'.
 *
 * Expected binary layout: a sequence of variable-length menu item records.
 * Each record consists of:
 *   2 bytes : flags (uint16_t, little-endian; MF_* constants)
 *   2 bytes : id    (uint16_t; 0 for popup items)
 *   Variable: text  (NUL-terminated string; empty string for separators)
 * Parsing stops when an item with MF_END set in flags is encountered.
 *
 * Returns NE_RES_OK on success; call ne_res_menu_free() when done.
 */
int ne_res_load_menu(NEMenu        *menu,
                     const uint8_t *raw,
                     uint32_t       size);

/*
 * ne_res_menu_free - release all resources owned by *menu.
 *
 * Safe to call on a zeroed or partially-initialised menu and on NULL.
 */
void ne_res_menu_free(NEMenu *menu);

/*
 * ne_res_track_popup_menu - TrackPopupMenu stub.
 *
 * On a real system this displays a floating popup menu and returns the
 * command ID chosen by the user.  This stub calls item_callback for each
 * item in *menu (in order) and returns 0.
 *
 * 'menu'          : previously loaded menu.
 * 'x', 'y'        : screen coordinates (ignored on host).
 * 'hwnd'          : owner window (ignored on host).
 * 'item_callback' : called once per item; receives the item pointer and
 *                   user_data.  May be NULL.
 *
 * Returns 0 (no item selected) in the host stub.
 */
int ne_res_track_popup_menu(const NEMenu *menu,
                             int16_t       x,
                             int16_t       y,
                             uint16_t      hwnd,
                             void (*item_callback)(const NEMenuItem *item,
                                                   void             *ud),
                             void         *user_data);

/* =========================================================================
 * Public API – error reporting
 * ===================================================================== */

/*
 * ne_res_strerror - return a static string describing error code 'err'.
 */
const char *ne_res_strerror(int err);

#endif /* NE_RESOURCE_H */

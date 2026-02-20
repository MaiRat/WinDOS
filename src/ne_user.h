/*
 * ne_user.h - USER.EXE subsystem: windowing, message queue, and dispatch
 *
 * Implements Phase 3 of the WinDOS kernel-replacement roadmap:
 *   - Message queue with get/peek/post primitives.
 *   - Window class registration (RegisterClass equivalent).
 *   - Window creation, destruction, and visibility management.
 *   - Message dispatch to window procedures (SendMessage / DispatchMessage).
 *   - Default window procedure (DefWindowProc equivalent).
 *
 * The USER subsystem sits above the kernel layer (ne_kernel / ne_mem /
 * ne_task) and provides the core event-driven programming model used by
 * Windows 3.1 applications.  Window handles (HWND) are 1-based uint16_t
 * indices into the window table; 0 (NE_USER_HWND_INVALID) is the null
 * sentinel, matching the NULL-handle convention of the Windows 3.1 API.
 *
 * Message queuing uses a fixed-size circular buffer per context.  When
 * WM_QUIT is posted, ne_user_get_message() returns 0 to signal the
 * application's message loop to exit, exactly as the Win16 GetMessage
 * contract specifies.
 *
 * Reference: Microsoft Windows 3.1 SDK – Window Management Functions.
 */

#ifndef NE_USER_H
#define NE_USER_H

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_USER_OK               0
#define NE_USER_ERR_NULL        -1   /* NULL pointer argument               */
#define NE_USER_ERR_INIT        -2   /* context not initialised             */
#define NE_USER_ERR_FULL        -3   /* table or queue at capacity          */
#define NE_USER_ERR_NOT_FOUND   -4   /* class or window not found           */
#define NE_USER_ERR_BAD_HANDLE  -5   /* zero or otherwise invalid handle    */

/* -------------------------------------------------------------------------
 * Window styles  (Windows 3.1 compatible subset)
 * ---------------------------------------------------------------------- */
#define WS_OVERLAPPED   0x00000000u  /* overlapped (top-level) window       */
#define WS_VISIBLE      0x10000000u  /* initially visible                   */
#define WS_CHILD        0x40000000u  /* child window                        */
#define WS_DISABLED     0x08000000u  /* initially disabled                  */
#define WS_POPUP        0x80000000u  /* popup window                        */

/* -------------------------------------------------------------------------
 * Window show commands  (ShowWindow nCmdShow values)
 * ---------------------------------------------------------------------- */
#define SW_HIDE          0           /* hide the window                     */
#define SW_SHOW          5           /* activate and display the window     */

/* -------------------------------------------------------------------------
 * Message identifiers  (Windows 3.1 compatible subset)
 * ---------------------------------------------------------------------- */
#define WM_CREATE       0x0001u      /* sent after window creation          */
#define WM_DESTROY      0x0002u      /* sent before window destruction      */
#define WM_PAINT        0x000Fu      /* paint request                       */
#define WM_QUIT         0x0012u      /* exit message loop                   */
#define WM_USER         0x0400u      /* first application-defined message   */
#define WM_SETFOCUS     0x0007u      /* window gaining input focus          */
#define WM_KILLFOCUS    0x0008u      /* window losing input focus           */
#define WM_ENABLE       0x000Au      /* window enabled/disabled             */
#define WM_SETTEXT      0x000Cu      /* set window text                     */
#define WM_GETTEXT      0x000Du      /* get window text                     */
#define WM_GETTEXTLENGTH 0x000Eu     /* get window text length              */
#define WM_TIMER        0x0113u      /* timer expiration                    */
#define WM_COMMAND      0x0111u      /* menu / control notification         */

/* -------------------------------------------------------------------------
 * MessageBox styles
 * ---------------------------------------------------------------------- */
#define MB_OK               0x0000u
#define MB_OKCANCEL         0x0001u
#define MB_YESNOCANCEL      0x0003u
#define MB_YESNO            0x0004u

/* -------------------------------------------------------------------------
 * MessageBox return values
 * ---------------------------------------------------------------------- */
#define IDOK                1
#define IDCANCEL            2
#define IDYES               6
#define IDNO                7

/* -------------------------------------------------------------------------
 * Configuration constants
 * ---------------------------------------------------------------------- */
#define NE_USER_MSG_QUEUE_CAP   64u  /* max messages in the circular queue  */
#define NE_USER_WNDCLASS_CAP    32u  /* max registered window classes       */
#define NE_USER_WND_CAP         64u  /* max simultaneous windows            */
#define NE_USER_CLASSNAME_MAX   64u  /* max class name length incl. NUL     */
#define NE_USER_WNDTEXT_MAX    128u  /* max window text length incl. NUL    */
#define NE_USER_CLIP_CAP      4096u  /* max clipboard data size             */
#define NE_USER_MENU_CAP        32u  /* max menu entries per menu           */
#define NE_USER_MENU_TABLE_CAP  16u  /* max simultaneously allocated menus  */
#define NE_USER_KEY_STATE_CAP  256u  /* key state table size                */

/* -------------------------------------------------------------------------
 * Window handle type
 *
 * A non-zero uint16_t value that identifies an active window.
 * NE_USER_HWND_INVALID (0) is the null sentinel.
 * ---------------------------------------------------------------------- */
typedef uint16_t NEUserHWND;

#define NE_USER_HWND_INVALID ((NEUserHWND)0)

/* -------------------------------------------------------------------------
 * Message structure
 *
 * Mirrors the Windows 3.1 MSG structure.  Passed through the message queue
 * and delivered to window procedures via ne_user_dispatch_message().
 * ---------------------------------------------------------------------- */
typedef struct {
    NEUserHWND hwnd;             /* target window handle                    */
    uint16_t   message;          /* WM_* message identifier                 */
    uint16_t   wParam;           /* message-specific first parameter        */
    uint32_t   lParam;           /* message-specific second parameter       */
} NEUserMsg;

/* -------------------------------------------------------------------------
 * Window procedure callback type
 *
 * Every registered window class supplies a procedure matching this
 * signature.  The return value is message-specific; callers that do not
 * handle a message should forward it to ne_user_def_window_proc().
 * ---------------------------------------------------------------------- */
typedef uint32_t (*NEUserWndProc)(uint16_t hwnd,
                                  uint16_t msg,
                                  uint16_t wParam,
                                  uint32_t lParam);

/* -------------------------------------------------------------------------
 * Window class descriptor
 *
 * Populated by ne_user_register_class().  The 'registered' flag is non-zero
 * for active entries; zero marks a free slot.
 * ---------------------------------------------------------------------- */
typedef struct {
    char          class_name[NE_USER_CLASSNAME_MAX]; /* NUL-terminated name */
    NEUserWndProc wnd_proc;      /* class window procedure                  */
    uint32_t      style;         /* class style flags                       */
    int           registered;    /* non-zero if slot is active              */
} NEUserWndClass;

/* -------------------------------------------------------------------------
 * Rectangle structure
 *
 * Axis-aligned bounding rectangle, matching the Windows 3.1 RECT layout.
 * ---------------------------------------------------------------------- */
typedef struct {
    int16_t left;
    int16_t top;
    int16_t right;
    int16_t bottom;
} NEUserRect;

/* -------------------------------------------------------------------------
 * Window descriptor
 *
 * One entry per live window.  The 'active' flag is non-zero for entries
 * that represent a created window; zero marks a free slot.
 * ---------------------------------------------------------------------- */
typedef struct {
    NEUserHWND hwnd;             /* unique 1-based handle                   */
    char       class_name[NE_USER_CLASSNAME_MAX]; /* owning class name     */
    NEUserHWND parent;           /* parent window handle (0 = top-level)    */
    uint32_t   style;            /* WS_* style flags                       */
    int        visible;          /* non-zero if currently visible           */
    int        needs_paint;      /* non-zero if WM_PAINT is pending        */
    char       window_text[NE_USER_WNDTEXT_MAX]; /* window title/text      */
    int16_t    x;                /* window X position                       */
    int16_t    y;                /* window Y position                       */
    int16_t    width;            /* window width                            */
    int16_t    height;           /* window height                           */
    int        enabled;          /* non-zero if window is enabled           */
    NEUserRect invalid_rect;     /* invalidated region                      */
    int        has_invalid;      /* non-zero if invalid_rect is set         */
    uint16_t   menu_handle;      /* attached menu handle (0 = none)         */
    int        active;           /* non-zero if slot is in use              */
} NEUserWindow;

/* -------------------------------------------------------------------------
 * Message queue (fixed-size circular buffer)
 *
 * Messages are enqueued at 'tail' and dequeued from 'head'.  When 'count'
 * reaches NE_USER_MSG_QUEUE_CAP the queue is full and ne_user_post_message
 * returns NE_USER_ERR_FULL.
 * ---------------------------------------------------------------------- */
typedef struct {
    NEUserMsg msgs[NE_USER_MSG_QUEUE_CAP]; /* circular message buffer      */
    uint16_t  head;              /* index of the oldest message             */
    uint16_t  tail;              /* index of the next free slot             */
    uint16_t  count;             /* number of messages currently enqueued   */
} NEUserMsgQueue;

/* -------------------------------------------------------------------------
 * Menu item descriptor
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t id;                   /* menu item command ID                  */
    char     text[NE_USER_CLASSNAME_MAX]; /* menu item text                 */
    int      active;               /* non-zero if slot is in use            */
} NEUserMenuItem;

/* -------------------------------------------------------------------------
 * Menu descriptor
 * ---------------------------------------------------------------------- */
typedef struct {
    NEUserMenuItem items[NE_USER_MENU_CAP]; /* menu items                   */
    uint16_t       item_count;     /* number of active items                */
    uint16_t       handle;         /* non-zero menu handle                  */
    int            active;         /* non-zero if this menu is allocated    */
} NEUserMenu;

/* -------------------------------------------------------------------------
 * Clipboard state
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t  data[NE_USER_CLIP_CAP]; /* clipboard data buffer              */
    uint16_t size;                 /* current data size in bytes            */
    uint16_t format;               /* clipboard data format                */
    NEUserHWND owner;              /* window that opened the clipboard     */
    int      open;                 /* non-zero if clipboard is open         */
} NEUserClipboard;

/* -------------------------------------------------------------------------
 * Caret state
 * ---------------------------------------------------------------------- */
typedef struct {
    NEUserHWND owner;              /* window that owns the caret            */
    int16_t    x;                  /* caret X position                      */
    int16_t    y;                  /* caret Y position                      */
    uint16_t   width;              /* caret width                           */
    uint16_t   height;             /* caret height                          */
    int        visible;            /* non-zero if caret is visible          */
    int        created;            /* non-zero if caret has been created    */
} NEUserCaret;

/* -------------------------------------------------------------------------
 * USER subsystem context
 *
 * Owns the class table, window table, and message queue.  Initialise with
 * ne_user_init(); release with ne_user_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    NEUserWndClass classes[NE_USER_WNDCLASS_CAP]; /* registered classes     */
    uint16_t       class_count;  /* number of registered classes            */

    NEUserWindow   windows[NE_USER_WND_CAP];      /* window table          */
    uint16_t       wnd_count;    /* number of active windows                */
    NEUserHWND     next_hwnd;    /* next handle value to assign (starts 1)  */

    NEUserMsgQueue queue;        /* application message queue               */

    int            quit_posted;  /* non-zero after PostQuitMessage          */
    int            quit_code;    /* exit code passed to PostQuitMessage     */

    NEUserHWND     capture_hwnd;  /* window with mouse capture              */
    NEUserHWND     focus_hwnd;    /* window with input focus                */

    NEUserMenu     menus[NE_USER_MENU_TABLE_CAP]; /* menu table            */
    uint16_t       menu_count;   /* number of active menus                  */
    uint16_t       next_menu;    /* next menu handle to assign              */

    NEUserClipboard clipboard;   /* clipboard state                         */
    NEUserCaret     caret;       /* caret state                             */

    uint8_t        key_state[NE_USER_KEY_STATE_CAP]; /* key state table     */

    int            initialized;  /* non-zero after successful init          */
} NEUserContext;

/* =========================================================================
 * Public API – initialisation and teardown
 * ===================================================================== */

/*
 * ne_user_init - initialise the USER subsystem context *ctx.
 *
 * Zeroes all internal tables and sets the initial handle counter.
 * Must be called before any other ne_user_* function on *ctx.
 * Returns NE_USER_OK on success or NE_USER_ERR_NULL.
 */
int ne_user_init(NEUserContext *ctx);

/*
 * ne_user_free - release all resources held by *ctx.
 *
 * Resets the context to its uninitialised state.  Safe to call on a zeroed
 * or partially-initialised context and on NULL.
 */
void ne_user_free(NEUserContext *ctx);

/* =========================================================================
 * Public API – window class registration
 * ===================================================================== */

/*
 * ne_user_register_class - register a new window class.
 *
 * 'class_name' : NUL-terminated name (max NE_USER_CLASSNAME_MAX - 1 chars).
 * 'wnd_proc'   : window procedure callback for the class.
 * 'style'      : class style flags (reserved; pass 0).
 *
 * Returns NE_USER_OK on success, NE_USER_ERR_FULL if the class table is at
 * capacity, or NE_USER_ERR_NULL if any required pointer is NULL.
 */
int ne_user_register_class(NEUserContext *ctx,
                           const char    *class_name,
                           NEUserWndProc  wnd_proc,
                           uint32_t       style);

/* =========================================================================
 * Public API – window management
 * ===================================================================== */

/*
 * ne_user_create_window - create a new window of the given class.
 *
 * 'class_name' : name of a previously registered window class.
 * 'parent'     : handle of the parent window, or NE_USER_HWND_INVALID for
 *                a top-level window.
 * 'style'      : WS_* style flags.
 *
 * Returns a non-zero NEUserHWND on success or NE_USER_HWND_INVALID on
 * failure (class not found, window table full, or NULL context).
 */
NEUserHWND ne_user_create_window(NEUserContext *ctx,
                                 const char    *class_name,
                                 NEUserHWND     parent,
                                 uint32_t       style);

/*
 * ne_user_destroy_window - destroy the window identified by 'hwnd'.
 *
 * Sends WM_DESTROY to the window procedure, marks the slot as free, and
 * decrements the active window count.
 *
 * Returns NE_USER_OK on success, NE_USER_ERR_BAD_HANDLE if hwnd is
 * NE_USER_HWND_INVALID, or NE_USER_ERR_NOT_FOUND if the handle is not
 * in the window table.
 */
int ne_user_destroy_window(NEUserContext *ctx, NEUserHWND hwnd);

/*
 * ne_user_show_window - set the visibility state of a window.
 *
 * 'cmd_show' : SW_SHOW to make the window visible, SW_HIDE to hide it.
 *
 * Returns NE_USER_OK on success or a negative NE_USER_ERR_* code.
 */
int ne_user_show_window(NEUserContext *ctx, NEUserHWND hwnd, int cmd_show);

/*
 * ne_user_update_window - trigger a synchronous WM_PAINT if the window's
 * needs_paint flag is set.
 *
 * After dispatching WM_PAINT the flag is cleared.  If the flag is already
 * clear this function is a no-op and returns NE_USER_OK.
 *
 * Returns NE_USER_OK on success or a negative NE_USER_ERR_* code.
 */
int ne_user_update_window(NEUserContext *ctx, NEUserHWND hwnd);

/* =========================================================================
 * Public API – message loop
 * ===================================================================== */

/*
 * ne_user_get_message - retrieve the next message from the queue.
 *
 * Blocks conceptually until a message is available (in the cooperative
 * model this means the caller should loop).  Copies the message into *msg.
 *
 * Returns a positive value for normal messages or 0 when a WM_QUIT
 * message is retrieved, signalling the application to exit its message
 * loop.  Returns a negative NE_USER_ERR_* code on error.
 */
int ne_user_get_message(NEUserContext *ctx, NEUserMsg *msg);

/*
 * ne_user_peek_message - check the queue without blocking.
 *
 * If 'remove' is non-zero the message is dequeued; otherwise it remains
 * in the queue.  Copies the message into *msg if one is available.
 *
 * Returns 1 if a message was available, 0 if the queue is empty, or a
 * negative NE_USER_ERR_* code on error.
 */
int ne_user_peek_message(NEUserContext *ctx, NEUserMsg *msg, int remove);

/*
 * ne_user_translate_message - translate virtual-key messages.
 *
 * Stub implementation for Phase 3; always returns 0.
 */
int ne_user_translate_message(NEUserContext *ctx, const NEUserMsg *msg);

/*
 * ne_user_dispatch_message - dispatch a message to its target window proc.
 *
 * Looks up the window identified by msg->hwnd, finds the associated class,
 * and calls the class window procedure with the message parameters.
 *
 * Returns the value returned by the window procedure.
 */
uint32_t ne_user_dispatch_message(NEUserContext *ctx, const NEUserMsg *msg);

/* =========================================================================
 * Public API – message sending and posting
 * ===================================================================== */

/*
 * ne_user_send_message - synchronously send a message to a window proc.
 *
 * Bypasses the message queue and calls the window procedure directly.
 * Returns the value returned by the window procedure.
 */
uint32_t ne_user_send_message(NEUserContext *ctx,
                              NEUserHWND     hwnd,
                              uint16_t       msg,
                              uint16_t       wParam,
                              uint32_t       lParam);

/*
 * ne_user_post_message - place a message into the queue.
 *
 * The message is appended at the tail of the circular buffer and will be
 * retrieved by a subsequent ne_user_get_message() or ne_user_peek_message()
 * call.
 *
 * Returns NE_USER_OK on success or NE_USER_ERR_FULL if the queue is at
 * capacity.
 */
int ne_user_post_message(NEUserContext *ctx,
                         NEUserHWND     hwnd,
                         uint16_t       msg,
                         uint16_t       wParam,
                         uint32_t       lParam);

/*
 * ne_user_def_window_proc - default processing for unhandled messages.
 *
 * Window procedures should call this for any message they do not handle.
 * Returns 0 for most messages.
 */
uint32_t ne_user_def_window_proc(NEUserContext *ctx,
                                 NEUserHWND     hwnd,
                                 uint16_t       msg,
                                 uint16_t       wParam,
                                 uint32_t       lParam);

/*
 * ne_user_post_quit_message - post a WM_QUIT message with the given exit
 * code.
 *
 * Sets the context's quit_posted flag and enqueues WM_QUIT so that
 * ne_user_get_message() will return 0 on its next call.
 *
 * Returns NE_USER_OK on success or a negative NE_USER_ERR_* code.
 */
int ne_user_post_quit_message(NEUserContext *ctx, int exit_code);

/* =========================================================================
 * Public API – error reporting
 * ===================================================================== */

/*
 * ne_user_strerror - return a static string describing error code 'err'.
 */
const char *ne_user_strerror(int err);

/* =========================================================================
 * Public API – Phase D: USER.EXE Expansion
 * ===================================================================== */

/* --- MessageBox --- */
int ne_user_message_box(NEUserContext *ctx, NEUserHWND hwnd,
                        const char *text, const char *caption,
                        uint16_t type);

/* --- Dialog APIs --- */
int ne_user_dialog_box(NEUserContext *ctx, const char *templ_name,
                       NEUserHWND owner, NEUserWndProc dlg_proc);
NEUserHWND ne_user_create_dialog(NEUserContext *ctx, const char *templ_name,
                                 NEUserHWND owner, NEUserWndProc dlg_proc);
int ne_user_end_dialog(NEUserContext *ctx, NEUserHWND dlg, int result);

/* --- Mouse capture --- */
NEUserHWND ne_user_set_capture(NEUserContext *ctx, NEUserHWND hwnd);
int ne_user_release_capture(NEUserContext *ctx);

/* --- Window rectangles --- */
int ne_user_get_client_rect(NEUserContext *ctx, NEUserHWND hwnd,
                            NEUserRect *rect);
int ne_user_get_window_rect(NEUserContext *ctx, NEUserHWND hwnd,
                            NEUserRect *rect);

/* --- Window position and size --- */
int ne_user_move_window(NEUserContext *ctx, NEUserHWND hwnd,
                        int16_t x, int16_t y,
                        int16_t width, int16_t height, int repaint);
int ne_user_set_window_pos(NEUserContext *ctx, NEUserHWND hwnd,
                           NEUserHWND insert_after,
                           int16_t x, int16_t y,
                           int16_t cx, int16_t cy, uint16_t flags);

/* --- Window text --- */
int ne_user_set_window_text(NEUserContext *ctx, NEUserHWND hwnd,
                            const char *text);
int ne_user_get_window_text(NEUserContext *ctx, NEUserHWND hwnd,
                            char *buf, int max_count);

/* --- Window state --- */
int ne_user_enable_window(NEUserContext *ctx, NEUserHWND hwnd, int enable);
int ne_user_is_window_enabled(NEUserContext *ctx, NEUserHWND hwnd);
int ne_user_is_window_visible(NEUserContext *ctx, NEUserHWND hwnd);

/* --- Input focus --- */
NEUserHWND ne_user_set_focus(NEUserContext *ctx, NEUserHWND hwnd);
NEUserHWND ne_user_get_focus(NEUserContext *ctx);

/* --- Paint invalidation --- */
int ne_user_invalidate_rect(NEUserContext *ctx, NEUserHWND hwnd,
                            const NEUserRect *rect, int erase);
int ne_user_validate_rect(NEUserContext *ctx, NEUserHWND hwnd,
                          const NEUserRect *rect);

/* --- Scrolling --- */
int ne_user_scroll_window(NEUserContext *ctx, NEUserHWND hwnd,
                          int16_t dx, int16_t dy,
                          const NEUserRect *scroll_rect,
                          const NEUserRect *clip_rect);

/* --- Timer wiring (delegates to ne_driver) --- */
uint16_t ne_user_set_timer(NEUserContext *ctx, NEUserHWND hwnd,
                           uint16_t id_event, uint32_t elapse);
int ne_user_kill_timer(NEUserContext *ctx, NEUserHWND hwnd,
                       uint16_t id_event);

/* --- Clipboard APIs --- */
int ne_user_open_clipboard(NEUserContext *ctx, NEUserHWND hwnd);
int ne_user_close_clipboard(NEUserContext *ctx);
int ne_user_set_clipboard_data(NEUserContext *ctx, uint16_t format,
                               const void *data, uint16_t size);
int ne_user_get_clipboard_data(NEUserContext *ctx, uint16_t format,
                               void *buf, uint16_t buf_size,
                               uint16_t *out_size);

/* --- Caret APIs --- */
int ne_user_create_caret(NEUserContext *ctx, NEUserHWND hwnd,
                         uint16_t width, uint16_t height);
int ne_user_set_caret_pos(NEUserContext *ctx, int16_t x, int16_t y);
int ne_user_show_caret(NEUserContext *ctx, NEUserHWND hwnd);
int ne_user_hide_caret(NEUserContext *ctx, NEUserHWND hwnd);
int ne_user_destroy_caret(NEUserContext *ctx);

/* --- Input state --- */
int16_t ne_user_get_key_state(NEUserContext *ctx, int vk);
int16_t ne_user_get_async_key_state(NEUserContext *ctx, int vk);

/* --- Menu APIs --- */
uint16_t ne_user_create_menu(NEUserContext *ctx);
int ne_user_set_menu(NEUserContext *ctx, NEUserHWND hwnd, uint16_t hmenu);
int ne_user_append_menu(NEUserContext *ctx, uint16_t hmenu,
                        uint16_t flags, uint16_t id, const char *text);
uint16_t ne_user_get_menu(NEUserContext *ctx, NEUserHWND hwnd);
int ne_user_destroy_menu(NEUserContext *ctx, uint16_t hmenu);

#endif /* NE_USER_H */

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

/* -------------------------------------------------------------------------
 * Configuration constants
 * ---------------------------------------------------------------------- */
#define NE_USER_MSG_QUEUE_CAP   64u  /* max messages in the circular queue  */
#define NE_USER_WNDCLASS_CAP    32u  /* max registered window classes       */
#define NE_USER_WND_CAP         64u  /* max simultaneous windows            */
#define NE_USER_CLASSNAME_MAX   64u  /* max class name length incl. NUL     */

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

#endif /* NE_USER_H */

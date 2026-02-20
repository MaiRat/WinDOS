/*
 * ne_user.c - Phase 3 USER.EXE subsystem implementation
 *
 * Implements windowing, message queue, and dispatch primitives for
 * the WinDOS kernel-replacement layer.  Window classes are registered
 * into a fixed-size table; windows are tracked in a parallel table
 * with 1-based handles.  The message queue is a circular buffer.
 *
 * Reference: Microsoft Windows 3.1 SDK – Window Management Functions.
 */

#include "ne_user.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * Static helpers – table lookups
 * ---------------------------------------------------------------------- */

static NEUserWindow *user_find_window_by_hwnd(NEUserContext *ctx,
                                              NEUserHWND     hwnd)
{
    uint16_t i;

    for (i = 0; i < NE_USER_WND_CAP; i++) {
        if (ctx->windows[i].active && ctx->windows[i].hwnd == hwnd)
            return &ctx->windows[i];
    }

    return NULL;
}

static NEUserWndClass *user_find_class_by_name(NEUserContext *ctx,
                                               const char    *name)
{
    uint16_t i;

    for (i = 0; i < NE_USER_WNDCLASS_CAP; i++) {
        if (ctx->classes[i].registered &&
            strncmp(ctx->classes[i].class_name, name,
                    NE_USER_CLASSNAME_MAX) == 0)
            return &ctx->classes[i];
    }

    return NULL;
}

/* =========================================================================
 * ne_user_init / ne_user_free
 * ===================================================================== */

int ne_user_init(NEUserContext *ctx)
{
    if (!ctx)
        return NE_USER_ERR_NULL;

    memset(ctx, 0, sizeof(*ctx));

    ctx->next_hwnd   = 1;
    ctx->initialized = 1;
    return NE_USER_OK;
}

void ne_user_free(NEUserContext *ctx)
{
    if (!ctx)
        return;

    memset(ctx, 0, sizeof(*ctx));
}

/* =========================================================================
 * Window class registration
 * ===================================================================== */

int ne_user_register_class(NEUserContext *ctx,
                           const char    *class_name,
                           NEUserWndProc  wnd_proc,
                           uint32_t       style)
{
    uint16_t i;

    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (!class_name || class_name[0] == '\0' || !wnd_proc)
        return NE_USER_ERR_NULL;

    /* Idempotent: if already registered, succeed silently */
    if (user_find_class_by_name(ctx, class_name))
        return NE_USER_OK;

    if (ctx->class_count >= NE_USER_WNDCLASS_CAP)
        return NE_USER_ERR_FULL;

    /* Find first free slot */
    for (i = 0; i < NE_USER_WNDCLASS_CAP; i++) {
        if (!ctx->classes[i].registered) {
            strncpy(ctx->classes[i].class_name, class_name,
                    NE_USER_CLASSNAME_MAX - 1u);
            ctx->classes[i].class_name[NE_USER_CLASSNAME_MAX - 1u] = '\0';
            ctx->classes[i].wnd_proc   = wnd_proc;
            ctx->classes[i].style      = style;
            ctx->classes[i].registered = 1;
            ctx->class_count++;
            return NE_USER_OK;
        }
    }

    return NE_USER_ERR_FULL;
}

/* =========================================================================
 * Window management
 * ===================================================================== */

NEUserHWND ne_user_create_window(NEUserContext *ctx,
                                 const char    *class_name,
                                 NEUserHWND     parent,
                                 uint32_t       style)
{
    NEUserWndClass *cls;
    NEUserHWND      hwnd;
    uint16_t        i;

    if (!ctx || !class_name)
        return NE_USER_HWND_INVALID;
    if (!ctx->initialized)
        return NE_USER_HWND_INVALID;

    cls = user_find_class_by_name(ctx, class_name);
    if (!cls)
        return NE_USER_HWND_INVALID;

    if (ctx->wnd_count >= NE_USER_WND_CAP)
        return NE_USER_HWND_INVALID;

    /* Find first free slot */
    for (i = 0; i < NE_USER_WND_CAP; i++) {
        if (!ctx->windows[i].active)
            break;
    }
    if (i >= NE_USER_WND_CAP)
        return NE_USER_HWND_INVALID;

    hwnd = ctx->next_hwnd++;

    strncpy(ctx->windows[i].class_name, class_name,
            NE_USER_CLASSNAME_MAX - 1u);
    ctx->windows[i].class_name[NE_USER_CLASSNAME_MAX - 1u] = '\0';
    ctx->windows[i].hwnd   = hwnd;
    ctx->windows[i].parent = parent;
    ctx->windows[i].style  = style;
    ctx->windows[i].active = 1;

    if (style & WS_VISIBLE) {
        ctx->windows[i].visible     = 1;
        ctx->windows[i].needs_paint = 1;
    }

    ctx->wnd_count++;

    /* Send WM_CREATE */
    cls->wnd_proc(hwnd, WM_CREATE, 0, 0);

    return hwnd;
}

int ne_user_destroy_window(NEUserContext *ctx, NEUserHWND hwnd)
{
    NEUserWindow   *wnd;
    NEUserWndClass *cls;

    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (hwnd == NE_USER_HWND_INVALID)
        return NE_USER_ERR_BAD_HANDLE;

    wnd = user_find_window_by_hwnd(ctx, hwnd);
    if (!wnd)
        return NE_USER_ERR_NOT_FOUND;

    cls = user_find_class_by_name(ctx, wnd->class_name);
    if (cls)
        cls->wnd_proc(hwnd, WM_DESTROY, 0, 0);

    memset(wnd, 0, sizeof(*wnd));
    ctx->wnd_count--;

    return NE_USER_OK;
}

/* -------------------------------------------------------------------------
 * Show / Update
 * ---------------------------------------------------------------------- */

int ne_user_show_window(NEUserContext *ctx, NEUserHWND hwnd, int cmd_show)
{
    NEUserWindow *wnd;

    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (hwnd == NE_USER_HWND_INVALID)
        return NE_USER_ERR_BAD_HANDLE;

    wnd = user_find_window_by_hwnd(ctx, hwnd);
    if (!wnd)
        return NE_USER_ERR_NOT_FOUND;

    if (cmd_show == SW_SHOW) {
        wnd->visible     = 1;
        wnd->needs_paint = 1;
    } else if (cmd_show == SW_HIDE) {
        wnd->visible = 0;
    }

    return NE_USER_OK;
}

int ne_user_update_window(NEUserContext *ctx, NEUserHWND hwnd)
{
    NEUserWindow   *wnd;
    NEUserWndClass *cls;

    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (hwnd == NE_USER_HWND_INVALID)
        return NE_USER_ERR_BAD_HANDLE;

    wnd = user_find_window_by_hwnd(ctx, hwnd);
    if (!wnd)
        return NE_USER_ERR_NOT_FOUND;

    if (wnd->needs_paint && wnd->visible) {
        cls = user_find_class_by_name(ctx, wnd->class_name);
        if (cls)
            cls->wnd_proc(hwnd, WM_PAINT, 0, 0);
        wnd->needs_paint = 0;
    }

    return NE_USER_OK;
}

/* =========================================================================
 * Message loop
 * ===================================================================== */

int ne_user_get_message(NEUserContext *ctx, NEUserMsg *msg)
{
    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (!msg)
        return NE_USER_ERR_NULL;

    if (ctx->queue.count == 0) {
        memset(msg, 0, sizeof(*msg));
        if (ctx->quit_posted) {
            msg->message = WM_QUIT;
            msg->wParam  = (uint16_t)ctx->quit_code;
        }
        return 0;
    }

    /* Dequeue from head */
    *msg = ctx->queue.msgs[ctx->queue.head];
    ctx->queue.head = (uint16_t)((ctx->queue.head + 1u) %
                                  NE_USER_MSG_QUEUE_CAP);
    ctx->queue.count--;

    if (msg->message == WM_QUIT)
        return 0;

    return 1;
}

int ne_user_peek_message(NEUserContext *ctx, NEUserMsg *msg, int remove)
{
    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (!msg)
        return NE_USER_ERR_NULL;

    if (ctx->queue.count == 0)
        return 0;

    *msg = ctx->queue.msgs[ctx->queue.head];

    if (remove) {
        ctx->queue.head = (uint16_t)((ctx->queue.head + 1u) %
                                      NE_USER_MSG_QUEUE_CAP);
        ctx->queue.count--;
    }

    return 1;
}

int ne_user_translate_message(NEUserContext *ctx, const NEUserMsg *msg)
{
    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (!msg)
        return NE_USER_ERR_NULL;

    /* Stub: no translation required at this phase */
    return 0;
}

uint32_t ne_user_dispatch_message(NEUserContext *ctx, const NEUserMsg *msg)
{
    NEUserWindow   *wnd;
    NEUserWndClass *cls;

    if (!ctx || !ctx->initialized || !msg)
        return 0;

    wnd = user_find_window_by_hwnd(ctx, msg->hwnd);
    if (!wnd)
        return 0;

    cls = user_find_class_by_name(ctx, wnd->class_name);
    if (!cls)
        return 0;

    return cls->wnd_proc(msg->hwnd, msg->message, msg->wParam, msg->lParam);
}

/* =========================================================================
 * Message sending and posting
 * ===================================================================== */

uint32_t ne_user_send_message(NEUserContext *ctx,
                              NEUserHWND     hwnd,
                              uint16_t       msg,
                              uint16_t       wParam,
                              uint32_t       lParam)
{
    NEUserWindow   *wnd;
    NEUserWndClass *cls;

    if (!ctx || !ctx->initialized)
        return 0;

    wnd = user_find_window_by_hwnd(ctx, hwnd);
    if (!wnd)
        return 0;

    cls = user_find_class_by_name(ctx, wnd->class_name);
    if (!cls)
        return 0;

    return cls->wnd_proc(hwnd, msg, wParam, lParam);
}

int ne_user_post_message(NEUserContext *ctx,
                         NEUserHWND     hwnd,
                         uint16_t       msg,
                         uint16_t       wParam,
                         uint32_t       lParam)
{
    NEUserMsg *slot;

    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;

    if (ctx->queue.count >= NE_USER_MSG_QUEUE_CAP)
        return NE_USER_ERR_FULL;

    slot = &ctx->queue.msgs[ctx->queue.tail];
    slot->hwnd    = hwnd;
    slot->message = msg;
    slot->wParam  = wParam;
    slot->lParam  = lParam;

    ctx->queue.tail = (uint16_t)((ctx->queue.tail + 1u) %
                                  NE_USER_MSG_QUEUE_CAP);
    ctx->queue.count++;

    return NE_USER_OK;
}

/* =========================================================================
 * Default window procedure
 * ===================================================================== */

uint32_t ne_user_def_window_proc(NEUserContext *ctx,
                                 NEUserHWND     hwnd,
                                 uint16_t       msg,
                                 uint16_t       wParam,
                                 uint32_t       lParam)
{
    if (!ctx)
        return 0;
    if (!ctx->initialized)
        return 0;

    (void)hwnd;
    (void)msg;
    (void)wParam;
    (void)lParam;

    return 0;
}

/* =========================================================================
 * PostQuitMessage
 * ===================================================================== */

int ne_user_post_quit_message(NEUserContext *ctx, int exit_code)
{
    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;

    ctx->quit_posted = 1;
    ctx->quit_code   = exit_code;

    return ne_user_post_message(ctx, 0, WM_QUIT,
                                (uint16_t)exit_code, 0);
}

/* =========================================================================
 * ne_user_strerror
 * ===================================================================== */

const char *ne_user_strerror(int err)
{
    switch (err) {
    case NE_USER_OK:             return "success";
    case NE_USER_ERR_NULL:       return "NULL pointer argument";
    case NE_USER_ERR_INIT:       return "context not initialised";
    case NE_USER_ERR_FULL:       return "table or queue at capacity";
    case NE_USER_ERR_NOT_FOUND:  return "class or window not found";
    case NE_USER_ERR_BAD_HANDLE: return "invalid handle";
    default:                     return "unknown error";
    }
}

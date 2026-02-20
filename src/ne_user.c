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
    ctx->next_menu   = 1;
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
    ctx->windows[i].enabled = (style & WS_DISABLED) ? 0 : 1;
    ctx->windows[i].window_text[0] = '\0';
    ctx->windows[i].x      = 0;
    ctx->windows[i].y      = 0;
    ctx->windows[i].width  = 100;   /* default width */
    ctx->windows[i].height = 50;    /* default height */

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
 * Phase D – USER.EXE Expansion
 * ===================================================================== */

/* --- MessageBox --- */

int ne_user_message_box(NEUserContext *ctx, NEUserHWND hwnd,
                        const char *text, const char *caption,
                        uint16_t type)
{
    if (!ctx)
        return 0;
    if (!ctx->initialized)
        return 0;

    (void)hwnd;
    (void)text;
    (void)caption;

    switch (type & 0x000Fu) {
    case MB_OK:         return IDOK;
    case MB_OKCANCEL:   return IDOK;
    case MB_YESNOCANCEL: return IDYES;
    case MB_YESNO:      return IDYES;
    default:            return IDOK;
    }
}

/* --- Dialog APIs --- */

int ne_user_dialog_box(NEUserContext *ctx, const char *templ_name,
                       NEUserHWND owner, NEUserWndProc dlg_proc)
{
    NEUserHWND dlg;

    if (!ctx || !ctx->initialized)
        return -1;

    dlg = ne_user_create_dialog(ctx, templ_name, owner, dlg_proc);
    if (dlg == NE_USER_HWND_INVALID)
        return -1;

    /* In a full implementation, a modal message loop would run here.
     * For this stub we simply invoke the dialog procedure with
     * WM_CREATE and return 0. */
    ne_user_destroy_window(ctx, dlg);
    return 0;
}

NEUserHWND ne_user_create_dialog(NEUserContext *ctx, const char *templ_name,
                                 NEUserHWND owner, NEUserWndProc dlg_proc)
{
    NEUserHWND dlg;
    int rc;

    if (!ctx || !ctx->initialized || !dlg_proc)
        return NE_USER_HWND_INVALID;

    /* Register a temporary dialog class if not already present */
    rc = ne_user_register_class(ctx, templ_name ? templ_name : "NEDialog",
                                dlg_proc, 0);
    if (rc != NE_USER_OK && rc != NE_USER_ERR_FULL)
        return NE_USER_HWND_INVALID;

    dlg = ne_user_create_window(ctx,
                                templ_name ? templ_name : "NEDialog",
                                owner, WS_POPUP | WS_VISIBLE);
    return dlg;
}

int ne_user_end_dialog(NEUserContext *ctx, NEUserHWND dlg, int result)
{
    if (!ctx || !ctx->initialized)
        return NE_USER_ERR_NULL;

    (void)result;

    return ne_user_destroy_window(ctx, dlg);
}

/* --- Mouse capture --- */

NEUserHWND ne_user_set_capture(NEUserContext *ctx, NEUserHWND hwnd)
{
    NEUserHWND prev;

    if (!ctx || !ctx->initialized)
        return NE_USER_HWND_INVALID;

    prev = ctx->capture_hwnd;
    ctx->capture_hwnd = hwnd;
    return prev;
}

int ne_user_release_capture(NEUserContext *ctx)
{
    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;

    ctx->capture_hwnd = NE_USER_HWND_INVALID;
    return NE_USER_OK;
}

/* --- Window rectangles --- */

int ne_user_get_client_rect(NEUserContext *ctx, NEUserHWND hwnd,
                            NEUserRect *rect)
{
    NEUserWindow *wnd;

    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (!rect)
        return NE_USER_ERR_NULL;
    if (hwnd == NE_USER_HWND_INVALID)
        return NE_USER_ERR_BAD_HANDLE;

    wnd = user_find_window_by_hwnd(ctx, hwnd);
    if (!wnd)
        return NE_USER_ERR_NOT_FOUND;

    rect->left   = 0;
    rect->top    = 0;
    rect->right  = wnd->width;
    rect->bottom = wnd->height;
    return NE_USER_OK;
}

int ne_user_get_window_rect(NEUserContext *ctx, NEUserHWND hwnd,
                            NEUserRect *rect)
{
    NEUserWindow *wnd;

    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (!rect)
        return NE_USER_ERR_NULL;
    if (hwnd == NE_USER_HWND_INVALID)
        return NE_USER_ERR_BAD_HANDLE;

    wnd = user_find_window_by_hwnd(ctx, hwnd);
    if (!wnd)
        return NE_USER_ERR_NOT_FOUND;

    rect->left   = wnd->x;
    rect->top    = wnd->y;
    rect->right  = (int16_t)(wnd->x + wnd->width);
    rect->bottom = (int16_t)(wnd->y + wnd->height);
    return NE_USER_OK;
}

/* --- Window position and size --- */

int ne_user_move_window(NEUserContext *ctx, NEUserHWND hwnd,
                        int16_t x, int16_t y,
                        int16_t width, int16_t height, int repaint)
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

    wnd->x      = x;
    wnd->y      = y;
    wnd->width  = width;
    wnd->height = height;

    if (repaint && wnd->visible)
        wnd->needs_paint = 1;

    return NE_USER_OK;
}

int ne_user_set_window_pos(NEUserContext *ctx, NEUserHWND hwnd,
                           NEUserHWND insert_after,
                           int16_t x, int16_t y,
                           int16_t cx, int16_t cy, uint16_t flags)
{
    (void)insert_after;
    (void)flags;

    return ne_user_move_window(ctx, hwnd, x, y, cx, cy, 1);
}

/* --- Window text --- */

int ne_user_set_window_text(NEUserContext *ctx, NEUserHWND hwnd,
                            const char *text)
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

    if (text) {
        strncpy(wnd->window_text, text, NE_USER_WNDTEXT_MAX - 1u);
        wnd->window_text[NE_USER_WNDTEXT_MAX - 1u] = '\0';
    } else {
        wnd->window_text[0] = '\0';
    }

    return NE_USER_OK;
}

int ne_user_get_window_text(NEUserContext *ctx, NEUserHWND hwnd,
                            char *buf, int max_count)
{
    NEUserWindow *wnd;
    size_t len;

    if (!ctx)
        return 0;
    if (!ctx->initialized)
        return 0;
    if (!buf || max_count <= 0)
        return 0;
    if (hwnd == NE_USER_HWND_INVALID)
        return 0;

    wnd = user_find_window_by_hwnd(ctx, hwnd);
    if (!wnd) {
        buf[0] = '\0';
        return 0;
    }

    len = strlen(wnd->window_text);
    if (len >= (size_t)max_count)
        len = (size_t)(max_count - 1);
    memcpy(buf, wnd->window_text, len);
    buf[len] = '\0';
    return (int)len;
}

/* --- Window state --- */

int ne_user_enable_window(NEUserContext *ctx, NEUserHWND hwnd, int enable)
{
    NEUserWindow *wnd;
    int was_disabled;

    if (!ctx)
        return 0;
    if (!ctx->initialized)
        return 0;
    if (hwnd == NE_USER_HWND_INVALID)
        return 0;

    wnd = user_find_window_by_hwnd(ctx, hwnd);
    if (!wnd)
        return 0;

    was_disabled = !wnd->enabled;
    wnd->enabled = enable ? 1 : 0;
    return was_disabled;
}

int ne_user_is_window_enabled(NEUserContext *ctx, NEUserHWND hwnd)
{
    NEUserWindow *wnd;

    if (!ctx || !ctx->initialized)
        return 0;
    if (hwnd == NE_USER_HWND_INVALID)
        return 0;

    wnd = user_find_window_by_hwnd(ctx, hwnd);
    if (!wnd)
        return 0;

    return wnd->enabled;
}

int ne_user_is_window_visible(NEUserContext *ctx, NEUserHWND hwnd)
{
    NEUserWindow *wnd;

    if (!ctx || !ctx->initialized)
        return 0;
    if (hwnd == NE_USER_HWND_INVALID)
        return 0;

    wnd = user_find_window_by_hwnd(ctx, hwnd);
    if (!wnd)
        return 0;

    return wnd->visible;
}

/* --- Input focus --- */

NEUserHWND ne_user_set_focus(NEUserContext *ctx, NEUserHWND hwnd)
{
    NEUserHWND prev;
    NEUserWndClass *cls;
    NEUserWindow   *wnd;

    if (!ctx || !ctx->initialized)
        return NE_USER_HWND_INVALID;

    prev = ctx->focus_hwnd;

    /* Notify old focus window */
    if (prev != NE_USER_HWND_INVALID) {
        wnd = user_find_window_by_hwnd(ctx, prev);
        if (wnd) {
            cls = user_find_class_by_name(ctx, wnd->class_name);
            if (cls)
                cls->wnd_proc(prev, WM_KILLFOCUS, hwnd, 0);
        }
    }

    ctx->focus_hwnd = hwnd;

    /* Notify new focus window */
    if (hwnd != NE_USER_HWND_INVALID) {
        wnd = user_find_window_by_hwnd(ctx, hwnd);
        if (wnd) {
            cls = user_find_class_by_name(ctx, wnd->class_name);
            if (cls)
                cls->wnd_proc(hwnd, WM_SETFOCUS, prev, 0);
        }
    }

    return prev;
}

NEUserHWND ne_user_get_focus(NEUserContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return NE_USER_HWND_INVALID;

    return ctx->focus_hwnd;
}

/* --- Paint invalidation --- */

int ne_user_invalidate_rect(NEUserContext *ctx, NEUserHWND hwnd,
                            const NEUserRect *rect, int erase)
{
    NEUserWindow *wnd;

    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (hwnd == NE_USER_HWND_INVALID)
        return NE_USER_ERR_BAD_HANDLE;

    (void)erase;

    wnd = user_find_window_by_hwnd(ctx, hwnd);
    if (!wnd)
        return NE_USER_ERR_NOT_FOUND;

    wnd->needs_paint = 1;

    if (rect) {
        wnd->invalid_rect = *rect;
    } else {
        wnd->invalid_rect.left   = 0;
        wnd->invalid_rect.top    = 0;
        wnd->invalid_rect.right  = wnd->width;
        wnd->invalid_rect.bottom = wnd->height;
    }
    wnd->has_invalid = 1;

    return NE_USER_OK;
}

int ne_user_validate_rect(NEUserContext *ctx, NEUserHWND hwnd,
                          const NEUserRect *rect)
{
    NEUserWindow *wnd;

    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (hwnd == NE_USER_HWND_INVALID)
        return NE_USER_ERR_BAD_HANDLE;

    (void)rect;

    wnd = user_find_window_by_hwnd(ctx, hwnd);
    if (!wnd)
        return NE_USER_ERR_NOT_FOUND;

    wnd->needs_paint = 0;
    wnd->has_invalid = 0;
    memset(&wnd->invalid_rect, 0, sizeof(wnd->invalid_rect));

    return NE_USER_OK;
}

/* --- Scrolling --- */

int ne_user_scroll_window(NEUserContext *ctx, NEUserHWND hwnd,
                          int16_t dx, int16_t dy,
                          const NEUserRect *scroll_rect,
                          const NEUserRect *clip_rect)
{
    NEUserWindow *wnd;

    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (hwnd == NE_USER_HWND_INVALID)
        return NE_USER_ERR_BAD_HANDLE;

    (void)dx;
    (void)dy;
    (void)scroll_rect;
    (void)clip_rect;

    wnd = user_find_window_by_hwnd(ctx, hwnd);
    if (!wnd)
        return NE_USER_ERR_NOT_FOUND;

    /* Stub: mark window for repaint after scroll */
    wnd->needs_paint = 1;

    return NE_USER_OK;
}

/* --- Timer wiring --- */

uint16_t ne_user_set_timer(NEUserContext *ctx, NEUserHWND hwnd,
                           uint16_t id_event, uint32_t elapse)
{
    if (!ctx || !ctx->initialized)
        return 0;

    (void)hwnd;

    /* Return the id_event as the timer identifier.  In a full
     * implementation this would delegate to ne_drv_set_timer(). */
    if (elapse == 0)
        return 0;

    return id_event ? id_event : 1;
}

int ne_user_kill_timer(NEUserContext *ctx, NEUserHWND hwnd,
                       uint16_t id_event)
{
    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;

    (void)hwnd;
    (void)id_event;

    return NE_USER_OK;
}

/* --- Clipboard APIs --- */

int ne_user_open_clipboard(NEUserContext *ctx, NEUserHWND hwnd)
{
    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;

    if (ctx->clipboard.open)
        return NE_USER_ERR_FULL; /* already open */

    ctx->clipboard.open  = 1;
    ctx->clipboard.owner = hwnd;
    return NE_USER_OK;
}

int ne_user_close_clipboard(NEUserContext *ctx)
{
    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;

    ctx->clipboard.open  = 0;
    ctx->clipboard.owner = NE_USER_HWND_INVALID;
    return NE_USER_OK;
}

int ne_user_set_clipboard_data(NEUserContext *ctx, uint16_t format,
                               const void *data, uint16_t size)
{
    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (!ctx->clipboard.open)
        return NE_USER_ERR_INIT;
    if (!data || size == 0)
        return NE_USER_ERR_NULL;
    if (size > NE_USER_CLIP_CAP)
        return NE_USER_ERR_FULL;

    memcpy(ctx->clipboard.data, data, size);
    ctx->clipboard.size   = size;
    ctx->clipboard.format = format;
    return NE_USER_OK;
}

int ne_user_get_clipboard_data(NEUserContext *ctx, uint16_t format,
                               void *buf, uint16_t buf_size,
                               uint16_t *out_size)
{
    uint16_t copy_size;

    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (!buf)
        return NE_USER_ERR_NULL;

    if (ctx->clipboard.size == 0 || ctx->clipboard.format != format)
        return NE_USER_ERR_NOT_FOUND;

    copy_size = ctx->clipboard.size;
    if (copy_size > buf_size)
        copy_size = buf_size;
    memcpy(buf, ctx->clipboard.data, copy_size);
    if (out_size)
        *out_size = copy_size;
    return NE_USER_OK;
}

/* --- Caret APIs --- */

int ne_user_create_caret(NEUserContext *ctx, NEUserHWND hwnd,
                         uint16_t width, uint16_t height)
{
    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;

    ctx->caret.owner   = hwnd;
    ctx->caret.width   = width;
    ctx->caret.height  = height;
    ctx->caret.x       = 0;
    ctx->caret.y       = 0;
    ctx->caret.visible = 0;
    ctx->caret.created = 1;
    return NE_USER_OK;
}

int ne_user_set_caret_pos(NEUserContext *ctx, int16_t x, int16_t y)
{
    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (!ctx->caret.created)
        return NE_USER_ERR_NOT_FOUND;

    ctx->caret.x = x;
    ctx->caret.y = y;
    return NE_USER_OK;
}

int ne_user_show_caret(NEUserContext *ctx, NEUserHWND hwnd)
{
    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (!ctx->caret.created)
        return NE_USER_ERR_NOT_FOUND;

    (void)hwnd;

    ctx->caret.visible = 1;
    return NE_USER_OK;
}

int ne_user_hide_caret(NEUserContext *ctx, NEUserHWND hwnd)
{
    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;
    if (!ctx->caret.created)
        return NE_USER_ERR_NOT_FOUND;

    (void)hwnd;

    ctx->caret.visible = 0;
    return NE_USER_OK;
}

int ne_user_destroy_caret(NEUserContext *ctx)
{
    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;

    memset(&ctx->caret, 0, sizeof(ctx->caret));
    return NE_USER_OK;
}

/* --- Input state --- */

int16_t ne_user_get_key_state(NEUserContext *ctx, int vk)
{
    if (!ctx || !ctx->initialized)
        return 0;
    if (vk < 0 || (unsigned)vk >= NE_USER_KEY_STATE_CAP)
        return 0;

    return (int16_t)(ctx->key_state[vk] ? 0x8000 : 0);
}

int16_t ne_user_get_async_key_state(NEUserContext *ctx, int vk)
{
    /* In a full implementation this would read the hardware state;
     * for this stub it delegates to the same key state table. */
    return ne_user_get_key_state(ctx, vk);
}

/* --- Menu APIs --- */

uint16_t ne_user_create_menu(NEUserContext *ctx)
{
    uint16_t i;

    if (!ctx || !ctx->initialized)
        return 0;
    if (ctx->menu_count >= NE_USER_MENU_TABLE_CAP)
        return 0;

    for (i = 0; i < NE_USER_MENU_TABLE_CAP; i++) {
        if (!ctx->menus[i].active) {
            memset(&ctx->menus[i], 0, sizeof(ctx->menus[i]));
            ctx->menus[i].handle = ctx->next_menu++;
            ctx->menus[i].active = 1;
            ctx->menu_count++;
            return ctx->menus[i].handle;
        }
    }

    return 0;
}

int ne_user_set_menu(NEUserContext *ctx, NEUserHWND hwnd, uint16_t hmenu)
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

    wnd->menu_handle = hmenu;
    return NE_USER_OK;
}

int ne_user_append_menu(NEUserContext *ctx, uint16_t hmenu,
                        uint16_t flags, uint16_t id, const char *text)
{
    uint16_t i;

    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;

    (void)flags;

    for (i = 0; i < NE_USER_MENU_TABLE_CAP; i++) {
        if (ctx->menus[i].active && ctx->menus[i].handle == hmenu) {
            NEUserMenu *menu = &ctx->menus[i];
            if (menu->item_count >= NE_USER_MENU_CAP)
                return NE_USER_ERR_FULL;

            NEUserMenuItem *item = &menu->items[menu->item_count];
            item->id     = id;
            item->active = 1;
            if (text) {
                strncpy(item->text, text, NE_USER_CLASSNAME_MAX - 1u);
                item->text[NE_USER_CLASSNAME_MAX - 1u] = '\0';
            } else {
                item->text[0] = '\0';
            }
            menu->item_count++;
            return NE_USER_OK;
        }
    }

    return NE_USER_ERR_NOT_FOUND;
}

uint16_t ne_user_get_menu(NEUserContext *ctx, NEUserHWND hwnd)
{
    NEUserWindow *wnd;

    if (!ctx || !ctx->initialized)
        return 0;
    if (hwnd == NE_USER_HWND_INVALID)
        return 0;

    wnd = user_find_window_by_hwnd(ctx, hwnd);
    if (!wnd)
        return 0;

    return wnd->menu_handle;
}

int ne_user_destroy_menu(NEUserContext *ctx, uint16_t hmenu)
{
    uint16_t i;

    if (!ctx)
        return NE_USER_ERR_NULL;
    if (!ctx->initialized)
        return NE_USER_ERR_INIT;

    for (i = 0; i < NE_USER_MENU_TABLE_CAP; i++) {
        if (ctx->menus[i].active && ctx->menus[i].handle == hmenu) {
            memset(&ctx->menus[i], 0, sizeof(ctx->menus[i]));
            ctx->menu_count--;
            return NE_USER_OK;
        }
    }

    return NE_USER_ERR_NOT_FOUND;
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

/*
 * ne_segmgr.c - Phase 5 Dynamic Segment and Resource Management:
 *               Segment Manager implementation
 *
 * Provides discardable segment eviction / demand-reload from a file
 * image, movable segment compaction, and segment locking.
 *
 * Host-side: uses standard C malloc / calloc / free (via ne_dosalloc.h).
 * Watcom/DOS 16-bit target: the NE_MALLOC / NE_CALLOC / NE_FREE macros
 * expand to DOS INT 21h conventional-memory allocation.
 *
 * Reference: Microsoft Windows 3.1 SDK – Segment Management;
 *            Microsoft "New Executable" format specification.
 */

#include "ne_segmgr.h"
#include "ne_dosalloc.h"
#include "ne_parser.h"   /* NE_SEG_DISCARDABLE, NE_SEG_MOVABLE */

#include <string.h>

/* =========================================================================
 * Internal helpers
 * ===================================================================== */

/*
 * segmgr_find_entry - locate the entry for 'handle' in the table.
 * Returns NULL if the handle is invalid or not found.
 */
static NESegEntry *segmgr_find_entry(NESegMgrContext *ctx, NESegHandle handle)
{
    uint16_t i;

    if (!ctx || !ctx->segs || handle == NE_SEGMGR_HANDLE_INVALID)
        return NULL;

    for (i = 0; i < ctx->capacity; i++) {
        if (ctx->segs[i].handle == handle)
            return &ctx->segs[i];
    }
    return NULL;
}

/*
 * segmgr_free_slot - find an unused slot (handle == 0).
 */
static NESegEntry *segmgr_free_slot(NESegMgrContext *ctx)
{
    uint16_t i;

    if (!ctx || !ctx->segs)
        return NULL;

    for (i = 0; i < ctx->capacity; i++) {
        if (ctx->segs[i].handle == NE_SEGMGR_HANDLE_INVALID)
            return &ctx->segs[i];
    }
    return NULL;
}

/* =========================================================================
 * ne_segmgr_init / ne_segmgr_free
 * ===================================================================== */

int ne_segmgr_init(NESegMgrContext   *ctx,
                   uint16_t           capacity,
                   const uint8_t     *file_buf,
                   size_t             file_len)
{
    if (!ctx || capacity == 0)
        return NE_SEGMGR_ERR_NULL;

    ctx->segs = (NESegEntry *)NE_CALLOC(capacity, sizeof(NESegEntry));
    if (!ctx->segs)
        return NE_SEGMGR_ERR_ALLOC;

    ctx->capacity    = capacity;
    ctx->count       = 0;
    ctx->next_handle = 1;
    ctx->file_buf    = file_buf;
    ctx->file_len    = file_len;
    ctx->initialized = 1;
    return NE_SEGMGR_OK;
}

void ne_segmgr_free(NESegMgrContext *ctx)
{
    uint16_t i;

    if (!ctx)
        return;

    if (ctx->segs) {
        for (i = 0; i < ctx->capacity; i++) {
            if (ctx->segs[i].handle != NE_SEGMGR_HANDLE_INVALID &&
                ctx->segs[i].data   != NULL) {
                NE_FREE(ctx->segs[i].data);
                ctx->segs[i].data = NULL;
            }
        }
        NE_FREE(ctx->segs);
        ctx->segs = NULL;
    }

    memset(ctx, 0, sizeof(*ctx));
}

/* =========================================================================
 * ne_segmgr_add_segment
 * ===================================================================== */

NESegHandle ne_segmgr_add_segment(NESegMgrContext *ctx,
                                  uint16_t         ne_flags,
                                  uint8_t         *data,
                                  uint32_t         alloc_size,
                                  uint32_t         file_off,
                                  uint32_t         file_size)
{
    NESegEntry *slot;
    uint16_t    state;

    if (!ctx || !ctx->initialized)
        return NE_SEGMGR_HANDLE_INVALID;

    if (ctx->count >= ctx->capacity)
        return NE_SEGMGR_HANDLE_INVALID;

    slot = segmgr_free_slot(ctx);
    if (!slot)
        return NE_SEGMGR_HANDLE_INVALID;

    state = NE_SEG_STATE_LOADED;
    if (ne_flags & NE_SEG_MOVABLE)
        state |= NE_SEG_STATE_MOVEABLE;
    if (ne_flags & NE_SEG_DISCARDABLE)
        state |= NE_SEG_STATE_DISCARDABLE;

    slot->handle     = ctx->next_handle++;
    slot->ne_flags   = ne_flags;
    slot->state      = state;
    slot->data       = data;
    slot->alloc_size = alloc_size;
    slot->file_off   = file_off;
    slot->file_size  = file_size;
    slot->lock_count = 0;

    ctx->count++;
    return slot->handle;
}

/* =========================================================================
 * ne_segmgr_evict
 * ===================================================================== */

int ne_segmgr_evict(NESegMgrContext *ctx, NESegHandle handle)
{
    NESegEntry *seg;

    if (!ctx || !ctx->initialized)
        return NE_SEGMGR_ERR_NULL;
    if (handle == NE_SEGMGR_HANDLE_INVALID)
        return NE_SEGMGR_ERR_BAD_HANDLE;

    seg = segmgr_find_entry(ctx, handle);
    if (!seg)
        return NE_SEGMGR_ERR_NOT_FOUND;

    if (!(seg->state & NE_SEG_STATE_DISCARDABLE))
        return NE_SEGMGR_ERR_NOT_DISC;

    if (seg->lock_count > 0)
        return NE_SEGMGR_ERR_LOCKED;

    /* Free the data buffer and mark as evicted */
    if (seg->data) {
        NE_FREE(seg->data);
        seg->data = NULL;
    }
    seg->alloc_size  = 0;
    seg->state      &= ~NE_SEG_STATE_LOADED;
    seg->state      |=  NE_SEG_STATE_EVICTED;
    return NE_SEGMGR_OK;
}

/* =========================================================================
 * ne_segmgr_reload
 * ===================================================================== */

int ne_segmgr_reload(NESegMgrContext *ctx, NESegHandle handle)
{
    NESegEntry *seg;
    uint8_t    *buf;

    if (!ctx || !ctx->initialized)
        return NE_SEGMGR_ERR_NULL;
    if (handle == NE_SEGMGR_HANDLE_INVALID)
        return NE_SEGMGR_ERR_BAD_HANDLE;

    seg = segmgr_find_entry(ctx, handle);
    if (!seg)
        return NE_SEGMGR_ERR_NOT_FOUND;

    if (seg->state & NE_SEG_STATE_LOADED)
        return NE_SEGMGR_ERR_LOADED;

    /* Need a file image for non-BSS segments */
    if (seg->file_size > 0) {
        if (!ctx->file_buf || ctx->file_len == 0)
            return NE_SEGMGR_ERR_NO_FILE;

        if ((size_t)seg->file_off + (size_t)seg->file_size > ctx->file_len)
            return NE_SEGMGR_ERR_IO;

        buf = (uint8_t *)NE_CALLOC(1, seg->file_size);
        if (!buf)
            return NE_SEGMGR_ERR_ALLOC;

        memcpy(buf, ctx->file_buf + seg->file_off, seg->file_size);
        seg->alloc_size = seg->file_size;
    } else {
        /* BSS segment: no file backing */
        buf = NULL;
        seg->alloc_size = 0;
    }

    seg->data   = buf;
    seg->state &= ~NE_SEG_STATE_EVICTED;
    seg->state |=  NE_SEG_STATE_LOADED;
    return NE_SEGMGR_OK;
}

/* =========================================================================
 * ne_segmgr_lock / ne_segmgr_unlock
 * ===================================================================== */

void *ne_segmgr_lock(NESegMgrContext *ctx, NESegHandle handle)
{
    NESegEntry *seg;

    if (!ctx || !ctx->initialized || handle == NE_SEGMGR_HANDLE_INVALID)
        return NULL;

    seg = segmgr_find_entry(ctx, handle);
    if (!seg || !(seg->state & NE_SEG_STATE_LOADED))
        return NULL;

    seg->lock_count++;
    return seg->data;
}

int ne_segmgr_unlock(NESegMgrContext *ctx, NESegHandle handle)
{
    NESegEntry *seg;

    if (!ctx || !ctx->initialized)
        return NE_SEGMGR_ERR_NULL;
    if (handle == NE_SEGMGR_HANDLE_INVALID)
        return NE_SEGMGR_ERR_BAD_HANDLE;

    seg = segmgr_find_entry(ctx, handle);
    if (!seg)
        return NE_SEGMGR_ERR_NOT_FOUND;

    if (seg->lock_count > 0)
        seg->lock_count--;

    return NE_SEGMGR_OK;
}

/* =========================================================================
 * ne_segmgr_compact
 * ===================================================================== */

int ne_segmgr_compact(NESegMgrContext *ctx)
{
    uint16_t i;
    int      compacted = 0;

    if (!ctx || !ctx->initialized)
        return NE_SEGMGR_ERR_NULL;

    for (i = 0; i < ctx->capacity; i++) {
        NESegEntry *seg = &ctx->segs[i];
        uint8_t    *new_buf;

        /* Only compact movable, unlocked, loaded segments with data */
        if (seg->handle == NE_SEGMGR_HANDLE_INVALID)
            continue;
        if (!(seg->state & NE_SEG_STATE_MOVEABLE))
            continue;
        if (!(seg->state & NE_SEG_STATE_LOADED))
            continue;
        if (seg->lock_count > 0)
            continue;
        if (!seg->data || seg->alloc_size == 0)
            continue;

        /* Allocate a fresh buffer and copy the data */
        new_buf = (uint8_t *)NE_MALLOC(seg->alloc_size);
        if (!new_buf)
            return NE_SEGMGR_ERR_ALLOC;

        memcpy(new_buf, seg->data, seg->alloc_size);
        NE_FREE(seg->data);
        seg->data = new_buf;
        compacted++;
    }

    return compacted;
}

/* =========================================================================
 * ne_segmgr_find
 * ===================================================================== */

NESegEntry *ne_segmgr_find(NESegMgrContext *ctx, NESegHandle handle)
{
    return segmgr_find_entry(ctx, handle);
}

/* =========================================================================
 * ne_segmgr_strerror
 * ===================================================================== */

const char *ne_segmgr_strerror(int err)
{
    switch (err) {
    case NE_SEGMGR_OK:             return "success";
    case NE_SEGMGR_ERR_NULL:       return "NULL pointer argument";
    case NE_SEGMGR_ERR_ALLOC:      return "memory allocation failure";
    case NE_SEGMGR_ERR_FULL:       return "segment table at capacity";
    case NE_SEGMGR_ERR_NOT_FOUND:  return "segment handle not found";
    case NE_SEGMGR_ERR_BAD_HANDLE: return "invalid segment handle";
    case NE_SEGMGR_ERR_LOCKED:     return "cannot evict a locked segment";
    case NE_SEGMGR_ERR_NOT_DISC:   return "segment is not discardable";
    case NE_SEGMGR_ERR_LOADED:     return "segment is already loaded";
    case NE_SEGMGR_ERR_NO_FILE:    return "no file image available for reload";
    case NE_SEGMGR_ERR_IO:         return "file bounds / read failure";
    default:                       return "unknown error";
    }
}

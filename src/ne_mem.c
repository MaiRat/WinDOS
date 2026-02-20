/*
 * ne_mem.c - Global and local memory management implementation
 *
 * Implements Step 6 of the WinDOS kernel-replacement roadmap:
 * Windows 3.1-compatible GMEM and LMEM primitives.
 *
 * Host-side: malloc / calloc / free (standard C library).
 * Watcom/DOS 16-bit target: replace with _fmalloc / _fcalloc / _ffree
 * (Watcom far-heap) or DOS INT 21h AH=48h / AH=49h (conventional memory).
 */

#include "ne_mem.h"
#include "ne_dosalloc.h"

#include <string.h>

/* =========================================================================
 * Internal helpers – GMEM
 * ===================================================================== */

/*
 * gmem_find_block - locate the block entry for 'handle'.
 * Returns NULL if not found or handle is invalid.
 */
static NEGMemBlock *gmem_find_block(NEGMemTable *tbl, NEGMemHandle handle)
{
    uint16_t i;

    if (!tbl || !tbl->blocks || handle == NE_GMEM_HANDLE_INVALID)
        return NULL;

    for (i = 0; i < tbl->capacity; i++) {
        if (tbl->blocks[i].handle == handle)
            return &tbl->blocks[i];
    }
    return NULL;
}

/*
 * gmem_free_slot - locate a slot with handle == 0 (unused).
 */
static NEGMemBlock *gmem_free_slot(NEGMemTable *tbl)
{
    uint16_t i;

    if (!tbl || !tbl->blocks)
        return NULL;

    for (i = 0; i < tbl->capacity; i++) {
        if (tbl->blocks[i].handle == NE_GMEM_HANDLE_INVALID)
            return &tbl->blocks[i];
    }
    return NULL;
}

/* =========================================================================
 * ne_gmem_table_init / ne_gmem_table_free
 * ===================================================================== */

int ne_gmem_table_init(NEGMemTable *tbl, uint16_t capacity)
{
    if (!tbl)
        return NE_MEM_ERR_NULL;
    if (capacity == 0)
        return NE_MEM_ERR_NULL;

    memset(tbl, 0, sizeof(*tbl));

    tbl->blocks = (NEGMemBlock *)NE_CALLOC(capacity, sizeof(NEGMemBlock));
    if (!tbl->blocks)
        return NE_MEM_ERR_ALLOC;

    tbl->capacity    = capacity;
    tbl->count       = 0;
    tbl->next_handle = 1u;

    return NE_MEM_OK;
}

void ne_gmem_table_free(NEGMemTable *tbl)
{
    uint16_t i;

    if (!tbl)
        return;

    if (tbl->blocks) {
        for (i = 0; i < tbl->capacity; i++) {
            if (tbl->blocks[i].handle != NE_GMEM_HANDLE_INVALID &&
                tbl->blocks[i].data   != NULL) {
                NE_FREE(tbl->blocks[i].data);
                tbl->blocks[i].data = NULL;
            }
        }
        NE_FREE(tbl->blocks);
    }

    memset(tbl, 0, sizeof(*tbl));
}

/* =========================================================================
 * ne_gmem_alloc
 * ===================================================================== */

NEGMemHandle ne_gmem_alloc(NEGMemTable *tbl,
                            uint16_t     flags,
                            uint32_t     size,
                            uint16_t     owner)
{
    NEGMemBlock *slot;
    uint8_t     *buf;

    if (!tbl || size == 0)
        return NE_GMEM_HANDLE_INVALID;

    if (tbl->count >= tbl->capacity)
        return NE_GMEM_HANDLE_INVALID;

    slot = gmem_free_slot(tbl);
    if (!slot)
        return NE_GMEM_HANDLE_INVALID;

    /*
     * Allocate the data buffer.
     * On Watcom/DOS: uses DOS INT 21h AH=48h for conventional memory.
     * On POSIX host: uses standard C library malloc/calloc.
     */
    if (flags & NE_GMEM_ZEROINIT) {
        buf = (uint8_t *)NE_CALLOC(1u, size);
    } else {
        buf = (uint8_t *)NE_MALLOC(size);
    }
    if (!buf)
        return NE_GMEM_HANDLE_INVALID;

    slot->handle     = tbl->next_handle++;
    slot->flags      = flags;
    slot->data       = buf;
    slot->size       = size;
    slot->lock_count = 0;
    slot->owner_task = owner;

    tbl->count++;

    return slot->handle;
}

/* =========================================================================
 * ne_gmem_free
 * ===================================================================== */

int ne_gmem_free(NEGMemTable *tbl, NEGMemHandle handle)
{
    NEGMemBlock *b;

    if (!tbl)
        return NE_MEM_ERR_NULL;
    if (handle == NE_GMEM_HANDLE_INVALID)
        return NE_MEM_ERR_BAD_HANDLE;

    b = gmem_find_block(tbl, handle);
    if (!b)
        return NE_MEM_ERR_NOT_FOUND;

    /* Free the data buffer. */
    if (b->data) {
        NE_FREE(b->data);
        b->data = NULL;
    }

    /* Zero the slot so it can be reused. */
    memset(b, 0, sizeof(*b));
    tbl->count--;

    return NE_MEM_OK;
}

/* =========================================================================
 * ne_gmem_lock / ne_gmem_unlock
 * ===================================================================== */

void *ne_gmem_lock(NEGMemTable *tbl, NEGMemHandle handle)
{
    NEGMemBlock *b;

    if (!tbl || handle == NE_GMEM_HANDLE_INVALID)
        return NULL;

    b = gmem_find_block(tbl, handle);
    if (!b || !b->data)
        return NULL;

    b->lock_count++;
    return b->data;
}

int ne_gmem_unlock(NEGMemTable *tbl, NEGMemHandle handle)
{
    NEGMemBlock *b;

    if (!tbl)
        return NE_MEM_ERR_NULL;
    if (handle == NE_GMEM_HANDLE_INVALID)
        return NE_MEM_ERR_BAD_HANDLE;

    b = gmem_find_block(tbl, handle);
    if (!b)
        return NE_MEM_ERR_NOT_FOUND;

    if (b->lock_count > 0)
        b->lock_count--;

    return NE_MEM_OK;
}

/* =========================================================================
 * ne_gmem_size
 * ===================================================================== */

uint32_t ne_gmem_size(const NEGMemTable *tbl, NEGMemHandle handle)
{
    uint16_t i;

    if (!tbl || !tbl->blocks || handle == NE_GMEM_HANDLE_INVALID)
        return 0;

    for (i = 0; i < tbl->capacity; i++) {
        if (tbl->blocks[i].handle == handle)
            return tbl->blocks[i].size;
    }
    return 0;
}

/* =========================================================================
 * ne_gmem_find_block (public wrapper)
 * ===================================================================== */

NEGMemBlock *ne_gmem_find_block(NEGMemTable *tbl, NEGMemHandle handle)
{
    return gmem_find_block(tbl, handle);
}

/* =========================================================================
 * ne_gmem_free_by_owner
 * ===================================================================== */

uint16_t ne_gmem_free_by_owner(NEGMemTable *tbl, uint16_t owner_task)
{
    uint16_t i;
    uint16_t freed = 0;

    if (!tbl || !tbl->blocks || owner_task == 0)
        return 0;

    for (i = 0; i < tbl->capacity; i++) {
        NEGMemBlock *b = &tbl->blocks[i];
        if (b->handle == NE_GMEM_HANDLE_INVALID)
            continue;
        if (b->owner_task != owner_task)
            continue;

        if (b->data) {
            NE_FREE(b->data);
            b->data = NULL;
        }
        memset(b, 0, sizeof(*b));
        tbl->count--;
        freed++;
    }

    return freed;
}

/* =========================================================================
 * Internal helpers – LMEM
 * ===================================================================== */

static NELMemBlock *lmem_find_block(NELMemHeap *heap, NELMemHandle handle)
{
    uint16_t i;

    if (!heap || handle == NE_LMEM_HANDLE_INVALID)
        return NULL;

    for (i = 0; i < NE_LMEM_HEAP_CAP; i++) {
        if (heap->blocks[i].handle == handle)
            return &heap->blocks[i];
    }
    return NULL;
}

static NELMemBlock *lmem_free_slot(NELMemHeap *heap)
{
    uint16_t i;

    if (!heap)
        return NULL;

    for (i = 0; i < NE_LMEM_HEAP_CAP; i++) {
        if (heap->blocks[i].handle == NE_LMEM_HANDLE_INVALID)
            return &heap->blocks[i];
    }
    return NULL;
}

/* =========================================================================
 * ne_lmem_heap_init / ne_lmem_heap_free
 * ===================================================================== */

int ne_lmem_heap_init(NELMemHeap *heap)
{
    if (!heap)
        return NE_MEM_ERR_NULL;

    memset(heap, 0, sizeof(*heap));
    heap->next_handle = 1u;

    return NE_MEM_OK;
}

void ne_lmem_heap_free(NELMemHeap *heap)
{
    uint16_t i;

    if (!heap)
        return;

    for (i = 0; i < NE_LMEM_HEAP_CAP; i++) {
        if (heap->blocks[i].handle != NE_LMEM_HANDLE_INVALID &&
            heap->blocks[i].data   != NULL) {
            NE_FREE(heap->blocks[i].data);
            heap->blocks[i].data = NULL;
        }
    }

    memset(heap, 0, sizeof(*heap));
}

/* =========================================================================
 * ne_lmem_alloc
 * ===================================================================== */

NELMemHandle ne_lmem_alloc(NELMemHeap *heap, uint16_t flags, uint16_t size)
{
    NELMemBlock *slot;
    uint8_t     *buf;

    if (!heap || size == 0)
        return NE_LMEM_HANDLE_INVALID;

    if (heap->count >= NE_LMEM_HEAP_CAP)
        return NE_LMEM_HANDLE_INVALID;

    slot = lmem_free_slot(heap);
    if (!slot)
        return NE_LMEM_HANDLE_INVALID;

    if (flags & NE_LMEM_ZEROINIT) {
        buf = (uint8_t *)NE_CALLOC(1u, size);
    } else {
        buf = (uint8_t *)NE_MALLOC(size);
    }
    if (!buf)
        return NE_LMEM_HANDLE_INVALID;

    slot->handle     = heap->next_handle++;
    slot->flags      = flags;
    slot->data       = buf;
    slot->size       = size;
    slot->lock_count = 0;

    heap->count++;

    return slot->handle;
}

/* =========================================================================
 * ne_lmem_free
 * ===================================================================== */

int ne_lmem_free(NELMemHeap *heap, NELMemHandle handle)
{
    NELMemBlock *b;

    if (!heap)
        return NE_MEM_ERR_NULL;
    if (handle == NE_LMEM_HANDLE_INVALID)
        return NE_MEM_ERR_BAD_HANDLE;

    b = lmem_find_block(heap, handle);
    if (!b)
        return NE_MEM_ERR_NOT_FOUND;

    if (b->data) {
        NE_FREE(b->data);
        b->data = NULL;
    }

    memset(b, 0, sizeof(*b));
    heap->count--;

    return NE_MEM_OK;
}

/* =========================================================================
 * ne_lmem_lock / ne_lmem_unlock
 * ===================================================================== */

void *ne_lmem_lock(NELMemHeap *heap, NELMemHandle handle)
{
    NELMemBlock *b;

    if (!heap || handle == NE_LMEM_HANDLE_INVALID)
        return NULL;

    b = lmem_find_block(heap, handle);
    if (!b || !b->data)
        return NULL;

    b->lock_count++;
    return b->data;
}

int ne_lmem_unlock(NELMemHeap *heap, NELMemHandle handle)
{
    NELMemBlock *b;

    if (!heap)
        return NE_MEM_ERR_NULL;
    if (handle == NE_LMEM_HANDLE_INVALID)
        return NE_MEM_ERR_BAD_HANDLE;

    b = lmem_find_block(heap, handle);
    if (!b)
        return NE_MEM_ERR_NOT_FOUND;

    if (b->lock_count > 0)
        b->lock_count--;

    return NE_MEM_OK;
}

/* =========================================================================
 * ne_lmem_size
 * ===================================================================== */

uint16_t ne_lmem_size(const NELMemHeap *heap, NELMemHandle handle)
{
    uint16_t i;

    if (!heap || handle == NE_LMEM_HANDLE_INVALID)
        return 0;

    for (i = 0; i < NE_LMEM_HEAP_CAP; i++) {
        if (heap->blocks[i].handle == handle)
            return heap->blocks[i].size;
    }
    return 0;
}

/* =========================================================================
 * ne_gmem_flags
 * ===================================================================== */

uint16_t ne_gmem_flags(const NEGMemTable *tbl, NEGMemHandle handle)
{
    uint16_t i;

    if (!tbl || !tbl->blocks || handle == NE_GMEM_HANDLE_INVALID)
        return 0;

    for (i = 0; i < tbl->capacity; i++) {
        if (tbl->blocks[i].handle == handle)
            return tbl->blocks[i].flags;
    }
    return 0;
}

/* =========================================================================
 * ne_gmem_handle
 * ===================================================================== */

NEGMemHandle ne_gmem_handle(const NEGMemTable *tbl, const void *ptr)
{
    uint16_t i;

    if (!tbl || !tbl->blocks || !ptr)
        return NE_GMEM_HANDLE_INVALID;

    for (i = 0; i < tbl->capacity; i++) {
        if (tbl->blocks[i].handle != NE_GMEM_HANDLE_INVALID &&
            tbl->blocks[i].data == (const uint8_t *)ptr)
            return tbl->blocks[i].handle;
    }
    return NE_GMEM_HANDLE_INVALID;
}

/* =========================================================================
 * ne_gmem_compact
 * ===================================================================== */

uint32_t ne_gmem_compact(NEGMemTable *tbl)
{
    (void)tbl;
    return 0;
}

/* =========================================================================
 * ne_lmem_realloc
 * ===================================================================== */

NELMemHandle ne_lmem_realloc(NELMemHeap *heap, NELMemHandle handle,
                              uint16_t new_size, uint16_t flags)
{
    NELMemBlock *b;
    uint8_t     *buf;
    uint16_t     copy_size;

    (void)flags;

    if (!heap || handle == NE_LMEM_HANDLE_INVALID || new_size == 0)
        return NE_LMEM_HANDLE_INVALID;

    b = lmem_find_block(heap, handle);
    if (!b)
        return NE_LMEM_HANDLE_INVALID;

    buf = (uint8_t *)NE_MALLOC(new_size);
    if (!buf)
        return NE_LMEM_HANDLE_INVALID;

    copy_size = (b->size < new_size) ? b->size : new_size;
    if (b->data) {
        memcpy(buf, b->data, copy_size);
        NE_FREE(b->data);
    }

    b->data = buf;
    b->size = new_size;

    return handle;
}

/* =========================================================================
 * ne_lmem_flags
 * ===================================================================== */

uint16_t ne_lmem_flags(const NELMemHeap *heap, NELMemHandle handle)
{
    uint16_t i;

    if (!heap || handle == NE_LMEM_HANDLE_INVALID)
        return 0;

    for (i = 0; i < NE_LMEM_HEAP_CAP; i++) {
        if (heap->blocks[i].handle == handle)
            return heap->blocks[i].flags;
    }
    return 0;
}

/* =========================================================================
 * ne_lmem_handle
 * ===================================================================== */

NELMemHandle ne_lmem_handle(const NELMemHeap *heap, const void *ptr)
{
    uint16_t i;

    if (!heap || !ptr)
        return NE_LMEM_HANDLE_INVALID;

    for (i = 0; i < NE_LMEM_HEAP_CAP; i++) {
        if (heap->blocks[i].handle != NE_LMEM_HANDLE_INVALID &&
            heap->blocks[i].data == (const uint8_t *)ptr)
            return heap->blocks[i].handle;
    }
    return NE_LMEM_HANDLE_INVALID;
}

/* =========================================================================
 * ne_lmem_compact
 * ===================================================================== */

uint16_t ne_lmem_compact(NELMemHeap *heap)
{
    (void)heap;
    return 0;
}

/* =========================================================================
 * ne_mem_strerror
 * ===================================================================== */

const char *ne_mem_strerror(int err)
{
    switch (err) {
    case NE_MEM_OK:              return "success";
    case NE_MEM_ERR_NULL:        return "NULL pointer argument";
    case NE_MEM_ERR_ALLOC:       return "memory allocation failure";
    case NE_MEM_ERR_FULL:        return "block table at capacity";
    case NE_MEM_ERR_BAD_HANDLE:  return "invalid memory handle";
    case NE_MEM_ERR_NOT_FOUND:   return "memory handle not found";
    case NE_MEM_ERR_LOCKED:      return "block is locked";
    case NE_MEM_ERR_ZERO_SIZE:   return "zero-byte allocation requested";
    case NE_MEM_ERR_LMEM_FULL:   return "local heap is full";
    default:                     return "unknown error";
    }
}

/*
 * ne_segmgr.h - Phase 5 Dynamic Segment and Resource Management:
 *               Segment Manager
 *
 * Implements the two segment-level capabilities required by larger
 * Windows 3.1 applications:
 *
 *   1. Discardable segment eviction and demand-reload from file.
 *      When physical memory is scarce the kernel may evict any unlocked
 *      segment that carries the NE_SEG_DISCARDABLE flag.  The segment's
 *      data buffer is freed; a subsequent access causes a demand-reload
 *      that re-reads the raw bytes from the file image stored in the
 *      manager context.
 *
 *   2. Movable segment compaction and handle table updates.
 *      When the heap becomes fragmented, ne_segmgr_compact() relocates
 *      all unlocked movable segments so that they are contiguous, then
 *      updates every slot's data pointer accordingly.  The caller is
 *      responsible for flushing any cached far pointers after compaction.
 *
 * Segment handles are 1-based uint16_t indices into the segment table.
 * 0 (NE_SEGMGR_HANDLE_INVALID) is the null sentinel, matching the
 * Windows 3.1 convention for invalid handles.
 *
 * The host-side implementation uses standard C malloc/free; on a real
 * 16-bit DOS target these should be replaced with the NE_MALLOC /
 * NE_CALLOC / NE_FREE macros defined in ne_dosalloc.h.
 *
 * Reference: Microsoft Windows 3.1 SDK – Segment Management;
 *            Microsoft "New Executable" format specification.
 */

#ifndef NE_SEGMGR_H
#define NE_SEGMGR_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_SEGMGR_OK               0
#define NE_SEGMGR_ERR_NULL        -1   /* NULL pointer argument             */
#define NE_SEGMGR_ERR_ALLOC       -2   /* memory allocation failure         */
#define NE_SEGMGR_ERR_FULL        -3   /* segment table at capacity         */
#define NE_SEGMGR_ERR_NOT_FOUND   -4   /* handle not in table               */
#define NE_SEGMGR_ERR_BAD_HANDLE  -5   /* zero or otherwise invalid handle  */
#define NE_SEGMGR_ERR_LOCKED      -6   /* cannot evict a locked segment     */
#define NE_SEGMGR_ERR_NOT_DISC    -7   /* segment is not discardable        */
#define NE_SEGMGR_ERR_LOADED      -8   /* segment is already loaded         */
#define NE_SEGMGR_ERR_NO_FILE     -9   /* no file image available to reload */
#define NE_SEGMGR_ERR_IO         -10   /* file bounds / read failure        */

/* -------------------------------------------------------------------------
 * Segment state flags (stored in NESegEntry.state)
 * ---------------------------------------------------------------------- */
#define NE_SEG_STATE_LOADED    0x0001u  /* segment data is in memory         */
#define NE_SEG_STATE_EVICTED   0x0002u  /* data was discarded; reload needed */
#define NE_SEG_STATE_MOVEABLE  0x0004u  /* segment may be relocated          */
#define NE_SEG_STATE_DISCARDABLE 0x0008u/* segment may be evicted when free  */

/* -------------------------------------------------------------------------
 * Configuration constants
 * ---------------------------------------------------------------------- */
#define NE_SEGMGR_DEFAULT_CAP  64u   /* default segment table capacity      */

/* -------------------------------------------------------------------------
 * Segment handle type
 *
 * A non-zero uint16_t that identifies one segment in the manager.
 * NE_SEGMGR_HANDLE_INVALID (0) is the null sentinel.
 * ---------------------------------------------------------------------- */
typedef uint16_t NESegHandle;

#define NE_SEGMGR_HANDLE_INVALID ((NESegHandle)0)

/* -------------------------------------------------------------------------
 * Segment entry  (internal)
 *
 * Describes one loaded or evicted segment.
 * ---------------------------------------------------------------------- */
typedef struct {
    NESegHandle  handle;      /* 1-based handle (0 = free slot)              */
    uint16_t     ne_flags;    /* NE_SEG_* flags from the NE segment table    */
    uint16_t     state;       /* NE_SEG_STATE_* bitmask                      */
    uint8_t     *data;        /* heap-allocated segment image (NULL if evict) */
    uint32_t     alloc_size;  /* total bytes allocated (0 when evicted)      */
    uint32_t     file_off;    /* source byte offset within the file image    */
    uint32_t     file_size;   /* byte count in file (may be 0 = BSS-like)    */
    uint16_t     lock_count;  /* number of outstanding locks                 */
} NESegEntry;

/* -------------------------------------------------------------------------
 * Segment manager context
 *
 * Initialise with ne_segmgr_init(); release with ne_segmgr_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    NESegEntry  *segs;        /* heap-allocated array [0..capacity-1]        */
    uint16_t     capacity;    /* total slots                                 */
    uint16_t     count;       /* number of active (non-free) slots           */
    uint16_t     next_handle; /* next handle value to assign (starts at 1)   */

    /* File image used for demand-reload.  May be NULL if demand-reload is
     * not required (all segments pre-loaded or BSS-only). */
    const uint8_t *file_buf;  /* pointer to the complete NE file image       */
    size_t         file_len;  /* byte length of file_buf                     */

    int          initialized; /* non-zero after successful init              */
} NESegMgrContext;

/* =========================================================================
 * Public API – initialisation and teardown
 * ===================================================================== */

/*
 * ne_segmgr_init - initialise a segment manager context.
 *
 * 'capacity' must be > 0 (pass NE_SEGMGR_DEFAULT_CAP for the default).
 * 'file_buf' / 'file_len' are the NE file image used for demand-reload;
 * pass NULL / 0 if demand-reload is not needed.
 *
 * Returns NE_SEGMGR_OK on success.  Call ne_segmgr_free() when done.
 */
int ne_segmgr_init(NESegMgrContext   *ctx,
                   uint16_t           capacity,
                   const uint8_t     *file_buf,
                   size_t             file_len);

/*
 * ne_segmgr_free - release all resources owned by *ctx.
 *
 * Frees every loaded segment's data buffer, then frees the segment array.
 * Safe to call on a zeroed or partially-initialised context and on NULL.
 */
void ne_segmgr_free(NESegMgrContext *ctx);

/* =========================================================================
 * Public API – segment registration
 * ===================================================================== */

/*
 * ne_segmgr_add_segment - register a segment and transfer ownership of
 * its data buffer to the manager.
 *
 * 'ne_flags'  : NE_SEG_* flags from the NE segment table.
 * 'data'      : heap-allocated segment data; the manager takes ownership
 *               and will free it on eviction or context teardown.
 *               May be NULL for a zero-size (BSS) segment.
 * 'alloc_size': byte count allocated for data (0 if data is NULL).
 * 'file_off'  : source offset in the NE file image (for demand-reload).
 * 'file_size' : byte count in the file (0 for BSS segments).
 *
 * Returns a non-zero NESegHandle on success, NE_SEGMGR_HANDLE_INVALID on
 * failure (table full, NULL context, or malloc failure).
 */
NESegHandle ne_segmgr_add_segment(NESegMgrContext *ctx,
                                  uint16_t         ne_flags,
                                  uint8_t         *data,
                                  uint32_t         alloc_size,
                                  uint32_t         file_off,
                                  uint32_t         file_size);

/* =========================================================================
 * Public API – eviction and demand-reload
 * ===================================================================== */

/*
 * ne_segmgr_evict - evict a discardable segment from memory.
 *
 * Frees the segment's data buffer and marks it as evicted.  The segment
 * must be discardable (NE_SEG_DISCARDABLE flag set in ne_flags) and must
 * not be locked (lock_count == 0).
 *
 * Returns NE_SEGMGR_OK, NE_SEGMGR_ERR_LOCKED, NE_SEGMGR_ERR_NOT_DISC,
 * NE_SEGMGR_ERR_NOT_FOUND, or NE_SEGMGR_ERR_BAD_HANDLE.
 */
int ne_segmgr_evict(NESegMgrContext *ctx, NESegHandle handle);

/*
 * ne_segmgr_reload - demand-reload an evicted segment from the file image.
 *
 * Re-allocates a buffer for the segment, copies file_size bytes from
 * file_buf[file_off], and marks the segment as loaded again.
 *
 * The segment must currently be in the evicted state.  A file image
 * (file_buf != NULL) must have been supplied to ne_segmgr_init().
 *
 * Returns NE_SEGMGR_OK, NE_SEGMGR_ERR_LOADED, NE_SEGMGR_ERR_NO_FILE,
 * NE_SEGMGR_ERR_IO, NE_SEGMGR_ERR_ALLOC, NE_SEGMGR_ERR_NOT_FOUND, or
 * NE_SEGMGR_ERR_BAD_HANDLE.
 */
int ne_segmgr_reload(NESegMgrContext *ctx, NESegHandle handle);

/* =========================================================================
 * Public API – locking
 * ===================================================================== */

/*
 * ne_segmgr_lock - increment the lock count of a segment and return a
 * pointer to its data.
 *
 * A locked segment cannot be evicted.  Returns NULL if the segment is
 * not found, is evicted, or handle is invalid.
 */
void *ne_segmgr_lock(NESegMgrContext *ctx, NESegHandle handle);

/*
 * ne_segmgr_unlock - decrement the lock count of a segment.
 *
 * Returns NE_SEGMGR_OK, NE_SEGMGR_ERR_NOT_FOUND, or
 * NE_SEGMGR_ERR_BAD_HANDLE.
 */
int ne_segmgr_unlock(NESegMgrContext *ctx, NESegHandle handle);

/* =========================================================================
 * Public API – compaction
 * ===================================================================== */

/*
 * ne_segmgr_compact - compact all movable unlocked segments.
 *
 * Re-allocates each movable, unlocked, loaded segment into a fresh
 * (ideally contiguous) buffer, copies the data, and updates the entry's
 * data pointer.  This simulates the Windows 3.1 GlobalCompact() heap
 * walk that eliminates free-space holes between movable objects.
 *
 * Returns the number of segments successfully compacted, or a negative
 * NE_SEGMGR_ERR_* code if a reallocation fails.
 */
int ne_segmgr_compact(NESegMgrContext *ctx);

/* =========================================================================
 * Public API – query
 * ===================================================================== */

/*
 * ne_segmgr_find - return a pointer to the NESegEntry for 'handle'.
 *
 * Returns NULL if not found.  The pointer is valid until the next alloc,
 * evict, or compact operation on the context.
 */
NESegEntry *ne_segmgr_find(NESegMgrContext *ctx, NESegHandle handle);

/*
 * ne_segmgr_strerror - return a static string describing error code 'err'.
 */
const char *ne_segmgr_strerror(int err);

#endif /* NE_SEGMGR_H */

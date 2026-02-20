/*
 * ne_mem.h - Windows 3.1-compatible global and local memory management
 *
 * Implements Step 6 of the WinDOS kernel-replacement roadmap:
 *   - Windows 3.1-compatible global memory allocation (GMEM) primitives.
 *   - Local memory allocation (LMEM) primitives per task heap.
 *   - Memory ownership tracking per task.
 *
 * The host-side implementation uses standard C library malloc/free/calloc.
 * On a real 16-bit DOS target the allocation calls should be replaced with
 * Watcom far-heap primitives (_fmalloc / _ffree / _fcalloc) or DOS
 * INT 21h / AH=48h (allocate memory) and AH=49h (release memory).
 *
 * GMEM handle values are 1-based uint16_t indices into the global block
 * table.  0 (NE_GMEM_HANDLE_INVALID) is the sentinel for an invalid handle,
 * matching the NULL-handle convention used by the Windows 3.1 API.
 *
 * Reference: Microsoft Windows 3.1 SDK – Memory Management Functions.
 */

#ifndef NE_MEM_H
#define NE_MEM_H

#include <stdint.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_MEM_OK               0
#define NE_MEM_ERR_NULL        -1   /* NULL pointer argument               */
#define NE_MEM_ERR_ALLOC       -2   /* memory allocation failure           */
#define NE_MEM_ERR_FULL        -3   /* block table at capacity             */
#define NE_MEM_ERR_BAD_HANDLE  -4   /* zero or otherwise invalid handle    */
#define NE_MEM_ERR_NOT_FOUND   -5   /* handle not in table                 */
#define NE_MEM_ERR_LOCKED      -6   /* cannot discard a locked block       */
#define NE_MEM_ERR_ZERO_SIZE   -7   /* zero-byte allocation requested      */
#define NE_MEM_ERR_LMEM_FULL   -8   /* local heap has no free slots        */

/* -------------------------------------------------------------------------
 * GMEM flags  (Windows 3.1 GlobalAlloc compatible subset)
 * ---------------------------------------------------------------------- */
#define NE_GMEM_FIXED        0x0000u  /* non-movable block                 */
#define NE_GMEM_MOVEABLE     0x0002u  /* movable; requires Lock/Unlock     */
#define NE_GMEM_ZEROINIT     0x0040u  /* zero-initialise on allocation     */
#define NE_GMEM_DISCARDABLE  0x0100u  /* may be discarded when unlocked    */

/* -------------------------------------------------------------------------
 * LMEM flags  (Windows 3.1 LocalAlloc compatible subset)
 * ---------------------------------------------------------------------- */
#define NE_LMEM_FIXED        0x0000u  /* non-movable block                 */
#define NE_LMEM_MOVEABLE     0x0002u  /* movable block                     */
#define NE_LMEM_ZEROINIT     0x0040u  /* zero-initialise on allocation     */

/* -------------------------------------------------------------------------
 * Configuration constants
 * ---------------------------------------------------------------------- */

/* Default initial capacity for a new GMEM block table. */
#define NE_GMEM_TABLE_CAP   128u

/* Maximum number of blocks in a single local heap. */
#define NE_LMEM_HEAP_CAP     64u

/* -------------------------------------------------------------------------
 * GMEM handle type
 *
 * A non-zero uint16_t value that identifies one global memory block.
 * NE_GMEM_HANDLE_INVALID (0) is the null sentinel.
 * ---------------------------------------------------------------------- */
typedef uint16_t NEGMemHandle;

#define NE_GMEM_HANDLE_INVALID ((NEGMemHandle)0)

/* -------------------------------------------------------------------------
 * LMEM handle type
 *
 * A non-zero uint16_t value that identifies one local memory block within
 * a task's local heap.  NE_LMEM_HANDLE_INVALID (0) is the null sentinel.
 * ---------------------------------------------------------------------- */
typedef uint16_t NELMemHandle;

#define NE_LMEM_HANDLE_INVALID ((NELMemHandle)0)

/* -------------------------------------------------------------------------
 * GMEM block descriptor  (internal)
 * ---------------------------------------------------------------------- */
typedef struct {
    NEGMemHandle  handle;      /* 1-based handle (0 = free slot)           */
    uint16_t      flags;       /* NE_GMEM_* flags supplied at alloc time   */
    uint8_t      *data;        /* heap-allocated block data                */
    uint32_t      size;        /* allocated byte count                     */
    uint16_t      lock_count;  /* number of outstanding locks              */
    uint16_t      owner_task;  /* NETaskHandle of owning task (0 = none)   */
} NEGMemBlock;

/* -------------------------------------------------------------------------
 * GMEM table
 *
 * Initialise with ne_gmem_table_init(); release with ne_gmem_table_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    NEGMemBlock *blocks;       /* heap-allocated array [0..capacity-1]     */
    uint16_t     capacity;     /* total slots                              */
    uint16_t     count;        /* number of allocated (active) slots       */
    uint16_t     next_handle;  /* next handle value to assign (starts at 1)*/
} NEGMemTable;

/* -------------------------------------------------------------------------
 * LMEM block descriptor  (internal)
 * ---------------------------------------------------------------------- */
typedef struct {
    NELMemHandle  handle;      /* 1-based handle (0 = free slot)           */
    uint16_t      flags;       /* NE_LMEM_* flags                         */
    uint8_t      *data;        /* heap-allocated block data                */
    uint16_t      size;        /* allocated byte count (LMEM ≤ 64 KB)     */
    uint16_t      lock_count;  /* number of outstanding locks              */
} NELMemBlock;

/* -------------------------------------------------------------------------
 * Local heap (one per task)
 *
 * Initialise with ne_lmem_heap_init(); release with ne_lmem_heap_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    NELMemBlock  blocks[NE_LMEM_HEAP_CAP]; /* fixed-size block array      */
    uint16_t     count;        /* number of allocated (active) blocks      */
    uint16_t     next_handle;  /* next handle value to assign              */
} NELMemHeap;

/* =========================================================================
 * Public API – global memory (GMEM)
 * ===================================================================== */

/*
 * ne_gmem_table_init - initialise *tbl with 'capacity' pre-allocated slots.
 *
 * 'capacity' must be > 0.  Pass NE_GMEM_TABLE_CAP for the default size.
 * Returns NE_MEM_OK on success; call ne_gmem_table_free() when done.
 */
int ne_gmem_table_init(NEGMemTable *tbl, uint16_t capacity);

/*
 * ne_gmem_table_free - release all resources owned by *tbl.
 *
 * Frees the data buffer of every active block, then frees the blocks array.
 * Safe to call on a zeroed or partially-initialised table and on NULL.
 */
void ne_gmem_table_free(NEGMemTable *tbl);

/*
 * ne_gmem_alloc - allocate a global memory block.
 *
 * 'flags'  : combination of NE_GMEM_* constants.
 * 'size'   : byte count to allocate; must be > 0.
 * 'owner'  : NETaskHandle of the owning task, or 0 for no owner.
 *
 * Returns a non-zero NEGMemHandle on success or NE_GMEM_HANDLE_INVALID on
 * failure (table full or malloc failure).
 *
 * On the Watcom/DOS target, replace malloc/calloc with _fmalloc/_fcalloc
 * or INT 21h / AH=48h for conventional-memory allocation.
 */
NEGMemHandle ne_gmem_alloc(NEGMemTable *tbl,
                            uint16_t     flags,
                            uint32_t     size,
                            uint16_t     owner);

/*
 * ne_gmem_free - free a global memory block.
 *
 * Releases the data buffer of the block identified by 'handle' and removes
 * the entry from the table.  Locked blocks (lock_count > 0) may still be
 * freed; the caller is responsible for not dereferencing the stale pointer.
 *
 * Returns NE_MEM_OK, NE_MEM_ERR_BAD_HANDLE, or NE_MEM_ERR_NOT_FOUND.
 */
int ne_gmem_free(NEGMemTable *tbl, NEGMemHandle handle);

/*
 * ne_gmem_lock - increment the lock count and return a pointer to the data.
 *
 * While a block's lock count is > 0 it is considered "pinned" and must not
 * be moved or discarded by a memory compaction pass.  Each ne_gmem_lock()
 * call must be balanced by a corresponding ne_gmem_unlock() call.
 *
 * Returns a pointer to the block data on success or NULL on failure.
 */
void *ne_gmem_lock(NEGMemTable *tbl, NEGMemHandle handle);

/*
 * ne_gmem_unlock - decrement the lock count.
 *
 * Returns NE_MEM_OK, NE_MEM_ERR_BAD_HANDLE, or NE_MEM_ERR_NOT_FOUND.
 */
int ne_gmem_unlock(NEGMemTable *tbl, NEGMemHandle handle);

/*
 * ne_gmem_size - return the allocated byte count for 'handle'.
 *
 * Returns the size on success or 0 on failure.
 */
uint32_t ne_gmem_size(const NEGMemTable *tbl, NEGMemHandle handle);

/*
 * ne_gmem_find_block - return a pointer to the block descriptor for 'handle'.
 *
 * Returns NULL if not found.  The pointer is valid until the next alloc or
 * free operation on the table.
 */
NEGMemBlock *ne_gmem_find_block(NEGMemTable *tbl, NEGMemHandle handle);

/*
 * ne_gmem_free_by_owner - free all blocks owned by 'owner_task'.
 *
 * Called during task teardown to reclaim all global memory blocks that were
 * allocated on behalf of 'owner_task'.  Blocks with a different owner (or
 * no owner) are not affected.
 *
 * Returns the number of blocks freed.
 */
uint16_t ne_gmem_free_by_owner(NEGMemTable *tbl, uint16_t owner_task);

/* =========================================================================
 * Public API – local memory (LMEM)
 * ===================================================================== */

/*
 * ne_lmem_heap_init - initialise an empty local heap *heap.
 *
 * Must be called before any other ne_lmem_* function on *heap.
 * Returns NE_MEM_OK or NE_MEM_ERR_NULL.
 */
int ne_lmem_heap_init(NELMemHeap *heap);

/*
 * ne_lmem_heap_free - release all blocks in *heap.
 *
 * Frees the data buffer of every active block.  Does NOT free *heap itself.
 * Safe to call on a zeroed or partially-initialised heap and on NULL.
 */
void ne_lmem_heap_free(NELMemHeap *heap);

/*
 * ne_lmem_alloc - allocate a local memory block.
 *
 * 'flags' : combination of NE_LMEM_* constants.
 * 'size'  : byte count; must be > 0 and ≤ 65535 (16-bit local heap limit).
 *
 * Returns a non-zero NELMemHandle on success or NE_LMEM_HANDLE_INVALID on
 * failure.
 */
NELMemHandle ne_lmem_alloc(NELMemHeap *heap, uint16_t flags, uint16_t size);

/*
 * ne_lmem_free - free a local memory block.
 *
 * Returns NE_MEM_OK, NE_MEM_ERR_BAD_HANDLE, or NE_MEM_ERR_NOT_FOUND.
 */
int ne_lmem_free(NELMemHeap *heap, NELMemHandle handle);

/*
 * ne_lmem_lock - lock a local block and return a pointer to its data.
 *
 * Returns NULL on failure.
 */
void *ne_lmem_lock(NELMemHeap *heap, NELMemHandle handle);

/*
 * ne_lmem_unlock - decrement the lock count of a local block.
 *
 * Returns NE_MEM_OK, NE_MEM_ERR_BAD_HANDLE, or NE_MEM_ERR_NOT_FOUND.
 */
int ne_lmem_unlock(NELMemHeap *heap, NELMemHandle handle);

/*
 * ne_lmem_size - return the byte count of a local block, or 0 on failure.
 */
uint16_t ne_lmem_size(const NELMemHeap *heap, NELMemHandle handle);

/*
 * ne_gmem_flags - return the flags word for a global memory block.
 *
 * Returns the NE_GMEM_* flags that were supplied at allocation time,
 * or 0 if the handle is not found.
 */
uint16_t ne_gmem_flags(const NEGMemTable *tbl, NEGMemHandle handle);

/*
 * ne_gmem_handle - look up a global memory handle by data pointer.
 *
 * Scans the table for a block whose data pointer equals 'ptr'.
 * Returns the handle on success or NE_GMEM_HANDLE_INVALID if not found.
 */
NEGMemHandle ne_gmem_handle(const NEGMemTable *tbl, const void *ptr);

/*
 * ne_gmem_compact - compact global memory (stub).
 *
 * In a real implementation this would coalesce free blocks and return
 * the size of the largest contiguous free block.  This stub returns 0.
 */
uint32_t ne_gmem_compact(NEGMemTable *tbl);

/*
 * ne_lmem_realloc - change the size of a local memory block.
 *
 * Allocates a new buffer of 'new_size' bytes, copies the existing data
 * (up to the smaller of old and new sizes), frees the old buffer, and
 * updates the block descriptor.  'flags' is reserved for future use.
 *
 * Returns the handle on success or NE_LMEM_HANDLE_INVALID on failure.
 */
NELMemHandle ne_lmem_realloc(NELMemHeap *heap, NELMemHandle handle,
                              uint16_t new_size, uint16_t flags);

/*
 * ne_lmem_flags - return the flags word for a local memory block.
 *
 * Returns the NE_LMEM_* flags that were supplied at allocation time,
 * or 0 if the handle is not found.
 */
uint16_t ne_lmem_flags(const NELMemHeap *heap, NELMemHandle handle);

/*
 * ne_lmem_handle - look up a local memory handle by data pointer.
 *
 * Scans the heap for a block whose data pointer equals 'ptr'.
 * Returns the handle on success or NE_LMEM_HANDLE_INVALID if not found.
 */
NELMemHandle ne_lmem_handle(const NELMemHeap *heap, const void *ptr);

/*
 * ne_lmem_compact - compact local memory (stub).
 *
 * In a real implementation this would coalesce free blocks and return
 * the size of the largest contiguous free block.  This stub returns 0.
 */
uint16_t ne_lmem_compact(NELMemHeap *heap);

/*
 * ne_mem_strerror - return a static string describing error code 'err'.
 */
const char *ne_mem_strerror(int err);

#endif /* NE_MEM_H */

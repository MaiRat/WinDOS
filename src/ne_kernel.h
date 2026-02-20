/*
 * ne_kernel.h - Phase 2 KERNEL.EXE API stubs
 *
 * Declares the unified kernel context and the stub functions that
 * correspond to the Windows 3.1 KERNEL.EXE ordinal exports.  Each
 * stub delegates to the lower-level subsystem (ne_mem, ne_task,
 * ne_module, ne_impexp) through a single NEKernelContext that owns
 * pointers to all subsystem tables.
 *
 * File I/O stubs (_lopen, _lclose, _lread, _lwrite, _llseek) wrap
 * standard C <stdio.h> calls on the host side; on a real 16-bit DOS
 * target they should be replaced with INT 21h service calls.
 *
 * The atom table is a simple linear array capped at
 * NE_KERNEL_ATOM_TABLE_CAP entries.  Atom values start at
 * NE_KERNEL_ATOM_BASE (0xC000) following the Windows convention for
 * string atoms.
 *
 * Reference: Microsoft Windows 3.1 SDK – KERNEL.EXE ordinal list.
 */

#ifndef NE_KERNEL_H
#define NE_KERNEL_H

#include "ne_impexp.h"
#include "ne_mem.h"
#include "ne_task.h"
#include "ne_module.h"

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_KERNEL_OK              0
#define NE_KERNEL_ERR_NULL       -1   /* NULL pointer argument               */
#define NE_KERNEL_ERR_INIT       -2   /* context not initialised             */
#define NE_KERNEL_ERR_IO         -3   /* file I/O failure                    */
#define NE_KERNEL_ERR_FULL       -4   /* table at capacity                   */
#define NE_KERNEL_ERR_NOT_FOUND  -5   /* requested item not found            */

/* -------------------------------------------------------------------------
 * File I/O constants
 * ---------------------------------------------------------------------- */
#define NE_KERNEL_OF_READ        0x0000u  /* open for reading                */
#define NE_KERNEL_OF_WRITE       0x0001u  /* open for writing                */
#define NE_KERNEL_OF_READWRITE   0x0002u  /* open for reading and writing    */
#define NE_KERNEL_HFILE_ERROR    (-1)     /* invalid file handle sentinel    */
#define NE_KERNEL_FILE_BEGIN     0        /* seek from beginning of file     */
#define NE_KERNEL_FILE_CURRENT   1        /* seek from current position      */
#define NE_KERNEL_FILE_END       2        /* seek from end of file           */

/* -------------------------------------------------------------------------
 * Export classification
 * ---------------------------------------------------------------------- */
#define NE_KERNEL_CLASS_CRITICAL   0  /* must be implemented for boot        */
#define NE_KERNEL_CLASS_SECONDARY  1  /* needed by most applications         */
#define NE_KERNEL_CLASS_OPTIONAL   2  /* rarely used or safely stubbed       */

/* -------------------------------------------------------------------------
 * Atom table constants
 * ---------------------------------------------------------------------- */
#define NE_KERNEL_ATOM_TABLE_CAP  64u    /* max entries in the atom table    */
#define NE_KERNEL_ATOM_NAME_MAX  256u    /* max atom name length incl. NUL   */
#define NE_KERNEL_ATOM_BASE    0xC000u   /* Windows string-atom base value   */
#define NE_KERNEL_ATOM_INVALID     0u    /* sentinel for no atom             */

/* -------------------------------------------------------------------------
 * KERNEL.EXE ordinal constants  (Windows 3.1)
 * ---------------------------------------------------------------------- */
#define NE_KERNEL_ORD_LOCAL_ALLOC          5
#define NE_KERNEL_ORD_LOCAL_FREE           7
#define NE_KERNEL_ORD_LOCAL_LOCK           8
#define NE_KERNEL_ORD_LOCAL_UNLOCK         9
#define NE_KERNEL_ORD_GLOBAL_ALLOC        15
#define NE_KERNEL_ORD_GLOBAL_REALLOC      16
#define NE_KERNEL_ORD_GLOBAL_FREE         17
#define NE_KERNEL_ORD_GLOBAL_LOCK         18
#define NE_KERNEL_ORD_GLOBAL_UNLOCK       19
#define NE_KERNEL_ORD_WAIT_EVENT          30
#define NE_KERNEL_ORD_POST_EVENT          31
#define NE_KERNEL_ORD_GET_CURRENT_TASK    36
#define NE_KERNEL_ORD_GET_MODULE_HANDLE   47
#define NE_KERNEL_ORD_GET_MODULE_FILENAME 49
#define NE_KERNEL_ORD_GET_PROC_ADDRESS    50
#define NE_KERNEL_ORD_FIND_RESOURCE       60
#define NE_KERNEL_ORD_LOAD_RESOURCE       61
#define NE_KERNEL_ORD_LOCK_RESOURCE       62
#define NE_KERNEL_ORD_INIT_TASK           73
#define NE_KERNEL_ORD_LOPEN               81
#define NE_KERNEL_ORD_LCLOSE              82
#define NE_KERNEL_ORD_LREAD               83
#define NE_KERNEL_ORD_LLSEEK              84
#define NE_KERNEL_ORD_LWRITE              85
#define NE_KERNEL_ORD_LOAD_LIBRARY        96
#define NE_KERNEL_ORD_FREE_LIBRARY       106
#define NE_KERNEL_ORD_YIELD              136
#define NE_KERNEL_ORD_GLOBAL_ADD_ATOM    163
#define NE_KERNEL_ORD_GLOBAL_DELETE_ATOM 164
#define NE_KERNEL_ORD_GLOBAL_FIND_ATOM   165
#define NE_KERNEL_ORD_GLOBAL_GET_ATOM_NAME 166
#define NE_KERNEL_ORD_LOAD_STRING        176

/* -------------------------------------------------------------------------
 * Export catalog entry
 *
 * Describes one KERNEL.EXE export for the static catalog returned by
 * ne_kernel_get_export_catalog().
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t ordinal;                      /* KERNEL.EXE ordinal number      */
    char     name[NE_EXPORT_NAME_MAX];     /* API name                       */
    uint8_t  classification;               /* NE_KERNEL_CLASS_*              */
} NEKernelExportInfo;

/* -------------------------------------------------------------------------
 * Kernel context
 *
 * Aggregates pointers to every subsystem table plus the kernel-private
 * atom table.  Initialise with ne_kernel_init(); release with
 * ne_kernel_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    NEGMemTable   *gmem;       /* global memory table (owned externally)    */
    NELMemHeap    *lmem;       /* local memory heap  (owned externally)     */
    NETaskTable   *tasks;      /* task table          (owned externally)    */
    NEModuleTable *modules;    /* module table        (owned externally)    */
    NEExportTable  exports;    /* KERNEL.EXE export table (owned)           */

    /* Atom table */
    struct {
        uint16_t atom;                            /* atom value             */
        char     name[NE_KERNEL_ATOM_NAME_MAX];   /* atom string           */
    } atoms[NE_KERNEL_ATOM_TABLE_CAP];
    uint16_t atom_count;       /* number of atoms currently registered      */
    uint16_t next_atom;        /* next atom value to assign                 */

    int      initialized;      /* non-zero after successful init            */
} NEKernelContext;

/* =========================================================================
 * Public API – initialisation
 * ===================================================================== */

/*
 * ne_kernel_init - initialise the kernel context.
 *
 * Stores the subsystem pointers, zeroes the atom table, and marks the
 * context as initialised.  All pointer arguments must be non-NULL.
 *
 * Returns NE_KERNEL_OK on success or NE_KERNEL_ERR_NULL.
 */
int ne_kernel_init(NEKernelContext *ctx,
                   NEGMemTable     *gmem,
                   NELMemHeap      *lmem,
                   NETaskTable     *tasks,
                   NEModuleTable   *modules);

/*
 * ne_kernel_free - release resources owned by the kernel context.
 *
 * Frees the internal export table and zeroes the context.  Does NOT free
 * the subsystem tables (gmem, lmem, tasks, modules) because they are
 * owned externally.  Safe to call on a zeroed context or NULL.
 */
void ne_kernel_free(NEKernelContext *ctx);

/* =========================================================================
 * Public API – export catalog
 * ===================================================================== */

/*
 * ne_kernel_get_export_catalog - return the static KERNEL.EXE export list.
 *
 * Sets *out to point to the internal catalog array and *out_count to the
 * number of entries.  The returned pointer is valid for the lifetime of
 * the program (static storage).
 *
 * Returns NE_KERNEL_OK or NE_KERNEL_ERR_NULL.
 */
int ne_kernel_get_export_catalog(const NEKernelExportInfo **out,
                                 uint16_t                  *out_count);

/* =========================================================================
 * Public API – export registration
 * ===================================================================== */

/*
 * ne_kernel_register_exports - populate the NEExportTable in ctx->exports.
 *
 * Builds export entries for every KERNEL.EXE ordinal so that other
 * modules can resolve imports against the kernel via the standard
 * ne_export_find_by_ordinal / ne_export_find_by_name helpers.
 *
 * Returns NE_KERNEL_OK or NE_KERNEL_ERR_INIT.
 */
int ne_kernel_register_exports(NEKernelContext *ctx);

/* =========================================================================
 * Public API – file I/O
 * ===================================================================== */

/*
 * ne_kernel_lopen - open a file and return a file handle.
 *
 * 'mode' is one of NE_KERNEL_OF_READ, NE_KERNEL_OF_WRITE, or
 * NE_KERNEL_OF_READWRITE.
 *
 * Returns a non-negative file handle on success or NE_KERNEL_HFILE_ERROR
 * on failure.
 */
int ne_kernel_lopen(NEKernelContext *ctx, const char *path, uint16_t mode);

/*
 * ne_kernel_lclose - close a previously opened file handle.
 *
 * Returns NE_KERNEL_OK or NE_KERNEL_ERR_IO.
 */
int ne_kernel_lclose(NEKernelContext *ctx, int hFile);

/*
 * ne_kernel_lread - read bytes from a file.
 *
 * Reads up to 'count' bytes into 'buf'.  Returns the number of bytes
 * actually read, or NE_KERNEL_HFILE_ERROR on failure.
 */
int ne_kernel_lread(NEKernelContext *ctx, int hFile,
                    void *buf, uint16_t count);

/*
 * ne_kernel_lwrite - write bytes to a file.
 *
 * Writes up to 'count' bytes from 'buf'.  Returns the number of bytes
 * actually written, or NE_KERNEL_HFILE_ERROR on failure.
 */
int ne_kernel_lwrite(NEKernelContext *ctx, int hFile,
                     const void *buf, uint16_t count);

/*
 * ne_kernel_llseek - reposition the file pointer.
 *
 * 'origin' is one of NE_KERNEL_FILE_BEGIN, NE_KERNEL_FILE_CURRENT, or
 * NE_KERNEL_FILE_END.
 *
 * Returns the new file position, or NE_KERNEL_HFILE_ERROR on failure.
 */
long ne_kernel_llseek(NEKernelContext *ctx, int hFile,
                      long offset, int origin);

/* =========================================================================
 * Public API – module management
 * ===================================================================== */

/*
 * ne_kernel_get_module_handle - return the handle for a loaded module.
 *
 * Returns a non-zero handle on success or 0 if not found.
 */
uint16_t ne_kernel_get_module_handle(NEKernelContext *ctx, const char *name);

/*
 * ne_kernel_get_module_filename - copy the filename of a module into 'buf'.
 *
 * Copies at most 'size' bytes (including the NUL terminator).
 * Returns NE_KERNEL_OK or NE_KERNEL_ERR_NOT_FOUND.
 */
int ne_kernel_get_module_filename(NEKernelContext *ctx, uint16_t hModule,
                                  char *buf, int size);

/*
 * ne_kernel_get_proc_address - look up a named export in a module.
 *
 * Returns a packed seg:offset value on success or 0 on failure.
 */
uint32_t ne_kernel_get_proc_address(NEKernelContext *ctx, uint16_t hModule,
                                     const char *name);

/*
 * ne_kernel_load_library - load a module by name and return its handle.
 *
 * Returns a non-zero handle on success or 0 on failure.
 */
uint16_t ne_kernel_load_library(NEKernelContext *ctx, const char *name);

/*
 * ne_kernel_free_library - decrement a module's reference count.
 *
 * The module is unloaded when its count reaches zero.
 */
void ne_kernel_free_library(NEKernelContext *ctx, uint16_t hModule);

/* =========================================================================
 * Public API – global memory
 * ===================================================================== */

/*
 * ne_kernel_global_alloc - allocate a global memory block.
 *
 * 'flags' is a combination of NE_GMEM_* constants.
 * Returns a non-zero handle on success or NE_GMEM_HANDLE_INVALID.
 */
NEGMemHandle ne_kernel_global_alloc(NEKernelContext *ctx,
                                     uint16_t flags, uint32_t size);

/*
 * ne_kernel_global_free - free a global memory block.
 *
 * Returns NE_KERNEL_OK or a negative error code.
 */
int ne_kernel_global_free(NEKernelContext *ctx, NEGMemHandle handle);

/*
 * ne_kernel_global_lock - lock a global block and return a pointer.
 *
 * Returns a pointer to the block data, or NULL on failure.
 */
void *ne_kernel_global_lock(NEKernelContext *ctx, NEGMemHandle handle);

/*
 * ne_kernel_global_unlock - decrement the lock count of a global block.
 *
 * Returns NE_KERNEL_OK or a negative error code.
 */
int ne_kernel_global_unlock(NEKernelContext *ctx, NEGMemHandle handle);

/*
 * ne_kernel_global_realloc - change the size of an existing global block.
 *
 * Returns the (possibly new) handle on success or NE_GMEM_HANDLE_INVALID.
 */
NEGMemHandle ne_kernel_global_realloc(NEKernelContext *ctx,
                                       NEGMemHandle handle,
                                       uint32_t new_size, uint16_t flags);

/* =========================================================================
 * Public API – local memory
 * ===================================================================== */

/*
 * ne_kernel_local_alloc - allocate a local memory block.
 *
 * 'flags' is a combination of NE_LMEM_* constants.
 * Returns a non-zero handle on success or NE_LMEM_HANDLE_INVALID.
 */
NELMemHandle ne_kernel_local_alloc(NEKernelContext *ctx,
                                    uint16_t flags, uint16_t size);

/*
 * ne_kernel_local_free - free a local memory block.
 *
 * Returns NE_KERNEL_OK or a negative error code.
 */
int ne_kernel_local_free(NEKernelContext *ctx, NELMemHandle handle);

/*
 * ne_kernel_local_lock - lock a local block and return a pointer.
 *
 * Returns a pointer to the block data, or NULL on failure.
 */
void *ne_kernel_local_lock(NEKernelContext *ctx, NELMemHandle handle);

/*
 * ne_kernel_local_unlock - decrement the lock count of a local block.
 *
 * Returns NE_KERNEL_OK or a negative error code.
 */
int ne_kernel_local_unlock(NEKernelContext *ctx, NELMemHandle handle);

/* =========================================================================
 * Public API – task / process management
 * ===================================================================== */

/*
 * ne_kernel_get_current_task - return the handle of the running task.
 *
 * Returns a non-zero handle or 0 if no task is active.
 */
uint16_t ne_kernel_get_current_task(NEKernelContext *ctx);

/*
 * ne_kernel_yield - voluntarily surrender the CPU.
 *
 * Transfers control to the cooperative scheduler.
 */
void ne_kernel_yield(NEKernelContext *ctx);

/*
 * ne_kernel_init_task - perform initial task setup for the current task.
 *
 * Returns NE_KERNEL_OK or a negative error code.
 */
int ne_kernel_init_task(NEKernelContext *ctx);

/*
 * ne_kernel_wait_event - block until an event is posted for 'hTask'.
 *
 * Returns NE_KERNEL_OK or a negative error code.
 */
int ne_kernel_wait_event(NEKernelContext *ctx, uint16_t hTask);

/*
 * ne_kernel_post_event - post an event to wake 'hTask'.
 *
 * Returns NE_KERNEL_OK or a negative error code.
 */
int ne_kernel_post_event(NEKernelContext *ctx, uint16_t hTask);

/* =========================================================================
 * Public API – string / resource stubs
 * ===================================================================== */

/*
 * ne_kernel_load_string - load a string resource.
 *
 * Copies at most 'buf_size' bytes (including NUL) into 'buf'.
 * Returns the number of characters copied (excluding NUL) or 0 on failure.
 */
int ne_kernel_load_string(NEKernelContext *ctx, uint16_t hModule,
                           uint16_t uID, char *buf, int buf_size);

/*
 * ne_kernel_find_resource - locate a resource in a module.
 *
 * Returns a non-zero resource info handle on success or 0 on failure.
 */
uint32_t ne_kernel_find_resource(NEKernelContext *ctx, uint16_t hModule,
                                  const char *name, const char *type);

/*
 * ne_kernel_load_resource - load a resource into memory.
 *
 * Returns a non-zero resource data handle on success or 0 on failure.
 */
uint16_t ne_kernel_load_resource(NEKernelContext *ctx, uint16_t hModule,
                                  uint32_t hResInfo);

/*
 * ne_kernel_lock_resource - lock a loaded resource and return a pointer.
 *
 * Returns a pointer to the resource data, or NULL on failure.
 */
void *ne_kernel_lock_resource(NEKernelContext *ctx, uint16_t hResData);

/* =========================================================================
 * Public API – atom table
 * ===================================================================== */

/*
 * ne_kernel_global_add_atom - add a string to the global atom table.
 *
 * If 'name' already exists, returns the existing atom value.  Otherwise
 * assigns a new atom value starting from NE_KERNEL_ATOM_BASE.
 *
 * Returns the atom value on success or NE_KERNEL_ATOM_INVALID on failure.
 */
uint16_t ne_kernel_global_add_atom(NEKernelContext *ctx, const char *name);

/*
 * ne_kernel_global_find_atom - look up an atom by name.
 *
 * Returns the atom value or NE_KERNEL_ATOM_INVALID if not found.
 */
uint16_t ne_kernel_global_find_atom(NEKernelContext *ctx, const char *name);

/*
 * ne_kernel_global_get_atom_name - copy the name of an atom into 'buf'.
 *
 * Copies at most 'size' bytes (including NUL).
 * Returns NE_KERNEL_OK or NE_KERNEL_ERR_NOT_FOUND.
 */
int ne_kernel_global_get_atom_name(NEKernelContext *ctx, uint16_t atom,
                                    char *buf, int size);

/*
 * ne_kernel_global_delete_atom - remove an atom from the table.
 *
 * Returns NE_KERNEL_OK or NE_KERNEL_ERR_NOT_FOUND.
 */
int ne_kernel_global_delete_atom(NEKernelContext *ctx, uint16_t atom);

/* =========================================================================
 * Public API – error string
 * ===================================================================== */

/*
 * ne_kernel_strerror - return a static string describing error code 'err'.
 */
const char *ne_kernel_strerror(int err);

#endif /* NE_KERNEL_H */

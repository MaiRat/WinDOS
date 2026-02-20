/*
 * ne_module.h - NE (New Executable) global module table
 *
 * Manages the lifecycle of loaded NE modules: registration, duplicate-load
 * detection, reference counting, inter-module dependency tracking, and
 * orderly unloading.
 *
 * This module implements Step 4 of the WinDOS kernel-replacement roadmap.
 * The host-side implementation uses standard C library memory functions;
 * on a real 16-bit DOS target these would be replaced by the appropriate
 * Watcom memory intrinsics or DOS INT 21h service calls.
 *
 * Reference: Microsoft "New Executable" format specification.
 */

#ifndef NE_MODULE_H
#define NE_MODULE_H

#include "ne_parser.h"
#include "ne_loader.h"

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_MOD_OK              0
#define NE_MOD_ERR_NULL       -1   /* NULL pointer argument                */
#define NE_MOD_ERR_ALLOC      -2   /* memory allocation failure            */
#define NE_MOD_ERR_NOT_FOUND  -3   /* handle or name not found             */
#define NE_MOD_ERR_FULL       -4   /* module table is at capacity          */
#define NE_MOD_ERR_DEP_FULL   -5   /* dependency list is at capacity       */
#define NE_MOD_ERR_IN_USE     -6   /* module still referenced by others    */
#define NE_MOD_ERR_BAD_HANDLE -7   /* handle value is invalid (0)          */

/* -------------------------------------------------------------------------
 * Configuration constants
 *
 * NE_MOD_TABLE_CAP : default initial capacity of the module table.
 * NE_MOD_NAME_MAX  : maximum module name length including the NUL byte.
 *                    Matches the 8-character DOS base-name limit.
 * NE_MOD_DEP_MAX   : maximum number of direct dependencies per module.
 * ---------------------------------------------------------------------- */
#define NE_MOD_TABLE_CAP  64u
#define NE_MOD_NAME_MAX    9u   /* 8 chars + NUL */
#define NE_MOD_DEP_MAX    16u

/* -------------------------------------------------------------------------
 * Module handle type
 *
 * A non-zero uint16_t uniquely identifies a loaded module for the lifetime
 * of the table.  NE_MOD_HANDLE_INVALID (0) is the null / sentinel value.
 * ---------------------------------------------------------------------- */
typedef uint16_t NEModuleHandle;

#define NE_MOD_HANDLE_INVALID ((NEModuleHandle)0)

/* -------------------------------------------------------------------------
 * Module entry
 *
 * Owns the parser and loader contexts; they are freed when the entry is
 * removed from the table.  Callers must NOT call ne_free() or
 * ne_loader_free() on contexts that have been passed to ne_mod_load().
 * ---------------------------------------------------------------------- */
typedef struct {
    NEModuleHandle  handle;               /* unique 1-based handle (0 = free) */
    char            name[NE_MOD_NAME_MAX];/* NUL-terminated module name       */
    uint16_t        ref_count;            /* reference count                   */
    NEParserContext parser;               /* owned parser context              */
    NELoaderContext loader;               /* owned loader context              */
    NEModuleHandle  deps[NE_MOD_DEP_MAX]; /* direct dependency handles        */
    uint16_t        dep_count;            /* number of entries in deps[]       */
} NEModuleEntry;

/* -------------------------------------------------------------------------
 * Module table
 *
 * Holds all currently loaded modules.  Initialise with ne_mod_table_init()
 * before use and release with ne_mod_table_free() when done.
 * ---------------------------------------------------------------------- */
typedef struct {
    NEModuleEntry *entries;    /* heap-allocated array of capacity entries   */
    uint16_t       capacity;   /* total slots allocated                      */
    uint16_t       count;      /* number of occupied (active) slots          */
    uint16_t       next_handle;/* next handle value to assign (starts at 1)  */
} NEModuleTable;

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/*
 * ne_mod_table_init - initialise *tbl with 'capacity' pre-allocated slots.
 *
 * 'capacity' must be > 0.  Pass NE_MOD_TABLE_CAP for the default size.
 * Returns NE_MOD_OK on success; *tbl is valid and must be freed with
 * ne_mod_table_free() when no longer needed.
 */
int ne_mod_table_init(NEModuleTable *tbl, uint16_t capacity);

/*
 * ne_mod_table_free - release all resources owned by *tbl.
 *
 * Calls ne_free() and ne_loader_free() on every active entry, then frees
 * the entries array.  Safe to call on a zeroed or partially-initialised
 * table and on NULL.
 */
void ne_mod_table_free(NEModuleTable *tbl);

/*
 * ne_mod_load - register a module or return an existing handle on re-load.
 *
 * If a module named 'name' already exists in *tbl:
 *   - Its reference count is incremented.
 *   - *out_handle is set to the existing handle.
 *   - The caller-supplied *parser and *loader are NOT consumed; the caller
 *     remains responsible for freeing them.
 *
 * If no module named 'name' exists:
 *   - A new entry is created.
 *   - Ownership of *parser and *loader is TRANSFERRED to the table; the
 *     caller must NOT call ne_free() or ne_loader_free() on them afterwards.
 *   - Reference count is set to 1.
 *   - *out_handle is set to the new handle.
 *
 * Returns NE_MOD_OK on success or NE_MOD_ERR_FULL / NE_MOD_ERR_ALLOC on
 * failure.  On failure *out_handle is set to NE_MOD_HANDLE_INVALID and
 * the caller retains ownership of *parser and *loader.
 */
int ne_mod_load(NEModuleTable  *tbl,
                const char     *name,
                NEParserContext *parser,
                NELoaderContext *loader,
                NEModuleHandle  *out_handle);

/*
 * ne_mod_addref - increment the reference count of the module identified
 * by 'handle'.
 *
 * Returns NE_MOD_OK, NE_MOD_ERR_BAD_HANDLE, or NE_MOD_ERR_NOT_FOUND.
 */
int ne_mod_addref(NEModuleTable *tbl, NEModuleHandle handle);

/*
 * ne_mod_unload - decrement the reference count of 'handle'.
 *
 * If the count reaches zero AND no other loaded module lists 'handle' as a
 * dependency, the entry is removed from the table and its owned parser and
 * loader contexts are freed.
 *
 * Returns NE_MOD_OK on success, NE_MOD_ERR_IN_USE if another module still
 * depends on this one (count not yet zero is not an error in this case;
 * the count is still decremented), or another NE_MOD_ERR_* code.
 */
int ne_mod_unload(NEModuleTable *tbl, NEModuleHandle handle);

/*
 * ne_mod_add_dep - record that module 'handle' directly depends on
 * module 'dep_handle'.
 *
 * Both handles must be valid and active.  Duplicate dependency entries are
 * silently ignored.
 *
 * Returns NE_MOD_OK, NE_MOD_ERR_DEP_FULL, or NE_MOD_ERR_NOT_FOUND.
 */
int ne_mod_add_dep(NEModuleTable *tbl,
                   NEModuleHandle handle,
                   NEModuleHandle dep_handle);

/*
 * ne_mod_find - look up a module by name.
 *
 * Returns the handle of the first active entry whose name matches 'name'
 * (case-sensitive), or NE_MOD_HANDLE_INVALID if not found.
 */
NEModuleHandle ne_mod_find(const NEModuleTable *tbl, const char *name);

/*
 * ne_mod_get - retrieve a pointer to the entry for 'handle'.
 *
 * Returns a pointer into the table's internal storage, or NULL if 'handle'
 * is NE_MOD_HANDLE_INVALID or not active.  The pointer is valid only until
 * the next ne_mod_load() or ne_mod_unload() call.
 */
NEModuleEntry *ne_mod_get(NEModuleTable *tbl, NEModuleHandle handle);

/*
 * ne_mod_strerror - return a static string describing error code 'err'.
 */
const char *ne_mod_strerror(int err);

#endif /* NE_MODULE_H */

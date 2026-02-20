/*
 * ne_impexp.h - NE (New Executable) import/export resolution
 *
 * Implements Step 5 of the WinDOS kernel-replacement roadmap:
 *   - Build per-module export tables indexed by ordinal and by name.
 *   - Implement ordinal-based and name-based import resolution against
 *     loaded-module export tables.
 *   - Maintain a shared stub-tracking table for imports whose target module
 *     is not yet loaded, with milestone-based replacement tracking.
 *
 * The host-side implementation uses standard C library memory functions;
 * on a real 16-bit DOS target these would be replaced by the appropriate
 * Watcom memory intrinsics or DOS INT 21h service calls.
 *
 * Reference: Microsoft "New Executable" format specification.
 */

#ifndef NE_IMPEXP_H
#define NE_IMPEXP_H

#include "ne_parser.h"

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_IMPEXP_OK              0
#define NE_IMPEXP_ERR_NULL       -1   /* NULL pointer argument               */
#define NE_IMPEXP_ERR_ALLOC      -2   /* memory allocation failure           */
#define NE_IMPEXP_ERR_IO         -3   /* data reads outside buffer bounds    */
#define NE_IMPEXP_ERR_UNRESOLVED -4   /* import target not found             */
#define NE_IMPEXP_ERR_FULL       -5   /* stub table at capacity              */

/* -------------------------------------------------------------------------
 * Export table
 * ---------------------------------------------------------------------- */

/*
 * Maximum length of an export symbol name, including the NUL terminator.
 * The NE format uses a 1-byte length prefix (max 255 chars), so 256 bytes
 * is the safe upper bound.
 */
#define NE_EXPORT_NAME_MAX  256u

/*
 * NEExportEntry - one exported symbol from a module.
 *
 * 'segment' is the 0-based index into the module's segment array.
 * 'name' is the empty string ("") for ordinal-only exports.
 */
typedef struct {
    uint16_t ordinal;                  /* 1-based ordinal number               */
    uint16_t segment;                  /* 0-based segment index                */
    uint16_t offset;                   /* byte offset within that segment      */
    char     name[NE_EXPORT_NAME_MAX]; /* export name, or empty string         */
} NEExportEntry;

/*
 * NEExportTable - the complete export table for one loaded module.
 *
 * Entries are kept in ascending ordinal order.
 * Build with ne_export_build(); release with ne_export_free().
 */
typedef struct {
    NEExportEntry *entries;  /* heap-allocated array, sorted by ordinal  */
    uint16_t       count;    /* number of valid entries                  */
} NEExportTable;

/* -------------------------------------------------------------------------
 * Stub table
 * ---------------------------------------------------------------------- */

/*
 * Maximum byte length of the module name field inside a stub entry,
 * including the NUL terminator.  Matches the 8-character DOS base-name
 * limit (8 chars + NUL).
 */
#define NE_STUB_MOD_NAME_MAX    9u

/* Maximum length of the human-readable stub-behavior description + NUL. */
#define NE_STUB_BEHAVIOR_MAX   64u

/* Maximum length of the replacement-milestone label + NUL. */
#define NE_STUB_MILESTONE_MAX  32u

/* Default initial capacity for a new stub table. */
#define NE_STUB_TABLE_CAP     128u

/*
 * NEStubEntry - one entry in the shared stub-tracking table.
 *
 * Records an import that could not be resolved at load time because the
 * target module was not yet loaded.  Fields:
 *   module_name : name of the module that owns this stub
 *   api_name    : exported symbol name (empty string if ordinal-only)
 *   ordinal     : ordinal number of the unresolved import
 *   behavior    : human-readable description of what the stub does
 *   milestone   : step label at which the stub will be replaced
 *   removed     : set to 1 by ne_stub_replace() once the real address
 *                 is known; entry is kept in the table for inspection
 */
typedef struct {
    char     module_name[NE_STUB_MOD_NAME_MAX]; /* owning module name         */
    char     api_name[NE_EXPORT_NAME_MAX];       /* API name or empty string  */
    uint16_t ordinal;                             /* ordinal number            */
    char     behavior[NE_STUB_BEHAVIOR_MAX];     /* stub behavior description */
    char     milestone[NE_STUB_MILESTONE_MAX];   /* replacement milestone     */
    int      removed;                             /* non-zero once replaced    */
} NEStubEntry;

/*
 * NEStubTable - the shared stub-tracking table.
 *
 * Initialise with ne_stub_table_init(); release with ne_stub_table_free().
 */
typedef struct {
    NEStubEntry *entries;  /* heap-allocated array                      */
    uint16_t     count;    /* number of entries currently registered    */
    uint16_t     capacity; /* total slots allocated                     */
} NEStubTable;

/* -------------------------------------------------------------------------
 * Public API – export table
 * ---------------------------------------------------------------------- */

/*
 * ne_export_build - build the export table for a module from its file image.
 *
 * Parses the entry table (from parser->entry_data / parser->entry_size) and
 * the resident name table (read from buf/len at the absolute offset
 * parser->ne_offset + header.resident_name_table_offset) to populate *tbl
 * with all exported symbols.
 *
 * 'buf' may be NULL; in that case the entry table is still parsed but
 * all export names will be empty strings.
 *
 * Returns NE_IMPEXP_OK on success (tbl->count may be 0 when the module has
 * no exports); caller must call ne_export_free() when done.
 * On failure *tbl is zeroed and no memory is leaked.
 */
int ne_export_build(const uint8_t       *buf,
                    size_t               len,
                    const NEParserContext *parser,
                    NEExportTable        *tbl);

/*
 * ne_export_free - release all heap memory owned by *tbl.
 * Safe to call on a zeroed context or NULL.
 */
void ne_export_free(NEExportTable *tbl);

/*
 * ne_export_find_by_ordinal - look up an export by ordinal.
 *
 * Returns a pointer to the matching entry, or NULL if not found.
 * The pointer is valid until ne_export_free() is called on *tbl.
 */
const NEExportEntry *ne_export_find_by_ordinal(const NEExportTable *tbl,
                                                uint16_t             ordinal);

/*
 * ne_export_find_by_name - look up an export by name (case-sensitive).
 *
 * Returns a pointer to the matching entry, or NULL if not found.
 */
const NEExportEntry *ne_export_find_by_name(const NEExportTable *tbl,
                                             const char          *name);

/* -------------------------------------------------------------------------
 * Public API – import resolution
 * ---------------------------------------------------------------------- */

/*
 * ne_import_resolve_ordinal - resolve an ordinal-based import.
 *
 * Looks up 'ordinal' in 'export_tbl'.  On success, sets *out_seg and
 * *out_off to the target address and returns NE_IMPEXP_OK.
 *
 * Returns NE_IMPEXP_ERR_UNRESOLVED when export_tbl is NULL or the ordinal
 * is not present in the table.
 * Returns NE_IMPEXP_ERR_NULL when out_seg or out_off is NULL.
 */
int ne_import_resolve_ordinal(const NEExportTable *export_tbl,
                               uint16_t             ordinal,
                               uint16_t            *out_seg,
                               uint16_t            *out_off);

/*
 * ne_import_resolve_name - resolve a name-based import (case-sensitive).
 *
 * Looks up 'api_name' in 'export_tbl'.  On success, sets *out_seg and
 * *out_off and returns NE_IMPEXP_OK.
 *
 * Returns NE_IMPEXP_ERR_UNRESOLVED when export_tbl is NULL or the name is
 * not found.  Returns NE_IMPEXP_ERR_NULL for NULL pointer arguments.
 */
int ne_import_resolve_name(const NEExportTable *export_tbl,
                            const char          *api_name,
                            uint16_t            *out_seg,
                            uint16_t            *out_off);

/* -------------------------------------------------------------------------
 * Public API – stub table
 * ---------------------------------------------------------------------- */

/*
 * ne_stub_table_init - initialise *tbl with 'capacity' pre-allocated slots.
 *
 * 'capacity' must be > 0.  Pass NE_STUB_TABLE_CAP for the default size.
 * Returns NE_IMPEXP_OK on success; call ne_stub_table_free() when done.
 */
int ne_stub_table_init(NEStubTable *tbl, uint16_t capacity);

/*
 * ne_stub_table_free - release all resources owned by *tbl.
 * Safe to call on a zeroed context or NULL.
 */
void ne_stub_table_free(NEStubTable *tbl);

/*
 * ne_stub_register - register a stub for an unresolved import.
 *
 * Records that the import identified by (module_name, ordinal) could not be
 * resolved at load time.  If an entry for the same (module_name, ordinal)
 * already exists it is silently accepted without creating a duplicate.
 *
 * 'api_name'  : exported symbol name, or empty string if ordinal-only.
 * 'behavior'  : human-readable description of what the stub does.
 * 'milestone' : label of the step at which the stub will be replaced.
 *
 * Returns NE_IMPEXP_OK on success, NE_IMPEXP_ERR_FULL when the table is at
 * capacity, or NE_IMPEXP_ERR_NULL for NULL pointer arguments.
 */
int ne_stub_register(NEStubTable *tbl,
                     const char  *module_name,
                     const char  *api_name,
                     uint16_t     ordinal,
                     const char  *behavior,
                     const char  *milestone);

/*
 * ne_stub_replace - mark a stub as replaced when the real address is known.
 *
 * Finds the first stub entry for (module_name, ordinal) that has not yet
 * been replaced and sets its 'removed' field to 1.  The entry is kept in
 * the table so callers can inspect the replacement history.
 *
 * Returns NE_IMPEXP_OK if an entry was found and marked, or
 * NE_IMPEXP_ERR_UNRESOLVED if no active (non-removed) matching entry exists.
 * Returns NE_IMPEXP_ERR_NULL for NULL pointer arguments.
 */
int ne_stub_replace(NEStubTable *tbl,
                    const char  *module_name,
                    uint16_t     ordinal);

/*
 * ne_stub_find_by_ordinal - find a stub entry by (module_name, ordinal).
 *
 * Returns a pointer to the first matching entry (regardless of removed
 * status), or NULL if no entry exists.
 */
const NEStubEntry *ne_stub_find_by_ordinal(const NEStubTable *tbl,
                                            const char        *module_name,
                                            uint16_t           ordinal);

/*
 * ne_stub_find_by_name - find a stub entry by (module_name, api_name).
 *
 * Returns a pointer to the first matching entry (regardless of removed
 * status), or NULL if no entry exists or api_name is empty.
 */
const NEStubEntry *ne_stub_find_by_name(const NEStubTable *tbl,
                                         const char        *module_name,
                                         const char        *api_name);

/*
 * ne_impexp_strerror - return a static string describing error code 'err'.
 */
const char *ne_impexp_strerror(int err);

#endif /* NE_IMPEXP_H */

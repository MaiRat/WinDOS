/*
 * ne_impexp.c - NE (New Executable) import/export resolution implementation
 *
 * Implements Step 5 of the WinDOS kernel-replacement roadmap.
 * Parses the NE entry table and resident name table to build per-module
 * export tables, performs ordinal- and name-based import resolution, and
 * maintains a shared stub-tracking table for unresolved imports.
 */

#include "ne_impexp.h"

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Entry table parsing constants
 * ---------------------------------------------------------------------- */

/* Bundle type values stored in the second byte of each bundle header */
#define BUNDLE_END      0u     /* count == 0: end of entry table            */
#define BUNDLE_NULL     0u     /* type == 0: count null (no-address) entries */
#define BUNDLE_MOVABLE  0xFFu  /* type == 0xFF: movable-segment entries      */
/* type 1..254: fixed-segment entries; type IS the 1-based segment number   */

/* Per-entry byte sizes */
#define FIXED_ENTRY_SIZE   3u  /* flags(1) + offset_lo(1) + offset_hi(1)    */
#define MOVABLE_ENTRY_SIZE 6u  /* flags(1)+0xCD(1)+0x3F(1)+seg(1)+off(2)   */

/* -------------------------------------------------------------------------
 * Internal helper – read a little-endian uint16
 * ---------------------------------------------------------------------- */
static uint16_t read_u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

/* -------------------------------------------------------------------------
 * Internal helper – count exportable (non-null) entries in the entry table
 *
 * Returns the count of entries that will actually appear in the export table
 * (i.e. entries from fixed-segment or movable-segment bundles only).
 * ---------------------------------------------------------------------- */
static uint16_t count_export_entries(const uint8_t *data, uint16_t size)
{
    uint16_t pos   = 0;
    uint16_t total = 0;
    uint8_t  count, type;
    uint32_t bundle_bytes;

    while (pos + 2u <= (uint16_t)size) {
        count = data[pos];
        type  = data[pos + 1u];
        pos  += 2u;

        if (count == 0u)
            break; /* end of entry table */

        if (type == BUNDLE_NULL) {
            /* Null entries: ordinals are consumed but have no address */
            continue;
        } else if (type == BUNDLE_MOVABLE) {
            bundle_bytes = (uint32_t)count * MOVABLE_ENTRY_SIZE;
        } else {
            bundle_bytes = (uint32_t)count * FIXED_ENTRY_SIZE;
        }

        if ((uint32_t)pos + bundle_bytes > (uint32_t)size)
            break; /* truncated – stop */

        total = (uint16_t)(total + count);
        pos   = (uint16_t)(pos   + (uint16_t)bundle_bytes);
    }

    return total;
}

/* -------------------------------------------------------------------------
 * Internal helper – fill 'out' with entries parsed from the entry table
 *
 * 'out' must have at least count_export_entries() pre-allocated slots.
 * Returns the number of entries actually written.
 * ---------------------------------------------------------------------- */
static uint16_t fill_export_entries(const uint8_t *data,
                                     uint16_t        size,
                                     NEExportEntry  *out)
{
    uint16_t pos     = 0;
    uint16_t ordinal = 1u;
    uint16_t written = 0u;
    uint8_t  count, type;
    uint8_t  i;

    while (pos + 2u <= (uint16_t)size) {
        count = data[pos];
        type  = data[pos + 1u];
        pos  += 2u;

        if (count == 0u)
            break;

        if (type == BUNDLE_NULL) {
            /* Skip count null ordinals; no per-entry bytes follow */
            ordinal = (uint16_t)(ordinal + count);
            continue;
        }

        if (type == BUNDLE_MOVABLE) {
            /* 6-byte movable entries */
            if ((uint32_t)pos + (uint32_t)count * MOVABLE_ENTRY_SIZE
                    > (uint32_t)size)
                break;

            for (i = 0u; i < count; i++) {
                /* layout: flags(1) | 0xCD(1) | 0x3F(1) | seg(1) | off(2) */
                uint8_t  seg_1based = data[pos + 3u];
                uint16_t off        = read_u16le(data + pos + 4u);

                out[written].ordinal = ordinal;
                /* segment is 1-based in the file; convert to 0-based */
                out[written].segment = (seg_1based > 0u)
                                       ? (uint16_t)(seg_1based - 1u)
                                       : 0u;
                out[written].offset  = off;
                out[written].name[0] = '\0';

                written++;
                ordinal++;
                pos = (uint16_t)(pos + MOVABLE_ENTRY_SIZE);
            }
        } else {
            /* Fixed-segment entries; type IS the 1-based segment number */
            uint16_t seg0 = (type > 0u) ? (uint16_t)(type - 1u) : 0u;

            if ((uint32_t)pos + (uint32_t)count * FIXED_ENTRY_SIZE
                    > (uint32_t)size)
                break;

            for (i = 0u; i < count; i++) {
                /* layout: flags(1) | offset_lo(1) | offset_hi(1) */
                uint16_t off = read_u16le(data + pos + 1u);

                out[written].ordinal = ordinal;
                out[written].segment = seg0;
                out[written].offset  = off;
                out[written].name[0] = '\0';

                written++;
                ordinal++;
                pos = (uint16_t)(pos + FIXED_ENTRY_SIZE);
            }
        }
    }

    return written;
}

/* -------------------------------------------------------------------------
 * Internal helper – attach names from the resident name table
 *
 * Scans the resident name table at buf[rnt_abs .. rnt_end) and for each
 * (name, ordinal) pair sets the 'name' field of the matching entry in
 * 'entries[0..count)'.
 *
 * The first entry in the resident name table is the module name (ordinal 0)
 * and is skipped.  All subsequent entries with ordinal > 0 are exports.
 * ---------------------------------------------------------------------- */
static void attach_export_names(const uint8_t *buf,
                                 uint32_t       rnt_abs,
                                 uint32_t       rnt_end,
                                 NEExportEntry *entries,
                                 uint16_t       count)
{
    uint32_t pos = rnt_abs;
    uint8_t  name_len;
    uint16_t ordinal;
    uint16_t i;
    uint8_t  copy_len;
    char     name_buf[NE_EXPORT_NAME_MAX];
    int      first = 1; /* flag to skip the module-name entry (ordinal 0) */

    while (pos < rnt_end) {
        name_len = buf[pos++];
        if (name_len == 0u)
            break; /* end-of-table marker */

        /* Check that the name + 2-byte ordinal fit within the table */
        if (pos + (uint32_t)name_len + 2u > rnt_end)
            break;

        /* Copy the name */
        copy_len = (name_len < (uint8_t)(NE_EXPORT_NAME_MAX - 1u))
                   ? name_len
                   : (uint8_t)(NE_EXPORT_NAME_MAX - 1u);
        memcpy(name_buf, buf + pos, copy_len);
        name_buf[copy_len] = '\0';
        pos += name_len;

        /* Read the 2-byte ordinal */
        ordinal = read_u16le(buf + pos);
        pos    += 2u;

        /* Skip the module-name entry (ordinal 0 by convention) */
        if (first) {
            first = 0;
            continue;
        }

        if (ordinal == 0u)
            continue; /* defensive: skip any other ordinal-0 entries */

        /* Find the matching entry in the export table and set the name */
        for (i = 0u; i < count; i++) {
            if (entries[i].ordinal == ordinal) {
                strncpy(entries[i].name, name_buf, NE_EXPORT_NAME_MAX - 1u);
                entries[i].name[NE_EXPORT_NAME_MAX - 1u] = '\0';
                break;
            }
        }
    }
}

/* =========================================================================
 * ne_export_build
 * ===================================================================== */

int ne_export_build(const uint8_t        *buf,
                    size_t                len,
                    const NEParserContext *parser,
                    NEExportTable        *tbl)
{
    uint16_t entry_count;
    uint16_t written;
    uint32_t rnt_abs;
    uint32_t rnt_end;

    if (!parser || !tbl)
        return NE_IMPEXP_ERR_NULL;

    memset(tbl, 0, sizeof(*tbl));

    /* ---- Entry table ---- */
    if (!parser->entry_data || parser->entry_size == 0u) {
        /* No entry table: module has no exports */
        return NE_IMPEXP_OK;
    }

    entry_count = count_export_entries(parser->entry_data, parser->entry_size);
    if (entry_count == 0u)
        return NE_IMPEXP_OK;

    tbl->entries = (NEExportEntry *)calloc(entry_count, sizeof(NEExportEntry));
    if (!tbl->entries)
        return NE_IMPEXP_ERR_ALLOC;

    written = fill_export_entries(parser->entry_data,
                                  parser->entry_size,
                                  tbl->entries);
    tbl->count = written;

    /* ---- Resident name table (requires the raw buffer) ---- */
    if (buf && len > 0u
            && parser->header.resident_name_table_offset != 0u) {

        rnt_abs = parser->ne_offset
                  + parser->header.resident_name_table_offset;

        /* The entry table immediately follows the resident name table in a
         * well-formed NE file; use that as the upper bound.  Fall back to
         * the buffer length if the entry table offset is not set. */
        if (parser->header.entry_table_offset != 0u) {
            rnt_end = parser->ne_offset + parser->header.entry_table_offset;
        } else {
            rnt_end = (uint32_t)len;
        }

        if (rnt_end > (uint32_t)len)
            rnt_end = (uint32_t)len;

        if (rnt_abs < rnt_end) {
            attach_export_names(buf, rnt_abs, rnt_end,
                                tbl->entries, tbl->count);
        }
    }

    return NE_IMPEXP_OK;
}

/* =========================================================================
 * ne_export_free
 * ===================================================================== */

void ne_export_free(NEExportTable *tbl)
{
    if (!tbl)
        return;
    free(tbl->entries);
    memset(tbl, 0, sizeof(*tbl));
}

/* =========================================================================
 * ne_export_find_by_ordinal
 * ===================================================================== */

const NEExportEntry *ne_export_find_by_ordinal(const NEExportTable *tbl,
                                                uint16_t             ordinal)
{
    uint16_t i;

    if (!tbl || !tbl->entries)
        return NULL;

    for (i = 0u; i < tbl->count; i++) {
        if (tbl->entries[i].ordinal == ordinal)
            return &tbl->entries[i];
    }
    return NULL;
}

/* =========================================================================
 * ne_export_find_by_name
 * ===================================================================== */

const NEExportEntry *ne_export_find_by_name(const NEExportTable *tbl,
                                             const char          *name)
{
    uint16_t i;

    if (!tbl || !tbl->entries || !name || name[0] == '\0')
        return NULL;

    for (i = 0u; i < tbl->count; i++) {
        if (strncmp(tbl->entries[i].name, name, NE_EXPORT_NAME_MAX) == 0)
            return &tbl->entries[i];
    }
    return NULL;
}

/* =========================================================================
 * ne_import_resolve_ordinal
 * ===================================================================== */

int ne_import_resolve_ordinal(const NEExportTable *export_tbl,
                               uint16_t             ordinal,
                               uint16_t            *out_seg,
                               uint16_t            *out_off)
{
    const NEExportEntry *e;

    if (!out_seg || !out_off)
        return NE_IMPEXP_ERR_NULL;

    if (!export_tbl)
        return NE_IMPEXP_ERR_UNRESOLVED;

    e = ne_export_find_by_ordinal(export_tbl, ordinal);
    if (!e)
        return NE_IMPEXP_ERR_UNRESOLVED;

    *out_seg = e->segment;
    *out_off = e->offset;
    return NE_IMPEXP_OK;
}

/* =========================================================================
 * ne_import_resolve_name
 * ===================================================================== */

int ne_import_resolve_name(const NEExportTable *export_tbl,
                            const char          *api_name,
                            uint16_t            *out_seg,
                            uint16_t            *out_off)
{
    const NEExportEntry *e;

    if (!api_name || !out_seg || !out_off)
        return NE_IMPEXP_ERR_NULL;

    if (!export_tbl)
        return NE_IMPEXP_ERR_UNRESOLVED;

    e = ne_export_find_by_name(export_tbl, api_name);
    if (!e)
        return NE_IMPEXP_ERR_UNRESOLVED;

    *out_seg = e->segment;
    *out_off = e->offset;
    return NE_IMPEXP_OK;
}

/* =========================================================================
 * ne_stub_table_init
 * ===================================================================== */

int ne_stub_table_init(NEStubTable *tbl, uint16_t capacity)
{
    if (!tbl)
        return NE_IMPEXP_ERR_NULL;
    if (capacity == 0u)
        return NE_IMPEXP_ERR_NULL;

    memset(tbl, 0, sizeof(*tbl));

    tbl->entries = (NEStubEntry *)calloc(capacity, sizeof(NEStubEntry));
    if (!tbl->entries)
        return NE_IMPEXP_ERR_ALLOC;

    tbl->capacity = capacity;
    tbl->count    = 0u;
    return NE_IMPEXP_OK;
}

/* =========================================================================
 * ne_stub_table_free
 * ===================================================================== */

void ne_stub_table_free(NEStubTable *tbl)
{
    if (!tbl)
        return;
    free(tbl->entries);
    memset(tbl, 0, sizeof(*tbl));
}

/* =========================================================================
 * ne_stub_register
 * ===================================================================== */

int ne_stub_register(NEStubTable *tbl,
                     const char  *module_name,
                     const char  *api_name,
                     uint16_t     ordinal,
                     const char  *behavior,
                     const char  *milestone)
{
    NEStubEntry *e;

    if (!tbl || !module_name || !api_name || !behavior || !milestone)
        return NE_IMPEXP_ERR_NULL;

    /* Suppress duplicate registrations for the same (module, ordinal) */
    if (ne_stub_find_by_ordinal(tbl, module_name, ordinal) != NULL)
        return NE_IMPEXP_OK;

    if (tbl->count >= tbl->capacity)
        return NE_IMPEXP_ERR_FULL;

    e = &tbl->entries[tbl->count];
    memset(e, 0, sizeof(*e));

    strncpy(e->module_name, module_name, NE_STUB_MOD_NAME_MAX - 1u);
    e->module_name[NE_STUB_MOD_NAME_MAX - 1u] = '\0';

    strncpy(e->api_name, api_name, NE_EXPORT_NAME_MAX - 1u);
    e->api_name[NE_EXPORT_NAME_MAX - 1u] = '\0';

    e->ordinal = ordinal;

    strncpy(e->behavior, behavior, NE_STUB_BEHAVIOR_MAX - 1u);
    e->behavior[NE_STUB_BEHAVIOR_MAX - 1u] = '\0';

    strncpy(e->milestone, milestone, NE_STUB_MILESTONE_MAX - 1u);
    e->milestone[NE_STUB_MILESTONE_MAX - 1u] = '\0';

    e->removed = 0;

    tbl->count++;
    return NE_IMPEXP_OK;
}

/* =========================================================================
 * ne_stub_replace
 * ===================================================================== */

int ne_stub_replace(NEStubTable *tbl,
                    const char  *module_name,
                    uint16_t     ordinal)
{
    uint16_t i;

    if (!tbl || !module_name)
        return NE_IMPEXP_ERR_NULL;

    for (i = 0u; i < tbl->count; i++) {
        NEStubEntry *e = &tbl->entries[i];
        if (e->removed)
            continue;
        if (e->ordinal != ordinal)
            continue;
        if (strncmp(e->module_name, module_name, NE_STUB_MOD_NAME_MAX) != 0)
            continue;
        e->removed = 1;
        return NE_IMPEXP_OK;
    }
    return NE_IMPEXP_ERR_UNRESOLVED;
}

/* =========================================================================
 * ne_stub_find_by_ordinal
 * ===================================================================== */

const NEStubEntry *ne_stub_find_by_ordinal(const NEStubTable *tbl,
                                            const char        *module_name,
                                            uint16_t           ordinal)
{
    uint16_t i;

    if (!tbl || !module_name)
        return NULL;

    for (i = 0u; i < tbl->count; i++) {
        const NEStubEntry *e = &tbl->entries[i];
        if (e->ordinal != ordinal)
            continue;
        if (strncmp(e->module_name, module_name, NE_STUB_MOD_NAME_MAX) != 0)
            continue;
        return e;
    }
    return NULL;
}

/* =========================================================================
 * ne_stub_find_by_name
 * ===================================================================== */

const NEStubEntry *ne_stub_find_by_name(const NEStubTable *tbl,
                                         const char        *module_name,
                                         const char        *api_name)
{
    uint16_t i;

    if (!tbl || !module_name || !api_name || api_name[0] == '\0')
        return NULL;

    for (i = 0u; i < tbl->count; i++) {
        const NEStubEntry *e = &tbl->entries[i];
        if (strncmp(e->module_name, module_name, NE_STUB_MOD_NAME_MAX) != 0)
            continue;
        if (strncmp(e->api_name, api_name, NE_EXPORT_NAME_MAX) != 0)
            continue;
        return e;
    }
    return NULL;
}

/* =========================================================================
 * ne_impexp_strerror
 * ===================================================================== */

const char *ne_impexp_strerror(int err)
{
    switch (err) {
    case NE_IMPEXP_OK:              return "success";
    case NE_IMPEXP_ERR_NULL:        return "NULL argument";
    case NE_IMPEXP_ERR_ALLOC:       return "memory allocation failure";
    case NE_IMPEXP_ERR_IO:          return "data out of buffer bounds";
    case NE_IMPEXP_ERR_UNRESOLVED:  return "import target unresolved";
    case NE_IMPEXP_ERR_FULL:        return "stub table is full";
    default:                        return "unknown error";
    }
}

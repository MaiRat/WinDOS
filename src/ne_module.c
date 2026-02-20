/*
 * ne_module.c - NE (New Executable) global module table implementation
 *
 * Manages the lifecycle of loaded NE modules: registration, duplicate-load
 * detection (by name), reference counting, inter-module dependency tracking,
 * and orderly unloading with reference-count-guarded memory release.
 */

#include "ne_module.h"
#include "ne_dosalloc.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/*
 * find_slot_by_handle - scan entries[] for an active slot with handle h.
 * Returns a pointer to the entry or NULL.
 */
static NEModuleEntry *find_slot_by_handle(NEModuleTable *tbl,
                                          NEModuleHandle h)
{
    uint16_t i;

    if (!tbl || !tbl->entries || h == NE_MOD_HANDLE_INVALID)
        return NULL;

    for (i = 0; i < tbl->capacity; i++) {
        if (tbl->entries[i].handle == h)
            return &tbl->entries[i];
    }
    return NULL;
}

/*
 * find_free_slot - locate the first slot with handle == 0 (unused).
 * Returns a pointer to the slot or NULL when the table is full.
 */
static NEModuleEntry *find_free_slot(NEModuleTable *tbl)
{
    uint16_t i;

    if (!tbl || !tbl->entries)
        return NULL;

    for (i = 0; i < tbl->capacity; i++) {
        if (tbl->entries[i].handle == NE_MOD_HANDLE_INVALID)
            return &tbl->entries[i];
    }
    return NULL;
}

/*
 * is_depended_on - return non-zero if any active entry lists 'target' as
 * one of its dependencies.
 */
static int is_depended_on(const NEModuleTable *tbl, NEModuleHandle target)
{
    uint16_t i;
    uint16_t d;

    if (!tbl || !tbl->entries)
        return 0;

    for (i = 0; i < tbl->capacity; i++) {
        const NEModuleEntry *e = &tbl->entries[i];
        if (e->handle == NE_MOD_HANDLE_INVALID)
            continue;
        for (d = 0; d < e->dep_count; d++) {
            if (e->deps[d] == target)
                return 1;
        }
    }
    return 0;
}

/*
 * release_entry - free owned contexts and zero the slot so it can be reused.
 */
static void release_entry(NEModuleEntry *e)
{
    ne_free(&e->parser);
    ne_loader_free(&e->loader);
    memset(e, 0, sizeof(*e));
}

/* -------------------------------------------------------------------------
 * ne_mod_table_init
 * ---------------------------------------------------------------------- */

int ne_mod_table_init(NEModuleTable *tbl, uint16_t capacity)
{
    if (!tbl)
        return NE_MOD_ERR_NULL;
    if (capacity == 0)
        return NE_MOD_ERR_NULL;

    memset(tbl, 0, sizeof(*tbl));

    tbl->entries = (NEModuleEntry *)NE_CALLOC(capacity, sizeof(NEModuleEntry));
    if (!tbl->entries)
        return NE_MOD_ERR_ALLOC;

    tbl->capacity    = capacity;
    tbl->count       = 0;
    tbl->next_handle = 1;   /* handles start at 1; 0 is the invalid sentinel */

    return NE_MOD_OK;
}

/* -------------------------------------------------------------------------
 * ne_mod_table_free
 * ---------------------------------------------------------------------- */

void ne_mod_table_free(NEModuleTable *tbl)
{
    uint16_t i;

    if (!tbl)
        return;

    if (tbl->entries) {
        for (i = 0; i < tbl->capacity; i++) {
            if (tbl->entries[i].handle != NE_MOD_HANDLE_INVALID)
                release_entry(&tbl->entries[i]);
        }
        NE_FREE(tbl->entries);
    }

    memset(tbl, 0, sizeof(*tbl));
}

/* -------------------------------------------------------------------------
 * ne_mod_load
 * ---------------------------------------------------------------------- */

int ne_mod_load(NEModuleTable  *tbl,
                const char     *name,
                NEParserContext *parser,
                NELoaderContext *loader,
                NEModuleHandle  *out_handle)
{
    NEModuleHandle  existing;
    NEModuleEntry  *slot;
    NEModuleHandle  h;

    if (!tbl || !name || !out_handle)
        return NE_MOD_ERR_NULL;

    *out_handle = NE_MOD_HANDLE_INVALID;

    /* Duplicate-load detection: return existing handle on re-load */
    existing = ne_mod_find(tbl, name);
    if (existing != NE_MOD_HANDLE_INVALID) {
        slot = find_slot_by_handle(tbl, existing);
        /* slot cannot be NULL here since ne_mod_find returned a valid handle */
        slot->ref_count++;
        *out_handle = existing;
        return NE_MOD_OK;
    }

    /* Locate a free slot */
    slot = find_free_slot(tbl);
    if (!slot)
        return NE_MOD_ERR_FULL;

    /* Assign the next handle, wrapping around but skipping 0 */
    h = tbl->next_handle;
    tbl->next_handle++;
    if (tbl->next_handle == NE_MOD_HANDLE_INVALID)
        tbl->next_handle = 1;

    /* Populate the entry; transfer ownership of parser and loader */
    memset(slot, 0, sizeof(*slot));
    slot->handle    = h;
    slot->ref_count = 1;

    /* Copy the name, truncating to NE_MOD_NAME_MAX - 1 characters */
    strncpy(slot->name, name, NE_MOD_NAME_MAX - 1u);
    slot->name[NE_MOD_NAME_MAX - 1u] = '\0';

    if (parser)
        slot->parser = *parser;
    if (loader)
        slot->loader = *loader;

    tbl->count++;
    *out_handle = h;
    return NE_MOD_OK;
}

/* -------------------------------------------------------------------------
 * ne_mod_addref
 * ---------------------------------------------------------------------- */

int ne_mod_addref(NEModuleTable *tbl, NEModuleHandle handle)
{
    NEModuleEntry *e;

    if (!tbl)
        return NE_MOD_ERR_NULL;
    if (handle == NE_MOD_HANDLE_INVALID)
        return NE_MOD_ERR_BAD_HANDLE;

    e = find_slot_by_handle(tbl, handle);
    if (!e)
        return NE_MOD_ERR_NOT_FOUND;

    e->ref_count++;
    return NE_MOD_OK;
}

/* -------------------------------------------------------------------------
 * ne_mod_unload
 * ---------------------------------------------------------------------- */

int ne_mod_unload(NEModuleTable *tbl, NEModuleHandle handle)
{
    NEModuleEntry *e;

    if (!tbl)
        return NE_MOD_ERR_NULL;
    if (handle == NE_MOD_HANDLE_INVALID)
        return NE_MOD_ERR_BAD_HANDLE;

    e = find_slot_by_handle(tbl, handle);
    if (!e)
        return NE_MOD_ERR_NOT_FOUND;

    /* Decrement reference count */
    if (e->ref_count > 0)
        e->ref_count--;

    /* Only remove when the count reaches zero */
    if (e->ref_count > 0)
        return NE_MOD_OK;

    /* Check that no other module depends on this one */
    if (is_depended_on(tbl, handle))
        return NE_MOD_ERR_IN_USE;

    /* Free owned resources and mark the slot as available */
    release_entry(e);
    tbl->count--;

    return NE_MOD_OK;
}

/* -------------------------------------------------------------------------
 * ne_mod_add_dep
 * ---------------------------------------------------------------------- */

int ne_mod_add_dep(NEModuleTable *tbl,
                   NEModuleHandle handle,
                   NEModuleHandle dep_handle)
{
    NEModuleEntry *e;
    uint16_t       d;

    if (!tbl)
        return NE_MOD_ERR_NULL;
    if (handle == NE_MOD_HANDLE_INVALID || dep_handle == NE_MOD_HANDLE_INVALID)
        return NE_MOD_ERR_BAD_HANDLE;

    e = find_slot_by_handle(tbl, handle);
    if (!e)
        return NE_MOD_ERR_NOT_FOUND;

    /* Validate that the dependency target exists */
    if (!find_slot_by_handle(tbl, dep_handle))
        return NE_MOD_ERR_NOT_FOUND;

    /* Ignore duplicate entries */
    for (d = 0; d < e->dep_count; d++) {
        if (e->deps[d] == dep_handle)
            return NE_MOD_OK;
    }

    if (e->dep_count >= NE_MOD_DEP_MAX)
        return NE_MOD_ERR_DEP_FULL;

    e->deps[e->dep_count] = dep_handle;
    e->dep_count++;
    return NE_MOD_OK;
}

/* -------------------------------------------------------------------------
 * ne_mod_find
 * ---------------------------------------------------------------------- */

NEModuleHandle ne_mod_find(const NEModuleTable *tbl, const char *name)
{
    uint16_t i;

    if (!tbl || !tbl->entries || !name)
        return NE_MOD_HANDLE_INVALID;

    for (i = 0; i < tbl->capacity; i++) {
        const NEModuleEntry *e = &tbl->entries[i];
        if (e->handle == NE_MOD_HANDLE_INVALID)
            continue;
        if (strncmp(e->name, name, NE_MOD_NAME_MAX) == 0)
            return e->handle;
    }
    return NE_MOD_HANDLE_INVALID;
}

/* -------------------------------------------------------------------------
 * ne_mod_get
 * ---------------------------------------------------------------------- */

NEModuleEntry *ne_mod_get(NEModuleTable *tbl, NEModuleHandle handle)
{
    return find_slot_by_handle(tbl, handle);
}

/* -------------------------------------------------------------------------
 * ne_mod_strerror
 * ---------------------------------------------------------------------- */

const char *ne_mod_strerror(int err)
{
    switch (err) {
    case NE_MOD_OK:              return "success";
    case NE_MOD_ERR_NULL:        return "NULL argument";
    case NE_MOD_ERR_ALLOC:       return "memory allocation failure";
    case NE_MOD_ERR_NOT_FOUND:   return "module not found";
    case NE_MOD_ERR_FULL:        return "module table is full";
    case NE_MOD_ERR_DEP_FULL:    return "dependency list is full";
    case NE_MOD_ERR_IN_USE:      return "module still in use by other modules";
    case NE_MOD_ERR_BAD_HANDLE:  return "invalid module handle";
    default:                     return "unknown error";
    }
}

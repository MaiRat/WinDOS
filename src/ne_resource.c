/*
 * ne_resource.c - Phase 5 Dynamic Segment and Resource Management:
 *                 Resource Manager implementation
 *
 * Provides resource table management, enumeration, accelerator-table
 * loading/translation, dialog-template loading, and menu loading.
 *
 * Host-side: uses standard C malloc / calloc / free (via ne_dosalloc.h).
 * Watcom/DOS 16-bit target: the NE_MALLOC / NE_CALLOC / NE_FREE macros
 * expand to DOS INT 21h conventional-memory allocation.
 *
 * Reference: Microsoft Windows 3.1 SDK – Resource Functions.
 */

#include "ne_resource.h"
#include "ne_dosalloc.h"

#include <string.h>

/* =========================================================================
 * Internal helpers
 * ===================================================================== */

/*
 * res_free_slot - find an unused slot (handle == 0).
 */
static NEResEntry *res_free_slot(NEResTable *tbl)
{
    uint16_t i;

    if (!tbl || !tbl->entries)
        return NULL;

    for (i = 0; i < tbl->capacity; i++) {
        if (tbl->entries[i].handle == NE_RES_HANDLE_INVALID)
            return &tbl->entries[i];
    }
    return NULL;
}

/* =========================================================================
 * ne_res_table_init / ne_res_table_free
 * ===================================================================== */

int ne_res_table_init(NEResTable *tbl, uint16_t capacity)
{
    if (!tbl || capacity == 0)
        return NE_RES_ERR_NULL;

    tbl->entries = (NEResEntry *)NE_CALLOC(capacity, sizeof(NEResEntry));
    if (!tbl->entries)
        return NE_RES_ERR_ALLOC;

    tbl->capacity    = capacity;
    tbl->count       = 0;
    tbl->next_handle = 1;
    tbl->initialized = 1;
    return NE_RES_OK;
}

void ne_res_table_free(NEResTable *tbl)
{
    if (!tbl)
        return;

    if (tbl->entries) {
        NE_FREE(tbl->entries);
        tbl->entries = NULL;
    }
    memset(tbl, 0, sizeof(*tbl));
}

/* =========================================================================
 * ne_res_add
 * ===================================================================== */

NEResHandle ne_res_add(NEResTable    *tbl,
                       uint16_t       type_id,
                       uint16_t       name_id,
                       const char    *name_str,
                       const uint8_t *raw_data,
                       uint32_t       raw_size)
{
    NEResEntry *slot;

    if (!tbl || !tbl->initialized)
        return NE_RES_HANDLE_INVALID;

    if (tbl->count >= tbl->capacity)
        return NE_RES_HANDLE_INVALID;

    slot = res_free_slot(tbl);
    if (!slot)
        return NE_RES_HANDLE_INVALID;

    slot->handle   = tbl->next_handle++;
    slot->type_id  = type_id;
    slot->name_id  = name_id;
    slot->raw_data = raw_data;
    slot->raw_size = raw_size;

    if (name_str) {
        strncpy(slot->name_str, name_str, NE_RES_NAME_MAX - 1);
        slot->name_str[NE_RES_NAME_MAX - 1] = '\0';
    } else {
        slot->name_str[0] = '\0';
    }

    tbl->count++;
    return slot->handle;
}

/* =========================================================================
 * ne_res_find_by_id / ne_res_find_by_name
 * ===================================================================== */

NEResEntry *ne_res_find_by_id(NEResTable *tbl,
                               uint16_t    type_id,
                               uint16_t    name_id)
{
    uint16_t i;

    if (!tbl || !tbl->entries)
        return NULL;

    for (i = 0; i < tbl->capacity; i++) {
        if (tbl->entries[i].handle != NE_RES_HANDLE_INVALID &&
            tbl->entries[i].type_id == type_id &&
            tbl->entries[i].name_id == name_id)
            return &tbl->entries[i];
    }
    return NULL;
}

NEResEntry *ne_res_find_by_name(NEResTable *tbl,
                                 uint16_t    type_id,
                                 const char *name_str)
{
    uint16_t i;

    if (!tbl || !tbl->entries || !name_str)
        return NULL;

    for (i = 0; i < tbl->capacity; i++) {
        if (tbl->entries[i].handle != NE_RES_HANDLE_INVALID &&
            tbl->entries[i].type_id == type_id &&
            strcmp(tbl->entries[i].name_str, name_str) == 0)
            return &tbl->entries[i];
    }
    return NULL;
}

/* =========================================================================
 * ne_res_enum_types
 * ===================================================================== */

int ne_res_enum_types(NEResTable *tbl,
                      int (*callback)(uint16_t type_id, void *user_data),
                      void *user_data)
{
    /* We use a small local bitmap to track seen types.
     * RT_* values are in the range 1–14 for standard types; we accept
     * any value up to 255 by using a 256-bit (32-byte) bitmap. */
    uint8_t  seen[32]; /* 256 bits; bit i set means type_id i was reported */
    uint16_t unique_count = 0;
    uint16_t i;

    if (!tbl || !tbl->entries || !callback)
        return NE_RES_ERR_NULL;

    memset(seen, 0, sizeof(seen));

    for (i = 0; i < tbl->capacity; i++) {
        uint16_t t;
        uint8_t  byte_idx;
        uint8_t  bit_idx;

        if (tbl->entries[i].handle == NE_RES_HANDLE_INVALID)
            continue;

        t = tbl->entries[i].type_id;
        /* Only track types 0–255 in the bitmap; ignore larger values */
        if (t > 255u)
            continue;

        byte_idx = (uint8_t)(t >> 3);
        bit_idx  = (uint8_t)(t & 0x07u);

        if (seen[byte_idx] & (uint8_t)(1u << bit_idx))
            continue;  /* already reported this type */

        seen[byte_idx] |= (uint8_t)(1u << bit_idx);
        unique_count++;

        if (!callback(t, user_data))
            break;  /* caller requested early stop */
    }

    return (int)unique_count;
}

/* =========================================================================
 * ne_res_enum_names
 * ===================================================================== */

int ne_res_enum_names(NEResTable *tbl,
                      uint16_t    type_id,
                      int (*callback)(const NEResEntry *entry,
                                      void             *user_data),
                      void *user_data)
{
    uint16_t i;
    int      found = 0;

    if (!tbl || !tbl->entries || !callback)
        return NE_RES_ERR_NULL;

    for (i = 0; i < tbl->capacity; i++) {
        if (tbl->entries[i].handle == NE_RES_HANDLE_INVALID)
            continue;
        if (tbl->entries[i].type_id != type_id)
            continue;

        found++;
        if (!callback(&tbl->entries[i], user_data))
            break;
    }

    return found;
}

/* =========================================================================
 * ne_res_load_accel / ne_res_accel_free / ne_res_accel_translate
 * ===================================================================== */

int ne_res_load_accel(NEAccelTable  *tbl,
                      const uint8_t *raw,
                      uint32_t       size)
{
    uint32_t      off;
    uint16_t      count;
    NEAccelEntry *buf;

    if (!tbl || !raw)
        return NE_RES_ERR_NULL;

    if (size < 5u)
        return NE_RES_ERR_BAD_DATA;

    /* Count entries by scanning to the FLASTKEY marker */
    count = 0;
    for (off = 0; off + 4u < size; off += 5u) {
        count++;
        if (raw[off] & FLASTKEY)
            break;
    }

    if (count == 0)
        return NE_RES_ERR_BAD_DATA;

    buf = (NEAccelEntry *)NE_CALLOC(count, sizeof(NEAccelEntry));
    if (!buf)
        return NE_RES_ERR_ALLOC;

    for (off = 0; off < (uint32_t)count * 5u; off += 5u) {
        uint16_t idx = (uint16_t)(off / 5u);
        buf[idx].fVirt = raw[off];
        buf[idx].key   = (uint16_t)( raw[off + 1u] |
                                    ((uint16_t)raw[off + 2u] << 8) );
        buf[idx].cmd   = (uint16_t)( raw[off + 3u] |
                                    ((uint16_t)raw[off + 4u] << 8) );
    }

    tbl->entries  = buf;
    tbl->count    = count;
    tbl->capacity = count;
    return NE_RES_OK;
}

void ne_res_accel_free(NEAccelTable *tbl)
{
    if (!tbl)
        return;

    if (tbl->entries) {
        NE_FREE(tbl->entries);
        tbl->entries = NULL;
    }
    tbl->count    = 0;
    tbl->capacity = 0;
}

int ne_res_accel_translate(const NEAccelTable *tbl,
                           uint8_t             fVirt,
                           uint16_t            key,
                           uint16_t           *out_cmd)
{
    uint16_t i;

    if (!tbl || !out_cmd)
        return NE_RES_ERR_NULL;

    for (i = 0; i < tbl->count; i++) {
        /* The modifier bits and FVIRTKEY flag must all match */
        uint8_t mod_mask = FSHIFT | FCONTROL | FALT | FVIRTKEY;
        if ((tbl->entries[i].fVirt & mod_mask) == (fVirt & mod_mask) &&
             tbl->entries[i].key == key) {
            *out_cmd = tbl->entries[i].cmd;
            return NE_RES_OK;
        }
    }
    return NE_RES_ERR_NOT_FOUND;
}

/* =========================================================================
 * ne_res_load_dialog
 * ===================================================================== */

/*
 * Helper: read a NUL-terminated string from raw[*off], advance *off past
 * the NUL, copy at most dst_max-1 chars into dst.
 * Returns NE_RES_OK or NE_RES_ERR_BAD_DATA if the NUL is not found before
 * raw+size.
 */
static int read_cstr(const uint8_t *raw, uint32_t size,
                     uint32_t *off,
                     char *dst, uint16_t dst_max)
{
    uint32_t start = *off;
    uint32_t end;
    uint32_t copy_len;

    /* Find the NUL terminator */
    for (end = start; end < size; end++) {
        if (raw[end] == '\0') {
            copy_len = end - start;
            if (copy_len >= dst_max)
                copy_len = dst_max - 1u;
            memcpy(dst, raw + start, copy_len);
            dst[copy_len] = '\0';
            *off = end + 1u;
            return NE_RES_OK;
        }
    }
    return NE_RES_ERR_BAD_DATA;
}

int ne_res_load_dialog(NEDlgTemplate *dlg,
                       const uint8_t *raw,
                       uint32_t       size)
{
    uint32_t off = 0;
    uint8_t  item_count;
    uint16_t i;

    if (!dlg || !raw)
        return NE_RES_ERR_NULL;

    memset(dlg, 0, sizeof(*dlg));

    /* Minimum header: 4 (style) + 1 (count) + 8 (x,y,cx,cy) = 13 bytes */
    if (size < 13u)
        return NE_RES_ERR_BAD_DATA;

    /* style (4 bytes, LE) */
    dlg->style = (uint32_t)( raw[0] |
                             ((uint32_t)raw[1] <<  8) |
                             ((uint32_t)raw[2] << 16) |
                             ((uint32_t)raw[3] << 24) );
    off = 4u;

    /* item count (1 byte) */
    item_count = raw[off++];
    if (item_count > NE_RES_DLG_ITEM_CAP)
        item_count = NE_RES_DLG_ITEM_CAP;

    /* x, y, cx, cy (2 bytes each, LE) */
    if (off + 8u > size)
        return NE_RES_ERR_BAD_DATA;

    dlg->x  = (int16_t)( raw[off] | ((uint16_t)raw[off+1] << 8) ); off += 2u;
    dlg->y  = (int16_t)( raw[off] | ((uint16_t)raw[off+1] << 8) ); off += 2u;
    dlg->cx = (int16_t)( raw[off] | ((uint16_t)raw[off+1] << 8) ); off += 2u;
    dlg->cy = (int16_t)( raw[off] | ((uint16_t)raw[off+1] << 8) ); off += 2u;

    /* title string */
    if (read_cstr(raw, size, &off, dlg->title, NE_RES_NAME_MAX) != NE_RES_OK)
        return NE_RES_ERR_BAD_DATA;

    /* font name string */
    if (read_cstr(raw, size, &off,
                  dlg->font_name, NE_RES_FONT_NAME_MAX) != NE_RES_OK)
        return NE_RES_ERR_BAD_DATA;

    /* font size (2 bytes, LE) */
    if (off + 2u > size)
        return NE_RES_ERR_BAD_DATA;
    dlg->font_size = (uint16_t)( raw[off] | ((uint16_t)raw[off+1] << 8) );
    off += 2u;

    /* Parse items: each item is 13 bytes + title string
     *   2: style, 2: x, 2: y, 2: cx, 2: cy, 2: id, 1: cls + title string */
    dlg->item_count = 0;
    for (i = 0; i < item_count; i++) {
        NEDlgItem *it = &dlg->items[dlg->item_count];

        if (off + 11u > size)
            break;

        it->style = (uint16_t)( raw[off] | ((uint16_t)raw[off+1] << 8) );
        off += 2u;
        it->x  = (int16_t)( raw[off] | ((uint16_t)raw[off+1] << 8) ); off += 2u;
        it->y  = (int16_t)( raw[off] | ((uint16_t)raw[off+1] << 8) ); off += 2u;
        it->cx = (int16_t)( raw[off] | ((uint16_t)raw[off+1] << 8) ); off += 2u;
        it->cy = (int16_t)( raw[off] | ((uint16_t)raw[off+1] << 8) ); off += 2u;
        it->id  = (uint16_t)( raw[off] | ((uint16_t)raw[off+1] << 8) );
        off += 2u;

        if (off >= size)
            break;
        it->cls = raw[off++];

        if (read_cstr(raw, size, &off, it->title, NE_RES_NAME_MAX) != NE_RES_OK)
            break;

        dlg->item_count++;
    }

    return NE_RES_OK;
}

/* =========================================================================
 * ne_res_dialog_box / ne_res_create_dialog
 * ===================================================================== */

int ne_res_dialog_box(uint16_t            hwnd_parent,
                      const NEDlgTemplate *dlg,
                      NEDlgProc            dlg_proc)
{
    if (!dlg || !dlg_proc)
        return -1;

    /* Host stub: call WM_INITDIALOG and return its result */
    return (int)dlg_proc(hwnd_parent, 0x0110u /* WM_INITDIALOG */, 0, 0);
}

uint16_t ne_res_create_dialog(uint16_t            hwnd_parent,
                               const NEDlgTemplate *dlg,
                               NEDlgProc            dlg_proc)
{
    if (!dlg || !dlg_proc)
        return 0;

    dlg_proc(hwnd_parent, 0x0110u /* WM_INITDIALOG */, 0, 0);
    return 1; /* pseudo-handle */
}

/* =========================================================================
 * ne_res_load_menu / ne_res_menu_free / ne_res_track_popup_menu
 * ===================================================================== */

int ne_res_load_menu(NEMenu        *menu,
                     const uint8_t *raw,
                     uint32_t       size)
{
    uint32_t    off;
    uint16_t    cap;
    NEMenuItem *buf;
    uint16_t    count;

    if (!menu || !raw)
        return NE_RES_ERR_NULL;

    if (size < 4u)
        return NE_RES_ERR_BAD_DATA;

    /* First pass: count items to allocate correctly */
    cap   = 0;
    off   = 0;
    count = 0;
    while (off + 4u <= size) {
        uint16_t flags;
        uint32_t text_start;

        flags      = (uint16_t)( raw[off] | ((uint16_t)raw[off+1] << 8) );
        off       += 4u; /* skip flags (2) + id (2) */
        text_start = off;

        /* Advance past the NUL-terminated text */
        while (off < size && raw[off] != '\0')
            off++;
        if (off < size)
            off++; /* skip NUL */

        (void)text_start; /* suppress unused-variable warning */
        cap++;

        if (flags & MF_END)
            break;
    }

    if (cap == 0)
        return NE_RES_ERR_BAD_DATA;

    buf = (NEMenuItem *)NE_CALLOC(cap, sizeof(NEMenuItem));
    if (!buf)
        return NE_RES_ERR_ALLOC;

    /* Second pass: populate items */
    off   = 0;
    count = 0;
    while (off + 4u <= size && count < cap) {
        uint16_t flags;
        uint16_t id;
        uint32_t text_start;
        uint32_t copy_len;
        uint32_t text_end;

        flags = (uint16_t)( raw[off] | ((uint16_t)raw[off+1] << 8) );
        id    = (uint16_t)( raw[off+2] | ((uint16_t)raw[off+3] << 8) );
        off  += 4u;

        text_start = off;
        text_end   = off;
        while (text_end < size && raw[text_end] != '\0')
            text_end++;

        copy_len = text_end - text_start;
        if (copy_len >= NE_RES_NAME_MAX)
            copy_len = NE_RES_NAME_MAX - 1u;

        buf[count].flags = flags;
        buf[count].id    = id;
        memcpy(buf[count].text, raw + text_start, copy_len);
        buf[count].text[copy_len] = '\0';

        off = text_end + 1u; /* skip NUL */
        count++;

        if (flags & MF_END)
            break;
    }

    menu->items    = buf;
    menu->count    = count;
    menu->capacity = cap;
    return NE_RES_OK;
}

void ne_res_menu_free(NEMenu *menu)
{
    if (!menu)
        return;

    if (menu->items) {
        NE_FREE(menu->items);
        menu->items = NULL;
    }
    menu->count    = 0;
    menu->capacity = 0;
}

int ne_res_track_popup_menu(const NEMenu *menu,
                             int16_t       x,
                             int16_t       y,
                             uint16_t      hwnd,
                             void (*item_callback)(const NEMenuItem *item,
                                                   void             *ud),
                             void         *user_data)
{
    uint16_t i;

    if (!menu)
        return NE_RES_ERR_NULL;

    /* Suppress unused-parameter warnings on the host */
    (void)x;
    (void)y;
    (void)hwnd;

    if (item_callback) {
        for (i = 0; i < menu->count; i++)
            item_callback(&menu->items[i], user_data);
    }

    return 0; /* no item selected in the stub */
}

/* =========================================================================
 * ne_res_strerror
 * ===================================================================== */

const char *ne_res_strerror(int err)
{
    switch (err) {
    case NE_RES_OK:             return "success";
    case NE_RES_ERR_NULL:       return "NULL pointer argument";
    case NE_RES_ERR_ALLOC:      return "memory allocation failure";
    case NE_RES_ERR_FULL:       return "resource table at capacity";
    case NE_RES_ERR_NOT_FOUND:  return "resource not found";
    case NE_RES_ERR_BAD_TYPE:   return "unrecognised resource type";
    case NE_RES_ERR_BAD_DATA:   return "malformed resource data";
    case NE_RES_ERR_BAD_HANDLE: return "invalid resource handle";
    case NE_RES_ERR_DUP:        return "duplicate resource entry";
    default:                    return "unknown error";
    }
}

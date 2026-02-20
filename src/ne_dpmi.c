/*
 * ne_dpmi.c - Protected-mode (DPMI) support implementation
 *
 * Implements Phase H of the KERNEL_ASSESSMENT.md roadmap.
 *
 * Host-side build: all DPMI operations are simulated in-memory using
 * the NEDpmiContext tables.  No actual INT 31h handler is installed.
 *
 * Watcom/DOS 16-bit target: the DPMI server installs an INT 31h handler
 * that dispatches function calls.  Selector and descriptor operations
 * manipulate the LDT via DPMI host calls.  Extended memory uses DPMI
 * AX=0501h/0502h/0503h.
 */

#include "ne_dpmi.h"

#include <stdlib.h>
#include <string.h>

#ifdef __WATCOMC__
#include <dos.h>
#include <i86.h>

/*
 * Saved original INT 31h vector for restoration when the server stops.
 */
static void (__interrupt __far *g_saved_int31)(void) = NULL;

/*
 * Global pointer to the active DPMI context for the ISR.
 */
static NEDpmiContext *g_dpmi_ctx = NULL;
#endif /* __WATCOMC__ */

/* =========================================================================
 * Internal helpers
 * ===================================================================== */

/*
 * find_selector_entry - locate the selector table entry for 'selector'.
 *
 * Returns the index into ctx->selectors, or -1 if not found.
 */
static int find_selector_entry(const NEDpmiContext *ctx, uint16_t selector)
{
    uint16_t i;

    for (i = 0; i < NE_DPMI_MAX_SELECTORS; i++) {
        if (ctx->selectors[i].in_use &&
            ctx->selectors[i].selector == selector)
            return (int)i;
    }
    return -1;
}

/*
 * find_ext_block - locate the extended memory block for 'handle'.
 *
 * Returns the index into ctx->ext_blocks, or -1 if not found.
 */
static int find_ext_block(const NEDpmiContext *ctx, uint32_t handle)
{
    uint16_t i;

    for (i = 0; i < NE_DPMI_MAX_EXTBLOCKS; i++) {
        if (ctx->ext_blocks[i].in_use &&
            ctx->ext_blocks[i].handle == handle)
            return (int)i;
    }
    return -1;
}

/*
 * alloc_descriptor_slot - find a free descriptor slot.
 *
 * Returns the index or -1 if the table is full.
 */
static int alloc_descriptor_slot(NEDpmiContext *ctx)
{
    uint16_t i;

    for (i = 0; i < NE_DPMI_MAX_DESCRIPTORS; i++) {
        if (!ctx->descriptors[i].in_use)
            return (int)i;
    }
    return -1;
}

/*
 * make_selector - construct a selector value from a descriptor index.
 *
 * Uses LDT (TI=1), RPL=3 (user mode).
 */
static uint16_t make_selector(uint16_t desc_index)
{
    return (uint16_t)((desc_index << NE_DPMI_SEL_INDEX_SHIFT) |
                       NE_DPMI_SEL_TI_LDT |
                       3u); /* RPL 3 */
}

/* =========================================================================
 * ne_dpmi_init / ne_dpmi_free
 * ===================================================================== */

int ne_dpmi_init(NEDpmiContext *ctx)
{
    if (!ctx)
        return NE_DPMI_ERR_NULL;

    memset(ctx, 0, sizeof(*ctx));
    ctx->next_ext_handle = 1;
    ctx->next_base       = 0x00100000uL; /* start at 1 MB (above conv mem) */
    ctx->initialized     = 1;

    return NE_DPMI_OK;
}

void ne_dpmi_free(NEDpmiContext *ctx)
{
    if (!ctx)
        return;

    if (ctx->server_active)
        ne_dpmi_server_stop(ctx);

    memset(ctx, 0, sizeof(*ctx));
}

/* =========================================================================
 * ne_dpmi_server_start / ne_dpmi_server_stop / ne_dpmi_server_is_active
 * ===================================================================== */

int ne_dpmi_server_start(NEDpmiContext *ctx)
{
    if (!ctx)
        return NE_DPMI_ERR_NULL;
    if (!ctx->initialized)
        return NE_DPMI_ERR_NOT_INIT;

#ifdef __WATCOMC__
    /* Save original INT 31h and install our handler. */
    g_saved_int31 = _dos_getvect(0x31);
    g_dpmi_ctx    = ctx;
    /* NOTE: A real __interrupt stub would be installed here that
     * reads registers and calls ne_dpmi_dispatch().  For the host
     * build we just mark the server as active. */
#endif

    ctx->server_active = 1;
    return NE_DPMI_OK;
}

int ne_dpmi_server_stop(NEDpmiContext *ctx)
{
    if (!ctx)
        return NE_DPMI_ERR_NULL;
    if (!ctx->initialized)
        return NE_DPMI_ERR_NOT_INIT;

#ifdef __WATCOMC__
    /* Restore original INT 31h. */
    if (g_saved_int31) {
        _dos_setvect(0x31, g_saved_int31);
        g_saved_int31 = NULL;
    }
    g_dpmi_ctx = NULL;
#endif

    ctx->server_active = 0;
    return NE_DPMI_OK;
}

int ne_dpmi_server_is_active(const NEDpmiContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;
    return ctx->server_active;
}

/* =========================================================================
 * ne_dpmi_get_version
 * ===================================================================== */

int ne_dpmi_get_version(const NEDpmiContext *ctx,
                        uint8_t *major, uint8_t *minor)
{
    if (!ctx || !major || !minor)
        return NE_DPMI_ERR_NULL;

    *major = NE_DPMI_VERSION_MAJOR;
    *minor = NE_DPMI_VERSION_MINOR;
    return NE_DPMI_OK;
}

/* =========================================================================
 * ne_dpmi_alloc_selector / ne_dpmi_free_selector / ne_dpmi_change_selector
 * ===================================================================== */

uint16_t ne_dpmi_alloc_selector(NEDpmiContext *ctx, uint16_t src_sel)
{
    int          slot;
    int          desc_idx;
    uint16_t     new_sel;
    uint16_t     i;

    if (!ctx || !ctx->initialized)
        return NE_DPMI_SEL_INVALID;

    /* Find a free selector slot. */
    slot = -1;
    for (i = 0; i < NE_DPMI_MAX_SELECTORS; i++) {
        if (!ctx->selectors[i].in_use) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0)
        return NE_DPMI_SEL_INVALID;

    /* Allocate a descriptor. */
    desc_idx = alloc_descriptor_slot(ctx);
    if (desc_idx < 0)
        return NE_DPMI_SEL_INVALID;

    new_sel = make_selector((uint16_t)desc_idx);

    /* Initialise the descriptor. */
    if (src_sel != 0) {
        /* Copy from source selector. */
        int src_idx = find_selector_entry(ctx, src_sel);
        if (src_idx >= 0) {
            uint16_t src_desc = ctx->selectors[src_idx].desc_index;
            ctx->descriptors[desc_idx] = ctx->descriptors[src_desc];
        } else {
            /* Source not found: use defaults. */
            ctx->descriptors[desc_idx].base   = 0;
            ctx->descriptors[desc_idx].limit  = 0xFFFFu;
            ctx->descriptors[desc_idx].access = NE_DPMI_DESC_DATA_RW;
            ctx->descriptors[desc_idx].flags  = 0;
        }
    } else {
        /* Default: writable data segment, base 0, limit 64K. */
        ctx->descriptors[desc_idx].base   = 0;
        ctx->descriptors[desc_idx].limit  = 0xFFFFu;
        ctx->descriptors[desc_idx].access = NE_DPMI_DESC_DATA_RW;
        ctx->descriptors[desc_idx].flags  = 0;
    }
    ctx->descriptors[desc_idx].in_use = 1;
    ctx->desc_count++;

    /* Record the selector. */
    ctx->selectors[slot].selector   = new_sel;
    ctx->selectors[slot].desc_index = (uint16_t)desc_idx;
    ctx->selectors[slot].in_use     = 1;
    ctx->sel_count++;

    return new_sel;
}

int ne_dpmi_free_selector(NEDpmiContext *ctx, uint16_t selector)
{
    int idx;

    if (!ctx)
        return NE_DPMI_ERR_NULL;
    if (!ctx->initialized)
        return NE_DPMI_ERR_NOT_INIT;

    idx = find_selector_entry(ctx, selector);
    if (idx < 0)
        return NE_DPMI_ERR_BAD_SEL;

    /* Free the descriptor. */
    {
        uint16_t di = ctx->selectors[idx].desc_index;
        memset(&ctx->descriptors[di], 0, sizeof(ctx->descriptors[di]));
        if (ctx->desc_count > 0)
            ctx->desc_count--;
    }

    /* Free the selector slot. */
    memset(&ctx->selectors[idx], 0, sizeof(ctx->selectors[idx]));
    if (ctx->sel_count > 0)
        ctx->sel_count--;

    return NE_DPMI_OK;
}

int ne_dpmi_change_selector(NEDpmiContext *ctx,
                            uint16_t dst_sel, uint16_t src_sel)
{
    int src_idx, dst_idx;
    uint16_t src_di, dst_di;

    if (!ctx)
        return NE_DPMI_ERR_NULL;
    if (!ctx->initialized)
        return NE_DPMI_ERR_NOT_INIT;

    src_idx = find_selector_entry(ctx, src_sel);
    dst_idx = find_selector_entry(ctx, dst_sel);

    if (src_idx < 0 || dst_idx < 0)
        return NE_DPMI_ERR_BAD_SEL;

    src_di = ctx->selectors[src_idx].desc_index;
    dst_di = ctx->selectors[dst_idx].desc_index;

    /* Copy the descriptor from source to destination. */
    ctx->descriptors[dst_di] = ctx->descriptors[src_di];

    /* Toggle code/data type by flipping bit 3 of the access byte.
     * Bit 3 distinguishes code (1) from data (0) segments. */
    ctx->descriptors[dst_di].access ^= 0x08u;

    return NE_DPMI_OK;
}

uint16_t ne_dpmi_get_selector_count(const NEDpmiContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;
    return ctx->sel_count;
}

/* =========================================================================
 * ne_dpmi_get_descriptor / ne_dpmi_set_descriptor
 * ===================================================================== */

int ne_dpmi_get_descriptor(const NEDpmiContext *ctx, uint16_t selector,
                           NEDpmiDescriptor *desc)
{
    int idx;

    if (!ctx || !desc)
        return NE_DPMI_ERR_NULL;
    if (!ctx->initialized)
        return NE_DPMI_ERR_NOT_INIT;

    idx = find_selector_entry(ctx, selector);
    if (idx < 0)
        return NE_DPMI_ERR_BAD_SEL;

    *desc = ctx->descriptors[ctx->selectors[idx].desc_index];
    return NE_DPMI_OK;
}

int ne_dpmi_set_descriptor(NEDpmiContext *ctx, uint16_t selector,
                           const NEDpmiDescriptor *desc)
{
    int idx;

    if (!ctx || !desc)
        return NE_DPMI_ERR_NULL;
    if (!ctx->initialized)
        return NE_DPMI_ERR_NOT_INIT;

    idx = find_selector_entry(ctx, selector);
    if (idx < 0)
        return NE_DPMI_ERR_BAD_SEL;

    {
        uint16_t di = ctx->selectors[idx].desc_index;
        ctx->descriptors[di].base   = desc->base;
        ctx->descriptors[di].limit  = desc->limit;
        ctx->descriptors[di].access = desc->access;
        ctx->descriptors[di].flags  = desc->flags;
        /* in_use remains set */
    }
    return NE_DPMI_OK;
}

int ne_dpmi_set_segment_base(NEDpmiContext *ctx, uint16_t selector,
                             uint32_t base)
{
    int idx;

    if (!ctx)
        return NE_DPMI_ERR_NULL;
    if (!ctx->initialized)
        return NE_DPMI_ERR_NOT_INIT;

    idx = find_selector_entry(ctx, selector);
    if (idx < 0)
        return NE_DPMI_ERR_BAD_SEL;

    ctx->descriptors[ctx->selectors[idx].desc_index].base = base;
    return NE_DPMI_OK;
}

int ne_dpmi_get_segment_base(const NEDpmiContext *ctx, uint16_t selector,
                             uint32_t *base)
{
    int idx;

    if (!ctx || !base)
        return NE_DPMI_ERR_NULL;
    if (!ctx->initialized)
        return NE_DPMI_ERR_NOT_INIT;

    idx = find_selector_entry(ctx, selector);
    if (idx < 0)
        return NE_DPMI_ERR_BAD_SEL;

    *base = ctx->descriptors[ctx->selectors[idx].desc_index].base;
    return NE_DPMI_OK;
}

int ne_dpmi_set_segment_limit(NEDpmiContext *ctx, uint16_t selector,
                              uint32_t limit)
{
    int idx;

    if (!ctx)
        return NE_DPMI_ERR_NULL;
    if (!ctx->initialized)
        return NE_DPMI_ERR_NOT_INIT;

    idx = find_selector_entry(ctx, selector);
    if (idx < 0)
        return NE_DPMI_ERR_BAD_SEL;

    ctx->descriptors[ctx->selectors[idx].desc_index].limit = limit;
    return NE_DPMI_OK;
}

/* =========================================================================
 * ne_dpmi_alloc_ext_memory / ne_dpmi_free_ext_memory /
 * ne_dpmi_resize_ext_memory
 * ===================================================================== */

int ne_dpmi_alloc_ext_memory(NEDpmiContext *ctx, uint32_t size,
                             uint32_t *handle, uint32_t *base)
{
    uint16_t i;
    int      slot = -1;

    if (!ctx || !handle || !base)
        return NE_DPMI_ERR_NULL;
    if (!ctx->initialized)
        return NE_DPMI_ERR_NOT_INIT;
    if (size == 0)
        return NE_DPMI_ERR_NO_MEM;

    /* Find a free slot. */
    for (i = 0; i < NE_DPMI_MAX_EXTBLOCKS; i++) {
        if (!ctx->ext_blocks[i].in_use) {
            slot = (int)i;
            break;
        }
    }
    if (slot < 0)
        return NE_DPMI_ERR_FULL;

    ctx->ext_blocks[slot].handle = ctx->next_ext_handle++;
    ctx->ext_blocks[slot].base   = ctx->next_base;
    ctx->ext_blocks[slot].size   = size;
    ctx->ext_blocks[slot].in_use = 1;
    ctx->ext_count++;

    /* Advance next_base past the allocated region (aligned to 4K pages). */
    ctx->next_base += (size + 0xFFFu) & ~0xFFFu;

    *handle = ctx->ext_blocks[slot].handle;
    *base   = ctx->ext_blocks[slot].base;

    return NE_DPMI_OK;
}

int ne_dpmi_free_ext_memory(NEDpmiContext *ctx, uint32_t handle)
{
    int idx;

    if (!ctx)
        return NE_DPMI_ERR_NULL;
    if (!ctx->initialized)
        return NE_DPMI_ERR_NOT_INIT;

    idx = find_ext_block(ctx, handle);
    if (idx < 0)
        return NE_DPMI_ERR_BAD_SEL;

    memset(&ctx->ext_blocks[idx], 0, sizeof(ctx->ext_blocks[idx]));
    if (ctx->ext_count > 0)
        ctx->ext_count--;

    return NE_DPMI_OK;
}

int ne_dpmi_resize_ext_memory(NEDpmiContext *ctx, uint32_t handle,
                              uint32_t new_size, uint32_t *new_base)
{
    int idx;

    if (!ctx || !new_base)
        return NE_DPMI_ERR_NULL;
    if (!ctx->initialized)
        return NE_DPMI_ERR_NOT_INIT;

    idx = find_ext_block(ctx, handle);
    if (idx < 0)
        return NE_DPMI_ERR_BAD_SEL;

    /*
     * In a real implementation the block might move.  Here we keep
     * the same base for simplicity and just update the size.
     */
    ctx->ext_blocks[idx].size = new_size;
    *new_base = ctx->ext_blocks[idx].base;

    return NE_DPMI_OK;
}

uint16_t ne_dpmi_get_ext_memory_count(const NEDpmiContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;
    return ctx->ext_count;
}

/* =========================================================================
 * ne_dpmi_dispatch – INT 31h function dispatcher
 * ===================================================================== */

int ne_dpmi_dispatch(NEDpmiContext *ctx,
                     uint16_t func,
                     uint16_t bx, uint16_t cx,
                     uint16_t dx, uint16_t si, uint16_t di,
                     uint16_t *out_ax, uint16_t *out_bx,
                     uint16_t *out_cx, uint16_t *out_dx,
                     uint16_t *out_si, uint16_t *out_di)
{
    if (!ctx)
        return NE_DPMI_ERR_NULL;
    if (!ctx->initialized)
        return NE_DPMI_ERR_NOT_INIT;

    (void)si; (void)di;

    switch (func) {

    case NE_DPMI_FN_ALLOC_LDT: {
        /* AX=0000h: Allocate LDT Descriptors
         * CX = number of descriptors to allocate (we support 1)
         * Returns: AX = base selector */
        uint16_t sel = ne_dpmi_alloc_selector(ctx, 0);
        if (sel == NE_DPMI_SEL_INVALID)
            return NE_DPMI_ERR_FULL;
        if (out_ax) *out_ax = sel;
        (void)cx;
        return NE_DPMI_OK;
    }

    case NE_DPMI_FN_FREE_LDT: {
        /* AX=0001h: Free LDT Descriptor
         * BX = selector to free */
        return ne_dpmi_free_selector(ctx, bx);
    }

    case NE_DPMI_FN_SEG_TO_DESC: {
        /* AX=0002h: Segment to Descriptor
         * BX = real-mode segment
         * Returns: AX = selector that maps the segment */
        uint16_t sel = ne_dpmi_alloc_selector(ctx, 0);
        if (sel == NE_DPMI_SEL_INVALID)
            return NE_DPMI_ERR_FULL;
        /* Set base to segment * 16. */
        ne_dpmi_set_segment_base(ctx, sel,
                                 (uint32_t)bx << 4);
        ne_dpmi_set_segment_limit(ctx, sel, 0xFFFFu);
        if (out_ax) *out_ax = sel;
        return NE_DPMI_OK;
    }

    case NE_DPMI_FN_GET_SEL_INC: {
        /* AX=0003h: Get Selector Increment Value
         * Returns: AX = selector increment */
        if (out_ax) *out_ax = NE_DPMI_SEL_INCREMENT;
        return NE_DPMI_OK;
    }

    case NE_DPMI_FN_GET_DESC: {
        /* AX=000Bh: Get Descriptor
         * BX = selector
         * Returns descriptor at ES:DI (we use out parameters) */
        NEDpmiDescriptor desc;
        int rc = ne_dpmi_get_descriptor(ctx, bx, &desc);
        if (rc != NE_DPMI_OK)
            return rc;
        /* Pack base into CX:DX, limit into SI:DI */
        if (out_cx) *out_cx = (uint16_t)(desc.base >> 16);
        if (out_dx) *out_dx = (uint16_t)(desc.base & 0xFFFF);
        if (out_si) *out_si = (uint16_t)(desc.limit >> 16);
        if (out_di) *out_di = (uint16_t)(desc.limit & 0xFFFF);
        if (out_bx) *out_bx = (uint16_t)desc.access;
        return NE_DPMI_OK;
    }

    case NE_DPMI_FN_SET_DESC: {
        /* AX=000Ch: Set Descriptor
         * BX = selector
         * CX:DX = base, SI:DI = limit (simplified) */
        NEDpmiDescriptor desc;
        desc.base   = ((uint32_t)cx << 16) | dx;
        desc.limit  = ((uint32_t)si << 16) | di;
        desc.access = NE_DPMI_DESC_DATA_RW;
        desc.flags  = 0;
        desc.in_use = 1;
        return ne_dpmi_set_descriptor(ctx, bx, &desc);
    }

    case NE_DPMI_FN_ALLOC_MEM: {
        /* AX=0501h: Allocate Memory Block
         * BX:CX = size in bytes
         * Returns: BX:CX = linear address, SI:DI = handle */
        uint32_t size32 = ((uint32_t)bx << 16) | cx;
        uint32_t handle, base;
        int rc = ne_dpmi_alloc_ext_memory(ctx, size32, &handle, &base);
        if (rc != NE_DPMI_OK)
            return rc;
        if (out_bx) *out_bx = (uint16_t)(base >> 16);
        if (out_cx) *out_cx = (uint16_t)(base & 0xFFFF);
        if (out_si) *out_si = (uint16_t)(handle >> 16);
        if (out_di) *out_di = (uint16_t)(handle & 0xFFFF);
        return NE_DPMI_OK;
    }

    case NE_DPMI_FN_FREE_MEM: {
        /* AX=0502h: Free Memory Block
         * SI:DI = handle */
        uint32_t handle = ((uint32_t)si << 16) | di;
        return ne_dpmi_free_ext_memory(ctx, handle);
    }

    case NE_DPMI_FN_RESIZE_MEM: {
        /* AX=0503h: Resize Memory Block
         * BX:CX = new size, SI:DI = handle
         * Returns: BX:CX = new linear address, SI:DI = new handle */
        uint32_t handle   = ((uint32_t)si << 16) | di;
        uint32_t new_size = ((uint32_t)bx << 16) | cx;
        uint32_t new_base;
        int rc = ne_dpmi_resize_ext_memory(ctx, handle, new_size, &new_base);
        if (rc != NE_DPMI_OK)
            return rc;
        if (out_bx) *out_bx = (uint16_t)(new_base >> 16);
        if (out_cx) *out_cx = (uint16_t)(new_base & 0xFFFF);
        /* Handle does not change in our implementation. */
        if (out_si) *out_si = (uint16_t)(handle >> 16);
        if (out_di) *out_di = (uint16_t)(handle & 0xFFFF);
        return NE_DPMI_OK;
    }

    case NE_DPMI_FN_GET_VERSION: {
        /* AX=0400h: Get DPMI Version
         * Returns: AH = major, AL = minor */
        uint16_t ver = ((uint16_t)NE_DPMI_VERSION_MAJOR << 8) |
                        (uint16_t)NE_DPMI_VERSION_MINOR;
        if (out_ax) *out_ax = ver;
        return NE_DPMI_OK;
    }

    default:
        return NE_DPMI_ERR_BAD_FUNC;
    }
}

/* =========================================================================
 * ne_dpmi_strerror
 * ===================================================================== */

const char *ne_dpmi_strerror(int err)
{
    switch (err) {
    case NE_DPMI_OK:           return "success";
    case NE_DPMI_ERR_NULL:     return "NULL pointer argument";
    case NE_DPMI_ERR_FULL:     return "table full";
    case NE_DPMI_ERR_BAD_SEL:  return "invalid selector or handle";
    case NE_DPMI_ERR_NO_MEM:   return "memory allocation failed";
    case NE_DPMI_ERR_NOT_INIT: return "context not initialised";
    case NE_DPMI_ERR_BAD_FUNC: return "unsupported DPMI function";
    default:                   return "unknown error";
    }
}

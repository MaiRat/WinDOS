/*
 * ne_reloc.c - NE (New Executable) relocation management implementation
 *
 * Parses per-segment relocation records from a raw NE file buffer and
 * applies them to the in-memory segment images held by an NELoaderContext.
 *
 * Supported address types : LOBYTE, SEG16, FAR32, OFF16, SEL16, PTR32
 * Supported reloc types   : INTERNAL, IMP_ORD, IMP_NAME, OS_FIXUP (skipped)
 * Chain following         : non-ADDITIVE records follow the linked-list chain
 *                           embedded in the segment data (terminated by
 *                           0xFFFF for 16-bit types, 0xFF for LOBYTE)
 */

#include "ne_reloc.h"
#include "ne_dosalloc.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * Internal byte-order helpers
 * ---------------------------------------------------------------------- */

static uint16_t rl_read_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static void rl_write_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

/* -------------------------------------------------------------------------
 * ne_reloc_parse
 * ---------------------------------------------------------------------- */

int ne_reloc_parse(const uint8_t        *buf,
                   size_t                len,
                   const NEParserContext *parser,
                   NERelocContext        *ctx)
{
    uint16_t i;
    uint16_t k;

    if (!buf || !parser || !ctx)
        return NE_RELOC_ERR_NULL;

    memset(ctx, 0, sizeof(*ctx));

    if (parser->header.segment_count == 0 || !parser->segments)
        return NE_RELOC_OK;

    /*
     * Allocate one NESegRelocTable slot per segment.  ctx->count tracks how
     * many slots are actually populated.
     */
    ctx->tables = (NESegRelocTable *)NE_CALLOC(parser->header.segment_count,
                                            sizeof(NESegRelocTable));
    if (!ctx->tables)
        return NE_RELOC_ERR_ALLOC;

    for (i = 0; i < parser->header.segment_count; i++) {
        const NESegmentDescriptor *sd = &parser->segments[i];
        NESegRelocTable *tbl;
        uint32_t file_off;
        uint32_t data_size;
        uint32_t reloc_off;
        uint16_t count;

        /* Skip segments without the relocation flag or without file data */
        if (!(sd->flags & NE_SEG_RELOC) || sd->offset == 0)
            continue;

        file_off  = (uint32_t)sd->offset << parser->header.align_shift;
        data_size = (sd->length == 0) ? 0x10000u : (uint32_t)sd->length;
        reloc_off = file_off + data_size;

        /* Need at least 2 bytes for the record count field */
        if ((uint32_t)reloc_off + 2u > (uint32_t)len) {
            ne_reloc_free(ctx);
            return NE_RELOC_ERR_IO;
        }

        count = rl_read_u16(buf + reloc_off);
        if (count == 0)
            continue;

        /* Validate that all record bytes are within the buffer */
        if ((uint32_t)reloc_off + 2u + (uint32_t)count * 8u > (uint32_t)len) {
            ne_reloc_free(ctx);
            return NE_RELOC_ERR_IO;
        }

        tbl = &ctx->tables[ctx->count];
        tbl->seg_idx = i;
        tbl->count   = count;
        tbl->records = (NERelocRecord *)NE_CALLOC(count, sizeof(NERelocRecord));
        if (!tbl->records) {
            ne_reloc_free(ctx);
            return NE_RELOC_ERR_ALLOC;
        }

        /* Parse each 8-byte record */
        for (k = 0; k < count; k++) {
            const uint8_t *rec = buf + reloc_off + 2u + (uint32_t)k * 8u;
            tbl->records[k].address_type  = rec[0];
            tbl->records[k].reloc_type    = rec[1];
            tbl->records[k].target_offset = rl_read_u16(rec + 2);
            tbl->records[k].ref1          = rl_read_u16(rec + 4);
            tbl->records[k].ref2          = rl_read_u16(rec + 6);
        }

        ctx->count++;
    }

    return NE_RELOC_OK;
}

/* -------------------------------------------------------------------------
 * patch_location – write a relocation value at a single target offset
 *
 * For non-ADDITIVE (chain) calls the caller must save the chain-next word
 * from data[off] BEFORE invoking this function, as the write will overwrite
 * those bytes.
 *
 * seg_val  : 1-based segment index (used for SEG16, SEL16, FAR32 high word)
 * off_val  : byte offset within the target segment (used for OFF16, FAR32
 *            low word, PTR32, LOBYTE)
 * additive : non-zero → add to existing value; zero → replace
 * ---------------------------------------------------------------------- */
static int patch_location(uint8_t  *data,
                           uint32_t  seg_size,
                           uint32_t  off,
                           uint8_t   addr_type,
                           uint16_t  seg_val,
                           uint16_t  off_val,
                           int       additive)
{
    uint16_t old16;
    uint16_t old_seg;

    if (off >= seg_size)
        return NE_RELOC_ERR_BAD_SEG;

    if (addr_type == NE_RELOC_ADDR_LOBYTE) {
        if (additive)
            data[off] = (uint8_t)(data[off] + (uint8_t)(off_val & 0xFF));
        else
            data[off] = (uint8_t)(off_val & 0xFF);

    } else if (addr_type == NE_RELOC_ADDR_SEG16
            || addr_type == NE_RELOC_ADDR_SEL16) {
        if (off + 2u > seg_size)
            return NE_RELOC_ERR_BAD_SEG;
        old16 = rl_read_u16(data + off);
        if (additive)
            rl_write_u16(data + off, (uint16_t)(old16 + seg_val));
        else
            rl_write_u16(data + off, seg_val);

    } else if (addr_type == NE_RELOC_ADDR_OFF16) {
        if (off + 2u > seg_size)
            return NE_RELOC_ERR_BAD_SEG;
        old16 = rl_read_u16(data + off);
        if (additive)
            rl_write_u16(data + off, (uint16_t)(old16 + off_val));
        else
            rl_write_u16(data + off, off_val);

    } else if (addr_type == NE_RELOC_ADDR_FAR32) {
        if (off + 4u > seg_size)
            return NE_RELOC_ERR_BAD_SEG;
        old16    = rl_read_u16(data + off);
        old_seg  = rl_read_u16(data + off + 2u);
        if (additive) {
            rl_write_u16(data + off,      (uint16_t)(old16   + off_val));
            rl_write_u16(data + off + 2u, (uint16_t)(old_seg + seg_val));
        } else {
            rl_write_u16(data + off,      off_val);
            rl_write_u16(data + off + 2u, seg_val);
        }

    } else if (addr_type == NE_RELOC_ADDR_PTR32) {
        if (off + 4u > seg_size)
            return NE_RELOC_ERR_BAD_SEG;
        old16 = rl_read_u16(data + off);
        if (additive)
            rl_write_u16(data + off, (uint16_t)(old16 + off_val));
        else {
            rl_write_u16(data + off,      off_val);
            rl_write_u16(data + off + 2u, 0); /* clear upper word */
        }

    } else {
        return NE_RELOC_ERR_ADDR_TYPE;
    }

    return NE_RELOC_OK;
}

/* -------------------------------------------------------------------------
 * ne_reloc_apply
 * ---------------------------------------------------------------------- */

int ne_reloc_apply(NELoaderContext        *loader,
                   const NERelocContext   *reloc_ctx,
                   const NEParserContext  *parser,
                   NEImportResolver        resolver,
                   void                   *resolver_data)
{
    uint16_t t;
    uint16_t k;

    if (!loader || !reloc_ctx || !parser)
        return NE_RELOC_ERR_NULL;

    for (t = 0; t < reloc_ctx->count; t++) {
        const NESegRelocTable *tbl = &reloc_ctx->tables[t];
        uint16_t seg_idx = tbl->seg_idx;
        uint8_t  *seg_data;
        uint32_t  seg_size;

        if (seg_idx >= loader->count)
            return NE_RELOC_ERR_BAD_SEG;

        seg_data = loader->segments[seg_idx].data;
        seg_size = loader->segments[seg_idx].alloc_size;

        for (k = 0; k < tbl->count; k++) {
            const NERelocRecord *rec = &tbl->records[k];
            uint8_t  rtype    = (uint8_t)(rec->reloc_type & 0x03u);
            int      additive = (rec->reloc_type & NE_RELOC_FLAG_ADDITIVE) != 0;
            uint16_t seg_val  = 0;
            uint16_t off_val  = 0;
            int      rc;
            uint32_t next;
            uint16_t chain_next;
            uint8_t  chain_next8;

            /* ---- Compute the value to patch ---- */
            if (rtype == NE_RELOC_TYPE_INTERNAL) {
                /*
                 * ref1 = target segment number (1-based).
                 * The "segment value" we write is the 1-based segment index
                 * (in a real DOS loader this would be the paragraph address).
                 * ref2 = byte offset within that segment (for FAR / OFF16).
                 */
                if (rec->ref1 == 0 || rec->ref1 > parser->header.segment_count)
                    return NE_RELOC_ERR_BAD_SEG;
                seg_val = rec->ref1;
                off_val = rec->ref2;

            } else if (rtype == NE_RELOC_TYPE_IMP_ORD
                    || rtype == NE_RELOC_TYPE_IMP_NAME) {
                if (!resolver)
                    return NE_RELOC_ERR_UNRESOLVED;
                rc = resolver(rec->ref1,
                              rec->ref2,
                              (rtype == NE_RELOC_TYPE_IMP_NAME) ? 1 : 0,
                              parser->imported_names,
                              parser->imported_names_size,
                              &seg_val,
                              &off_val,
                              resolver_data);
                if (rc != NE_RELOC_OK)
                    return NE_RELOC_ERR_UNRESOLVED;

            } else {
                /* OS_FIXUP: silently skip */
                continue;
            }

            /* ---- Apply the patch ---- */
            if (additive) {
                rc = patch_location(seg_data, seg_size,
                                    (uint32_t)rec->target_offset,
                                    rec->address_type,
                                    seg_val, off_val, 1);
                if (rc != NE_RELOC_OK)
                    return rc;

            } else if (rec->address_type == NE_RELOC_ADDR_LOBYTE) {
                /*
                 * 1-byte chain: each byte gives the next offset to patch;
                 * 0xFF marks the end of the chain.
                 */
                next = (uint32_t)rec->target_offset;
                while (next != 0xFFu) {
                    if (next >= seg_size)
                        return NE_RELOC_ERR_BAD_SEG;
                    chain_next8 = seg_data[next];
                    rc = patch_location(seg_data, seg_size, next,
                                        rec->address_type,
                                        seg_val, off_val, 0);
                    if (rc != NE_RELOC_OK)
                        return rc;
                    next = (uint32_t)chain_next8;
                }

            } else {
                /*
                 * 2-byte chain: each word gives the next offset to patch;
                 * 0xFFFF marks the end of the chain.
                 */
                next = (uint32_t)rec->target_offset;
                while (next != 0xFFFFu) {
                    if (next + 2u > seg_size)
                        return NE_RELOC_ERR_BAD_SEG;
                    /* Save chain pointer before we overwrite those bytes */
                    chain_next = rl_read_u16(seg_data + next);
                    rc = patch_location(seg_data, seg_size, next,
                                        rec->address_type,
                                        seg_val, off_val, 0);
                    if (rc != NE_RELOC_OK)
                        return rc;
                    next = (uint32_t)chain_next;
                }
            }
        }
    }

    return NE_RELOC_OK;
}

/* -------------------------------------------------------------------------
 * ne_reloc_free
 * ---------------------------------------------------------------------- */

void ne_reloc_free(NERelocContext *ctx)
{
    uint16_t i;

    if (!ctx)
        return;

    if (ctx->tables) {
        for (i = 0; i < ctx->count; i++)
            NE_FREE(ctx->tables[i].records);
        NE_FREE(ctx->tables);
    }

    memset(ctx, 0, sizeof(*ctx));
}

/* -------------------------------------------------------------------------
 * ne_reloc_strerror
 * ---------------------------------------------------------------------- */

const char *ne_reloc_strerror(int err)
{
    switch (err) {
    case NE_RELOC_OK:              return "success";
    case NE_RELOC_ERR_NULL:        return "NULL argument";
    case NE_RELOC_ERR_ALLOC:       return "memory allocation failure";
    case NE_RELOC_ERR_IO:          return "relocation data outside file bounds";
    case NE_RELOC_ERR_BAD_SEG:     return "segment index out of range";
    case NE_RELOC_ERR_UNRESOLVED:  return "unresolvable imported reference";
    case NE_RELOC_ERR_ADDR_TYPE:   return "unsupported address type";
    default:                       return "unknown error";
    }
}

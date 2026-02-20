/*
 * ne_parser.c - NE (New Executable) file format parser implementation
 *
 * Parses the DOS MZ stub followed by the 16-bit Windows NE header, segment
 * table, resource table, imported-names table, and entry table.
 */

#include "ne_parser.h"
#include "ne_dosalloc.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/* Read a little-endian uint16 from a buffer position */
static uint16_t read_u16(const uint8_t *buf)
{
    return (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
}

/* Read a little-endian uint32 from a buffer position */
static uint32_t read_u32(const uint8_t *buf)
{
    return (uint32_t)(buf[0]
                    | ((uint32_t)buf[1] <<  8)
                    | ((uint32_t)buf[2] << 16)
                    | ((uint32_t)buf[3] << 24));
}

/* Populate an MZHeader struct from raw bytes */
static void parse_mz_header(const uint8_t *buf, MZHeader *hdr)
{
    hdr->magic                = read_u16(buf + 0x00);
    hdr->bytes_in_last_block  = read_u16(buf + 0x02);
    hdr->blocks_in_file       = read_u16(buf + 0x04);
    hdr->num_relocs           = read_u16(buf + 0x06);
    hdr->header_paragraphs    = read_u16(buf + 0x08);
    hdr->min_extra_paragraphs = read_u16(buf + 0x0A);
    hdr->max_extra_paragraphs = read_u16(buf + 0x0C);
    hdr->initial_ss           = read_u16(buf + 0x0E);
    hdr->initial_sp           = read_u16(buf + 0x10);
    hdr->checksum             = read_u16(buf + 0x12);
    hdr->initial_ip           = read_u16(buf + 0x14);
    hdr->initial_cs           = read_u16(buf + 0x16);
    hdr->reloc_table_offset   = read_u16(buf + 0x18);
    hdr->overlay_number       = read_u16(buf + 0x1A);
    hdr->reserved[0]          = read_u16(buf + 0x1C);
    hdr->reserved[1]          = read_u16(buf + 0x1E);
    hdr->reserved[2]          = read_u16(buf + 0x20);
    hdr->reserved[3]          = read_u16(buf + 0x22);
    hdr->oem_id               = read_u16(buf + 0x24);
    hdr->oem_info             = read_u16(buf + 0x26);
    /* reserved2[0..9] at 0x28-0x3B */
    hdr->ne_offset            = read_u32(buf + 0x3C);
}

/* Populate an NEHeader struct from raw bytes at the start of the NE header */
static void parse_ne_header(const uint8_t *buf, NEHeader *hdr)
{
    hdr->magic                      = read_u16(buf + 0x00);
    hdr->linker_major               = buf[0x02];
    hdr->linker_minor               = buf[0x03];
    hdr->entry_table_offset         = read_u16(buf + 0x04);
    hdr->entry_table_length         = read_u16(buf + 0x06);
    hdr->crc32                      = read_u32(buf + 0x08);
    hdr->program_flags              = buf[0x0C];
    hdr->app_flags                  = buf[0x0D];
    hdr->auto_data_seg              = read_u16(buf + 0x0E);
    hdr->heap_size                  = read_u16(buf + 0x10);
    hdr->stack_size                 = read_u16(buf + 0x12);
    hdr->initial_ip                 = read_u16(buf + 0x14);
    hdr->initial_cs                 = read_u16(buf + 0x16);
    hdr->initial_sp                 = read_u16(buf + 0x18);
    hdr->initial_ss                 = read_u16(buf + 0x1A);
    hdr->segment_count              = read_u16(buf + 0x1C);
    hdr->module_ref_count           = read_u16(buf + 0x1E);
    hdr->nonresident_name_size      = read_u16(buf + 0x20);
    hdr->segment_table_offset       = read_u16(buf + 0x22);
    hdr->resource_table_offset      = read_u16(buf + 0x24);
    hdr->resident_name_table_offset = read_u16(buf + 0x26);
    hdr->module_ref_table_offset    = read_u16(buf + 0x28);
    hdr->imported_names_offset      = read_u16(buf + 0x2A);
    hdr->nonresident_name_offset    = read_u32(buf + 0x2C);
    hdr->movable_entry_count        = read_u16(buf + 0x30);
    hdr->align_shift                = read_u16(buf + 0x32);
    hdr->resource_seg_count         = read_u16(buf + 0x34);
    hdr->target_os                  = buf[0x36];
    hdr->os2_flags                  = buf[0x37];
    hdr->return_thunks_offset       = read_u16(buf + 0x38);
    hdr->seg_ref_thunks_offset      = read_u16(buf + 0x3A);
    hdr->min_code_swap_size         = read_u16(buf + 0x3C);
    hdr->expected_win_ver_minor     = buf[0x3E];
    hdr->expected_win_ver_major     = buf[0x3F];
}

/* Populate one NESegmentDescriptor from 8 bytes */
static void parse_segment_descriptor(const uint8_t *buf, NESegmentDescriptor *seg)
{
    seg->offset    = read_u16(buf + 0);
    seg->length    = read_u16(buf + 2);
    seg->flags     = read_u16(buf + 4);
    seg->min_alloc = read_u16(buf + 6);
}

/* Allocate and copy a region from the file buffer */
static uint8_t *dup_region(const uint8_t *buf, size_t buf_len,
                            uint32_t offset, uint16_t size)
{
    uint8_t *p;
    if (size == 0)
        return NULL;
    if ((uint32_t)offset + size > buf_len)
        return NULL;
    p = (uint8_t *)NE_MALLOC(size);
    if (p)
        memcpy(p, buf + offset, size);
    return p;
}

/* -------------------------------------------------------------------------
 * Core buffer parser
 * ---------------------------------------------------------------------- */

/*
 * Minimum sizes we need before we can read a field.
 * The MZHeader is 64 bytes; the NEHeader is 64 bytes.
 */
#define MZ_HEADER_MIN_SIZE  64u
#define NE_HEADER_SIZE      64u
#define NE_SEG_DESC_SIZE     8u

int ne_parse_buffer(const uint8_t *buf, size_t len, NEParserContext *ctx)
{
    MZHeader   mz;
    uint32_t   ne_off;
    uint32_t   seg_table_abs;
    uint32_t   res_table_abs;
    uint32_t   entry_table_abs;
    uint32_t   imp_names_abs;
    uint16_t   i;

    if (!buf || !ctx)
        return NE_ERR_NULL_ARG;

    memset(ctx, 0, sizeof(*ctx));

    /* ---- MZ header ---- */
    if (len < MZ_HEADER_MIN_SIZE)
        return NE_ERR_NOT_MZ;

    parse_mz_header(buf, &mz);

    if (mz.magic != MZ_MAGIC)
        return NE_ERR_NOT_MZ;

    ne_off = mz.ne_offset;

    /* ---- Validate NE header offset ---- */
    if (ne_off < MZ_HEADER_MIN_SIZE || (uint32_t)ne_off + NE_HEADER_SIZE > len)
        return NE_ERR_BAD_OFFSET;

    /* ---- NE header ---- */
    parse_ne_header(buf + ne_off, &ctx->header);

    if (ctx->header.magic != NE_MAGIC)
        return NE_ERR_NOT_NE;

    ctx->ne_offset = ne_off;

    /* ---- Validate segment table ---- */
    seg_table_abs = ne_off + ctx->header.segment_table_offset;
    if (ctx->header.segment_count > 0) {
        uint32_t seg_table_end = (uint32_t)seg_table_abs
                               + (uint32_t)ctx->header.segment_count * NE_SEG_DESC_SIZE;
        if (seg_table_end > len)
            return NE_ERR_BAD_OFFSET;
    }

    /* ---- Parse segment table ---- */
    if (ctx->header.segment_count > 0) {
        ctx->segments = (NESegmentDescriptor *)NE_CALLOC(
                            ctx->header.segment_count,
                            sizeof(NESegmentDescriptor));
        if (!ctx->segments) {
            ne_free(ctx);
            return NE_ERR_ALLOC;
        }
        for (i = 0; i < ctx->header.segment_count; i++) {
            parse_segment_descriptor(
                buf + seg_table_abs + (uint32_t)i * NE_SEG_DESC_SIZE,
                &ctx->segments[i]);
        }
    }

    /* ---- Resource table ---- */
    res_table_abs = ne_off + ctx->header.resource_table_offset;
    if (ctx->header.resource_table_offset < ctx->header.segment_table_offset
            + (uint32_t)ctx->header.segment_count * NE_SEG_DESC_SIZE) {
        /* resource table precedes or overlaps segment table – skip */
        ctx->resource_size = 0;
    } else {
        /* size = distance to next table (resident names) or entry table */
        uint16_t res_size = 0;
        if (ctx->header.resident_name_table_offset > ctx->header.resource_table_offset)
            res_size = (uint16_t)(ctx->header.resident_name_table_offset
                                  - ctx->header.resource_table_offset);
        ctx->resource_size = res_size;
        if (res_size > 0) {
            if ((uint32_t)res_table_abs + res_size > len) {
                ne_free(ctx);
                return NE_ERR_BAD_OFFSET;
            }
            ctx->resource_data = dup_region(buf, len, res_table_abs, res_size);
            if (!ctx->resource_data) {
                ne_free(ctx);
                return NE_ERR_ALLOC;
            }
        }
    }

    /* ---- Entry table ---- */
    entry_table_abs = ne_off + ctx->header.entry_table_offset;
    if (ctx->header.entry_table_length > 0) {
        if ((uint32_t)entry_table_abs + ctx->header.entry_table_length > len) {
            ne_free(ctx);
            return NE_ERR_BAD_OFFSET;
        }
        ctx->entry_size = ctx->header.entry_table_length;
        ctx->entry_data = dup_region(buf, len,
                                     entry_table_abs, ctx->entry_size);
        if (!ctx->entry_data) {
            ne_free(ctx);
            return NE_ERR_ALLOC;
        }
    }

    /* ---- Imported-names table ---- */
    imp_names_abs = ne_off + ctx->header.imported_names_offset;
    if (ctx->header.imported_names_offset > 0
            && ctx->header.module_ref_table_offset > 0
            && ctx->header.module_ref_table_offset > ctx->header.imported_names_offset) {
        uint16_t imp_size = (uint16_t)(ctx->header.module_ref_table_offset
                                       - ctx->header.imported_names_offset);
        if (imp_size > 0 && (uint32_t)imp_names_abs + imp_size <= len) {
            ctx->imported_names_size = imp_size;
            ctx->imported_names = dup_region(buf, len, imp_names_abs, imp_size);
            if (!ctx->imported_names) {
                ne_free(ctx);
                return NE_ERR_ALLOC;
            }
        }
    }

    return NE_OK;
}

/* -------------------------------------------------------------------------
 * File-based entry point
 * ---------------------------------------------------------------------- */

int ne_parse_file(const char *path, NEParserContext *ctx)
{
    FILE    *fp;
    long     file_len;
    uint8_t *buf;
    int      ret;

    if (!path || !ctx)
        return NE_ERR_NULL_ARG;

    fp = fopen(path, "rb");
    if (!fp)
        return NE_ERR_IO;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NE_ERR_IO;
    }

    file_len = ftell(fp);
    if (file_len <= 0) {
        fclose(fp);
        return NE_ERR_IO;
    }

    rewind(fp);

    buf = (uint8_t *)NE_MALLOC((size_t)file_len);
    if (!buf) {
        fclose(fp);
        return NE_ERR_ALLOC;
    }

    if (fread(buf, 1, (size_t)file_len, fp) != (size_t)file_len) {
        NE_FREE(buf);
        fclose(fp);
        return NE_ERR_IO;
    }
    fclose(fp);

    ret = ne_parse_buffer(buf, (size_t)file_len, ctx);
    NE_FREE(buf);
    return ret;
}

/* -------------------------------------------------------------------------
 * Resource cleanup
 * ---------------------------------------------------------------------- */

void ne_free(NEParserContext *ctx)
{
    if (!ctx)
        return;
    NE_FREE(ctx->segments);
    NE_FREE(ctx->resource_data);
    NE_FREE(ctx->entry_data);
    NE_FREE(ctx->imported_names);
    memset(ctx, 0, sizeof(*ctx));
}

/* -------------------------------------------------------------------------
 * Human-readable output
 * ---------------------------------------------------------------------- */

static const char *target_os_name(uint8_t os)
{
    switch (os) {
    case NE_OS_UNKNOWN: return "Unknown";
    case NE_OS_OS2:     return "OS/2";
    case NE_OS_WINDOWS: return "Windows";
    case NE_OS_DOS4:    return "DOS 4.x";
    case NE_OS_WIN386:  return "Win32s/386";
    default:            return "Other";
    }
}

void ne_print_info(const NEParserContext *ctx, FILE *out)
{
    uint16_t i;
    const NEHeader *h;

    if (!ctx || !out)
        return;

    h = &ctx->header;

    fprintf(out, "=== NE Executable Information ===\n");
    fprintf(out, "NE header offset   : 0x%08X\n", ctx->ne_offset);
    fprintf(out, "Linker version     : %u.%u\n",
            h->linker_major, h->linker_minor);
    fprintf(out, "Target OS          : %s (%u)\n",
            target_os_name(h->target_os), h->target_os);
    fprintf(out, "Expected Win ver   : %u.%u\n",
            h->expected_win_ver_major, h->expected_win_ver_minor);
    fprintf(out, "Program flags      : 0x%02X\n", h->program_flags);
    fprintf(out, "Application flags  : 0x%02X%s\n",
            h->app_flags,
            (h->app_flags & NE_AFLAG_DLL) ? " (DLL)" : "");
    fprintf(out, "Segment count      : %u\n",   h->segment_count);
    fprintf(out, "Module ref count   : %u\n",   h->module_ref_count);
    fprintf(out, "Auto data segment  : %u\n",   h->auto_data_seg);
    fprintf(out, "Heap size          : %u bytes\n", h->heap_size);
    fprintf(out, "Stack size         : %u bytes\n", h->stack_size);
    fprintf(out, "Entry point        : CS=%u  IP=0x%04X\n",
            h->initial_cs, h->initial_ip);
    fprintf(out, "Initial stack      : SS=%u  SP=0x%04X\n",
            h->initial_ss, h->initial_sp);
    fprintf(out, "Align shift        : %u  (sector size = %u bytes)\n",
            h->align_shift, (h->align_shift ? (1u << h->align_shift) : 0u));
    fprintf(out, "Entry table size   : %u bytes\n", h->entry_table_length);
    fprintf(out, "Movable entries    : %u\n",   h->movable_entry_count);
    fprintf(out, "Resource segs      : %u\n",   h->resource_seg_count);
    fprintf(out, "Non-res name size  : %u bytes\n", h->nonresident_name_size);
    fprintf(out, "\n");

    if (h->segment_count > 0) {
        fprintf(out, "--- Segment Table (%u entries) ---\n", h->segment_count);
        fprintf(out, "  %-4s  %-8s  %-8s  %-6s  %-8s  %s\n",
                "Idx", "Offset", "Length", "Flags", "MinAlloc", "Type");
        for (i = 0; i < h->segment_count; i++) {
            const NESegmentDescriptor *s = &ctx->segments[i];
            uint32_t file_off = (uint32_t)s->offset << h->align_shift;
            fprintf(out, "  %-4u  0x%06X  %-8u  0x%04X  %-8u  %s\n",
                    i + 1,
                    file_off,
                    s->length  == 0 ? 65536u : s->length,
                    s->flags,
                    s->min_alloc == 0 ? 65536u : s->min_alloc,
                    (s->flags & NE_SEG_DATA) ? "DATA" : "CODE");
        }
        fprintf(out, "\n");
    }
}

/* -------------------------------------------------------------------------
 * Error string
 * ---------------------------------------------------------------------- */

const char *ne_strerror(int err)
{
    switch (err) {
    case NE_OK:              return "success";
    case NE_ERR_IO:          return "I/O error";
    case NE_ERR_NOT_MZ:      return "not an MZ executable (bad magic)";
    case NE_ERR_NOT_NE:      return "not an NE executable (bad magic)";
    case NE_ERR_BAD_OFFSET:  return "table offset out of file bounds";
    case NE_ERR_ALLOC:       return "memory allocation failure";
    case NE_ERR_BAD_HEADER:  return "invalid header field";
    case NE_ERR_NULL_ARG:    return "NULL argument";
    default:                 return "unknown error";
    }
}

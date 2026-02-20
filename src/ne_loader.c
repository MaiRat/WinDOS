/*
 * ne_loader.c - NE (New Executable) segment loader implementation
 *
 * Allocates memory for each NE segment, copies the file-backed data into
 * those buffers, and validates the module entry point.
 */

#include "ne_loader.h"

#include <stdlib.h>
#include <string.h>

#ifdef __WATCOMC__
#include <dos.h>
#include <i86.h>

/*
 * DOS INT 21h memory allocation wrappers for the loader.
 */
static uint16_t ld_bytes_to_paras(uint32_t bytes)
{
    return (uint16_t)((bytes + 15u) >> 4);
}

static void __far *ld_dos_alloc(uint32_t size)
{
    union REGS   r;
    struct SREGS sr;

    if (size == 0)
        return NULL;

    r.h.ah = 0x48;
    r.x.bx = ld_bytes_to_paras(size);
    intdosx(&r, &r, &sr);

    if (r.x.cflag)
        return NULL;

    return MK_FP(r.x.ax, 0);
}

static void ld_dos_free(void __far *ptr)
{
    union REGS   r;
    struct SREGS sr;

    if (ptr == NULL)
        return;

    segread(&sr);
    sr.es  = FP_SEG(ptr);
    r.h.ah = 0x49;
    intdosx(&r, &r, &sr);
}

static void __far *ld_dos_calloc(uint32_t count, uint32_t size)
{
    uint32_t total = count * size;
    void __far *p = ld_dos_alloc(total);
    if (p)
        _fmemset(p, 0, (size_t)total);
    return p;
}

#define NE_LD_MALLOC(sz)      ld_dos_alloc((uint32_t)(sz))
#define NE_LD_CALLOC(n, sz)   ld_dos_calloc((uint32_t)(n), (uint32_t)(sz))
#define NE_LD_FREE(p)         ld_dos_free(p)

#else /* POSIX host */

#define NE_LD_MALLOC(sz)      malloc((size_t)(sz))
#define NE_LD_CALLOC(n, sz)   calloc((size_t)(n), (size_t)(sz))
#define NE_LD_FREE(p)         free(p)

#endif /* __WATCOMC__ */

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/*
 * Resolve the 16-bit NE "0 means 64 KB" convention.
 * Returns the actual byte count as a uint32_t.
 */
static uint32_t resolve_seg_size(uint16_t raw)
{
    return (raw == 0) ? 0x10000u : (uint32_t)raw;
}

/* -------------------------------------------------------------------------
 * Core loader
 * ---------------------------------------------------------------------- */

int ne_load_buffer(const uint8_t *buf, size_t len,
                   const NEParserContext *parser,
                   NELoaderContext *loader)
{
    const NEHeader *hdr;
    uint16_t i;

    if (!buf || !parser || !loader)
        return NE_LOAD_ERR_NULL;

    memset(loader, 0, sizeof(*loader));

    hdr = &parser->header;

    /* Nothing to do when there are no segments */
    if (hdr->segment_count == 0)
        goto validate_entry;

    loader->segments = (NELoadedSegment *)NE_LD_CALLOC(hdr->segment_count,
                                                 sizeof(NELoadedSegment));
    if (!loader->segments)
        return NE_LOAD_ERR_NOMEM;

    loader->count = hdr->segment_count;

    for (i = 0; i < hdr->segment_count; i++) {
        const NESegmentDescriptor *sd = &parser->segments[i];
        NELoadedSegment *ls = &loader->segments[i];
        uint32_t file_off;
        uint32_t data_sz;
        uint32_t alloc_sz;

        /* File byte offset of this segment's data (0 means no file data) */
        file_off = (sd->offset != 0)
                   ? ((uint32_t)sd->offset << hdr->align_shift)
                   : 0u;

        /* Actual sizes: 0 in the NE field means 64 KB */
        data_sz  = resolve_seg_size(sd->length);
        alloc_sz = resolve_seg_size(sd->min_alloc);
        /* Allocation must be at least as large as the on-disk image */
        if (alloc_sz < data_sz)
            alloc_sz = data_sz;

        ls->flags     = sd->flags;
        ls->file_off  = file_off;
        ls->alloc_size = alloc_sz;
        ls->data_size  = 0;

        /* Allocate and zero-fill the segment buffer */
        ls->data = (uint8_t *)NE_LD_CALLOC(1, alloc_sz);
        if (!ls->data) {
            ne_loader_free(loader);
            return NE_LOAD_ERR_NOMEM;
        }

        /* Copy file-backed segment data when the segment has file content */
        if (sd->offset != 0) {
            if ((uint32_t)file_off + data_sz > (uint32_t)len) {
                ne_loader_free(loader);
                return NE_LOAD_ERR_IO;
            }
            memcpy(ls->data, buf + file_off, data_sz);
            ls->data_size = data_sz;
        }
    }

validate_entry:
    /*
     * Verify that the entry-point CS:IP lies within the bounds of the
     * designated code segment.  initial_cs == 0 means no entry point
     * (typical for DLLs), so skip the check in that case.
     */
    if (hdr->initial_cs != 0) {
        uint16_t cs_idx = (uint16_t)(hdr->initial_cs - 1u);
        if (cs_idx >= loader->count) {
            ne_loader_free(loader);
            return NE_LOAD_ERR_BOUNDS;
        }
        if ((uint32_t)hdr->initial_ip >= loader->segments[cs_idx].alloc_size) {
            ne_loader_free(loader);
            return NE_LOAD_ERR_BOUNDS;
        }
    }

    return NE_LOAD_OK;
}

/* -------------------------------------------------------------------------
 * File-based entry point
 * ---------------------------------------------------------------------- */

int ne_load_file(const char *path,
                 const NEParserContext *parser,
                 NELoaderContext *loader)
{
    FILE    *fp;
    long     file_len;
    uint8_t *buf;
    int      ret;

    if (!path || !parser || !loader)
        return NE_LOAD_ERR_NULL;

    fp = fopen(path, "rb");
    if (!fp)
        return NE_LOAD_ERR_IO;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NE_LOAD_ERR_IO;
    }

    file_len = ftell(fp);
    if (file_len <= 0) {
        fclose(fp);
        return NE_LOAD_ERR_IO;
    }

    rewind(fp);

    buf = (uint8_t *)NE_LD_MALLOC((size_t)file_len);
    if (!buf) {
        fclose(fp);
        return NE_LOAD_ERR_NOMEM;
    }

    if (fread(buf, 1, (size_t)file_len, fp) != (size_t)file_len) {
        NE_LD_FREE(buf);
        fclose(fp);
        return NE_LOAD_ERR_IO;
    }
    fclose(fp);

    ret = ne_load_buffer(buf, (size_t)file_len, parser, loader);
    NE_LD_FREE(buf);
    return ret;
}

/* -------------------------------------------------------------------------
 * Resource cleanup
 * ---------------------------------------------------------------------- */

void ne_loader_free(NELoaderContext *loader)
{
    uint16_t i;

    if (!loader)
        return;

    if (loader->segments) {
        for (i = 0; i < loader->count; i++)
            NE_LD_FREE(loader->segments[i].data);
        NE_LD_FREE(loader->segments);
    }

    memset(loader, 0, sizeof(*loader));
}

/* -------------------------------------------------------------------------
 * Human-readable diagnostics
 * ---------------------------------------------------------------------- */

void ne_loader_print_info(const NELoaderContext *loader,
                          const NEParserContext *parser,
                          FILE *out)
{
    uint16_t i;

    if (!loader || !out)
        return;

    fprintf(out, "=== NE Loader Diagnostics ===\n");
    fprintf(out, "Segments loaded : %u\n", loader->count);

    if (loader->count > 0 && loader->segments) {
        fprintf(out, "  %-4s  %-18s  %-10s  %-10s  %s\n",
                "Idx", "Host Address", "AllocSize", "FileBytes", "Type");
        for (i = 0; i < loader->count; i++) {
            const NELoadedSegment *ls = &loader->segments[i];
            fprintf(out, "  %-4u  %-18p  %-10lu  %-10lu  %s\n",
                    (unsigned)(i + 1u),
                    (void *)ls->data,
                    (unsigned long)ls->alloc_size,
                    (unsigned long)ls->data_size,
                    (ls->flags & NE_SEG_DATA) ? "DATA" : "CODE");
        }
    }

    if (parser && parser->header.initial_cs != 0) {
        fprintf(out, "Entry point     : CS=%u  IP=0x%04X\n",
                parser->header.initial_cs,
                parser->header.initial_ip);
    }

    fprintf(out, "\n");
}

/* -------------------------------------------------------------------------
 * Error string
 * ---------------------------------------------------------------------- */

const char *ne_loader_strerror(int err)
{
    switch (err) {
    case NE_LOAD_OK:          return "success";
    case NE_LOAD_ERR_NULL:    return "NULL argument";
    case NE_LOAD_ERR_NOMEM:   return "insufficient memory";
    case NE_LOAD_ERR_IO:      return "I/O error or segment data out of file bounds";
    case NE_LOAD_ERR_BOUNDS:  return "entry-point offset outside segment bounds";
    default:                  return "unknown error";
    }
}

/*
 * ne_loader.h - NE (New Executable) segment loader
 *
 * Loads the code and data segments described by a parsed NEParserContext into
 * heap-allocated memory buffers, verifies the entry-point offset, and
 * provides basic diagnostics.
 *
 * This module implements Step 2 of the WinDOS kernel-replacement roadmap.
 * The host-side implementation uses malloc/free; on a real 16-bit DOS target
 * these would be replaced by the appropriate DOS memory allocation calls
 * (e.g. INT 21h / AH=48h with the Watcom toolchain).
 *
 * Reference: Microsoft "New Executable" format specification.
 */

#ifndef NE_LOADER_H
#define NE_LOADER_H

#include "ne_parser.h"

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_LOAD_OK          0
#define NE_LOAD_ERR_NULL   -1   /* NULL pointer argument                   */
#define NE_LOAD_ERR_NOMEM  -2   /* memory allocation failure               */
#define NE_LOAD_ERR_IO     -3   /* file I/O error or data out of bounds    */
#define NE_LOAD_ERR_BOUNDS -4   /* entry-point offset outside segment      */

/* -------------------------------------------------------------------------
 * Loaded segment descriptor
 * ---------------------------------------------------------------------- */

/*
 * NELoadedSegment - the in-memory image of one NE segment.
 *
 * 'data' points to an alloc_size-byte heap buffer that holds the segment
 * image.  Bytes [0, data_size) were read from the file; the rest are zero.
 * A data_size of 0 means the segment has no file backing (BSS-like).
 */
typedef struct {
    uint8_t  *data;       /* heap-allocated segment image (alloc_size bytes) */
    uint32_t  file_off;   /* source byte offset within the file (0 if none)  */
    uint32_t  alloc_size; /* total bytes allocated for this segment           */
    uint32_t  data_size;  /* bytes read from the file (may be 0)             */
    uint16_t  flags;      /* NE_SEG_* flags copied from the segment table    */
} NELoadedSegment;

/* -------------------------------------------------------------------------
 * Loader context
 * ---------------------------------------------------------------------- */

/*
 * NELoaderContext - result of loading all segments of an NE module.
 *
 * Caller must call ne_loader_free() when done.
 */
typedef struct {
    NELoadedSegment *segments; /* heap-allocated array [0 .. count-1] */
    uint16_t         count;    /* number of loaded segments            */
} NELoaderContext;

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/*
 * ne_load_buffer - load NE segments from an in-memory file image.
 *
 * 'buf' / 'len' : the complete NE file image (same buffer passed to
 *                 ne_parse_buffer).
 * 'parser'      : fully-populated context from ne_parse_buffer / ne_parse_file.
 * 'loader'      : output context; caller must call ne_loader_free() on success.
 *
 * Returns NE_LOAD_OK on success or a negative NE_LOAD_ERR_* code on failure.
 * On failure *loader is zeroed and no memory is leaked.
 */
int ne_load_buffer(const uint8_t *buf, size_t len,
                   const NEParserContext *parser,
                   NELoaderContext *loader);

/*
 * ne_load_file - open 'path', parse the NE header, then load all segments.
 *
 * Combines ne_parse_file() and ne_load_buffer() into a single call.
 * Returns NE_LOAD_OK on success; caller must call ne_loader_free().
 */
int ne_load_file(const char *path,
                 const NEParserContext *parser,
                 NELoaderContext *loader);

/*
 * ne_loader_free - release all heap memory owned by *loader.
 * Safe to call on a partially-initialised or zeroed context, and on NULL.
 */
void ne_loader_free(NELoaderContext *loader);

/*
 * ne_loader_print_info - write a human-readable segment placement summary
 * to 'out'.  'parser' may be NULL (entry-point line is then omitted).
 */
void ne_loader_print_info(const NELoaderContext *loader,
                          const NEParserContext *parser,
                          FILE *out);

/*
 * ne_loader_strerror - return a static string describing error code 'err'.
 */
const char *ne_loader_strerror(int err);

#endif /* NE_LOADER_H */

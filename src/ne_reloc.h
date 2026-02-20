/*
 * ne_reloc.h - NE (New Executable) relocation management
 *
 * Parses and applies the per-segment relocation records embedded in an NE
 * file, handling internal (intra-module), imported-reference, and OS-fixup
 * record types as required by the Windows 3.1 runtime.
 *
 * This module implements Step 3 of the WinDOS kernel-replacement roadmap.
 * The host-side implementation uses standard C library memory functions;
 * on a real 16-bit DOS target these would be replaced by the appropriate
 * Watcom memory intrinsics or DOS INT 21h service calls.
 *
 * Reference: Microsoft "New Executable" format specification.
 */

#ifndef NE_RELOC_H
#define NE_RELOC_H

#include "ne_parser.h"
#include "ne_loader.h"

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_RELOC_OK              0
#define NE_RELOC_ERR_NULL       -1   /* NULL pointer argument               */
#define NE_RELOC_ERR_ALLOC      -2   /* memory allocation failure           */
#define NE_RELOC_ERR_IO         -3   /* data reads outside file bounds       */
#define NE_RELOC_ERR_BAD_SEG    -4   /* segment index out of range          */
#define NE_RELOC_ERR_UNRESOLVED -5   /* imported symbol unresolvable        */
#define NE_RELOC_ERR_ADDR_TYPE  -6   /* unsupported address type            */

/* -------------------------------------------------------------------------
 * Address type codes  (NERelocRecord.address_type)
 * ---------------------------------------------------------------------- */
#define NE_RELOC_ADDR_LOBYTE   0   /* patch 1 byte  – low byte of offset   */
#define NE_RELOC_ADDR_SEG16    2   /* patch 2 bytes – 16-bit segment sel.  */
#define NE_RELOC_ADDR_FAR32    3   /* patch 4 bytes – far ptr off16:seg16  */
#define NE_RELOC_ADDR_OFF16    5   /* patch 2 bytes – 16-bit offset        */
#define NE_RELOC_ADDR_SEL16   11   /* patch 2 bytes – selector alias       */
#define NE_RELOC_ADDR_PTR32   13   /* patch 4 bytes – 32-bit flat offset   */

/* -------------------------------------------------------------------------
 * Relocation type codes  (lower 2 bits of NERelocRecord.reloc_type)
 * ---------------------------------------------------------------------- */
#define NE_RELOC_TYPE_INTERNAL  0   /* intra-module segment/offset fixup   */
#define NE_RELOC_TYPE_IMP_ORD   1   /* imported reference – by ordinal     */
#define NE_RELOC_TYPE_IMP_NAME  2   /* imported reference – by name        */
#define NE_RELOC_TYPE_OS_FIXUP  3   /* OS-specific fixup (skip)            */

/* Modifier flag in the reloc_type byte */
#define NE_RELOC_FLAG_ADDITIVE  0x04 /* add to existing value (not chain)  */

/* -------------------------------------------------------------------------
 * Raw relocation record (8 bytes, as stored in the NE file)
 *
 * Layout in the file (all offsets within the 8-byte record):
 *   [0]   address_type  – NE_RELOC_ADDR_* constant
 *   [1]   reloc_type    – NE_RELOC_TYPE_* | NE_RELOC_FLAG_* bits
 *   [2-3] target_offset – first byte offset in the segment to patch;
 *                         if NE_RELOC_FLAG_ADDITIVE is NOT set this is the
 *                         head of a linked-list chain (each word at the
 *                         patched location contains the next offset; the
 *                         list ends at 0xFFFF / 0xFF for LOBYTE)
 *   [4-5] ref1          – INTERNAL: 1-based target segment number
 *                         IMP_ORD / IMP_NAME: 1-based module-ref index
 *                         OS_FIXUP: OS fixup type code
 *   [6-7] ref2          – INTERNAL: byte offset within the target segment
 *                         IMP_ORD: ordinal number
 *                         IMP_NAME: byte offset in the imported-names table
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t  address_type;   /* NE_RELOC_ADDR_* constant                   */
    uint8_t  reloc_type;     /* NE_RELOC_TYPE_* | NE_RELOC_FLAG_*          */
    uint16_t target_offset;  /* offset in segment; chain head if not ADDITIVE */
    uint16_t ref1;           /* meaning depends on reloc_type (see above)  */
    uint16_t ref2;           /* meaning depends on reloc_type (see above)  */
} NERelocRecord;

/* -------------------------------------------------------------------------
 * Per-segment relocation table
 * ---------------------------------------------------------------------- */
typedef struct {
    NERelocRecord *records;  /* heap-allocated array of parsed records      */
    uint16_t       count;    /* number of records in this table             */
    uint16_t       seg_idx;  /* 0-based index of the owning segment         */
} NESegRelocTable;

/* -------------------------------------------------------------------------
 * Module-level relocation context
 * ---------------------------------------------------------------------- */
typedef struct {
    NESegRelocTable *tables; /* heap-allocated; one entry per segment with
                                NE_SEG_RELOC set and non-zero file data     */
    uint16_t         count;  /* number of filled entries in tables[]        */
} NERelocContext;

/* -------------------------------------------------------------------------
 * Import resolver callback
 *
 * Invoked by ne_reloc_apply() for every IMPORTED_ORDINAL or IMPORTED_NAME
 * record.  The implementation must locate the symbol in the referenced
 * module and return the target segment index (0-based) and offset.
 *
 * Parameters:
 *   mod_idx        – 1-based index into the NE module-reference table
 *   ref2           – ordinal number (IMP_ORD) or offset in imported-names
 *                    table (IMP_NAME)
 *   by_name        – non-zero when this is an IMP_NAME lookup
 *   imported_names – pointer to the imported-names table bytes (may be NULL)
 *   imp_names_size – byte length of the imported-names table
 *   out_seg        – receives the resolved 0-based segment index
 *   out_offset     – receives the resolved byte offset within that segment
 *   userdata       – caller-supplied opaque pointer
 *
 * Return NE_RELOC_OK on success, NE_RELOC_ERR_UNRESOLVED on failure.
 * ---------------------------------------------------------------------- */
typedef int (*NEImportResolver)(uint16_t       mod_idx,
                                uint16_t       ref2,
                                int            by_name,
                                const uint8_t *imported_names,
                                uint16_t       imp_names_size,
                                uint16_t      *out_seg,
                                uint16_t      *out_offset,
                                void          *userdata);

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/*
 * ne_reloc_parse - parse relocation records from an in-memory NE file image.
 *
 * For every segment in 'parser' that has the NE_SEG_RELOC flag set and a
 * non-zero file offset, the relocation block that follows the segment data
 * in the file is parsed and stored in an NESegRelocTable inside *ctx.
 *
 * 'buf' / 'len' must be the same complete file image used for
 * ne_parse_buffer() / ne_load_buffer().
 *
 * Returns NE_RELOC_OK on success; *ctx is valid and the caller must call
 * ne_reloc_free() when done.  On failure *ctx is zeroed and no memory is
 * leaked.
 */
int ne_reloc_parse(const uint8_t      *buf,
                   size_t              len,
                   const NEParserContext *parser,
                   NERelocContext      *ctx);

/*
 * ne_reloc_apply - apply all parsed relocations to the loaded segment images.
 *
 * For each relocation record:
 *   INTERNAL     – patch with the segment index (and optional offset) of the
 *                  designated intra-module segment.
 *   IMP_ORD/NAME – invoke 'resolver' to obtain the target; fail with
 *                  NE_RELOC_ERR_UNRESOLVED if resolver is NULL or returns
 *                  an error.
 *   OS_FIXUP     – silently skipped.
 *
 * Non-ADDITIVE records follow the embedded linked-list chain; ADDITIVE
 * records add the computed value to whatever is already at the target.
 *
 * Returns NE_RELOC_OK on success or a negative NE_RELOC_ERR_* code.
 */
int ne_reloc_apply(NELoaderContext        *loader,
                   const NERelocContext   *reloc_ctx,
                   const NEParserContext  *parser,
                   NEImportResolver        resolver,
                   void                   *resolver_data);

/*
 * ne_reloc_free - release all heap memory owned by *ctx.
 * Safe to call on a zeroed context or NULL.
 */
void ne_reloc_free(NERelocContext *ctx);

/*
 * ne_reloc_strerror - return a static string describing error code 'err'.
 */
const char *ne_reloc_strerror(int err);

#endif /* NE_RELOC_H */

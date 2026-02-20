/*
 * ne_parser.h - NE (New Executable) file format parser
 *
 * Parses the 16-bit Windows / OS/2 NE executable format as used by
 * Windows 3.1 (kernel, user, GDI and application DLLs).
 *
 * Reference: Microsoft "New Executable" format specification.
 */

#ifndef NE_PARSER_H
#define NE_PARSER_H

#include <stdint.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Magic values
 * ---------------------------------------------------------------------- */
#define MZ_MAGIC  0x5A4D   /* 'MZ' - DOS executable stub header */
#define NE_MAGIC  0x454E   /* 'NE' - New Executable header      */

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_OK              0
#define NE_ERR_IO         -1   /* file read / seek failure          */
#define NE_ERR_NOT_MZ     -2   /* MZ magic bytes not found          */
#define NE_ERR_NOT_NE     -3   /* NE magic bytes not found          */
#define NE_ERR_BAD_OFFSET -4   /* table offset out of range         */
#define NE_ERR_ALLOC      -5   /* memory allocation failure         */
#define NE_ERR_BAD_HEADER -6   /* other header field validation err */
#define NE_ERR_NULL_ARG   -7   /* NULL pointer argument             */

/* -------------------------------------------------------------------------
 * Target OS codes stored in NEHeader.target_os
 * ---------------------------------------------------------------------- */
#define NE_OS_UNKNOWN   0
#define NE_OS_OS2       1
#define NE_OS_WINDOWS   2
#define NE_OS_DOS4      3
#define NE_OS_WIN386    4

/* -------------------------------------------------------------------------
 * Segment flags (NESegmentDescriptor.flags)
 * ---------------------------------------------------------------------- */
#define NE_SEG_DATA        0x0001  /* data segment (else code)          */
#define NE_SEG_ALLOC       0x0002  /* allocated                         */
#define NE_SEG_LOADED      0x0004  /* loaded                            */
#define NE_SEG_MOVABLE     0x0010  /* movable                           */
#define NE_SEG_SHARED      0x0020  /* shared                            */
#define NE_SEG_PRELOAD     0x0040  /* preload                           */
#define NE_SEG_ERONLY      0x0080  /* execute-only (code) / read-only (data) */
#define NE_SEG_RELOC       0x0100  /* has relocation records            */
#define NE_SEG_DISCARDABLE 0x1000  /* discardable                       */

/* -------------------------------------------------------------------------
 * Program flags (NEHeader.program_flags)
 * ---------------------------------------------------------------------- */
#define NE_PFLAG_SINGLEDATA  0x01  /* single shared data segment        */
#define NE_PFLAG_MULTIDATA   0x02  /* multiple data instances           */
#define NE_PFLAG_PROTECTED   0x08  /* runs in protected mode            */

/* -------------------------------------------------------------------------
 * Application flags (NEHeader.app_flags)
 * ---------------------------------------------------------------------- */
#define NE_AFLAG_FULLSCREEN  0x01  /* full-screen (text) app            */
#define NE_AFLAG_WINCOMPAT   0x02  /* compatible with Windows PM        */
#define NE_AFLAG_WINAPI      0x03  /* uses Windows API                  */
#define NE_AFLAG_NOTWINCOMPAT 0x04 /* not compatible with Windows       */
#define NE_AFLAG_DLL         0x80  /* DLL or driver (no automatic data) */

/* -------------------------------------------------------------------------
 * Packed structure layout helpers
 * ---------------------------------------------------------------------- */
#pragma pack(push, 1)

/*
 * DOS MZ executable header.
 * The NE header offset is stored at byte offset 0x3C (field ne_offset).
 */
typedef struct {
    uint16_t magic;                  /* 0x00: 'MZ'                        */
    uint16_t bytes_in_last_block;    /* 0x02: bytes in last 512-byte block */
    uint16_t blocks_in_file;         /* 0x04: number of 512-byte blocks    */
    uint16_t num_relocs;             /* 0x06: number of relocation entries  */
    uint16_t header_paragraphs;      /* 0x08: header size in paragraphs    */
    uint16_t min_extra_paragraphs;   /* 0x0A: min extra paragraphs         */
    uint16_t max_extra_paragraphs;   /* 0x0C: max extra paragraphs         */
    uint16_t initial_ss;             /* 0x0E: initial SS value             */
    uint16_t initial_sp;             /* 0x10: initial SP value             */
    uint16_t checksum;               /* 0x12: checksum                     */
    uint16_t initial_ip;             /* 0x14: initial IP value             */
    uint16_t initial_cs;             /* 0x16: initial CS value             */
    uint16_t reloc_table_offset;     /* 0x18: relocation table offset      */
    uint16_t overlay_number;         /* 0x1A: overlay number               */
    uint16_t reserved[4];            /* 0x1C: reserved                     */
    uint16_t oem_id;                 /* 0x24: OEM identifier               */
    uint16_t oem_info;               /* 0x26: OEM information              */
    uint16_t reserved2[10];          /* 0x28: reserved                     */
    uint32_t ne_offset;              /* 0x3C: file offset of NE header     */
} MZHeader;

/*
 * NE executable header (starts with 'NE' magic at the ne_offset position).
 * All table offset fields are relative to the start of this header.
 */
typedef struct {
    uint16_t magic;                      /* 0x00: 'NE'                           */
    uint8_t  linker_major;               /* 0x02: linker major version            */
    uint8_t  linker_minor;               /* 0x03: linker minor version            */
    uint16_t entry_table_offset;         /* 0x04: entry table offset (rel)        */
    uint16_t entry_table_length;         /* 0x06: entry table length in bytes     */
    uint32_t crc32;                      /* 0x08: file CRC (unused, may be 0)     */
    uint8_t  program_flags;              /* 0x0C: program flags                   */
    uint8_t  app_flags;                  /* 0x0D: application flags               */
    uint16_t auto_data_seg;              /* 0x0E: automatic data segment index    */
    uint16_t heap_size;                  /* 0x10: initial local heap size         */
    uint16_t stack_size;                 /* 0x12: initial stack size              */
    uint16_t initial_ip;                 /* 0x14: entry-point IP                  */
    uint16_t initial_cs;                 /* 0x16: entry-point CS segment index    */
    uint16_t initial_sp;                 /* 0x18: initial SP                      */
    uint16_t initial_ss;                 /* 0x1A: initial SS segment index        */
    uint16_t segment_count;              /* 0x1C: number of segment descriptors   */
    uint16_t module_ref_count;           /* 0x1E: entries in module-ref table     */
    uint16_t nonresident_name_size;      /* 0x20: non-resident name table size    */
    uint16_t segment_table_offset;       /* 0x22: segment table offset (rel)      */
    uint16_t resource_table_offset;      /* 0x24: resource table offset (rel)     */
    uint16_t resident_name_table_offset; /* 0x26: resident name table offset(rel) */
    uint16_t module_ref_table_offset;    /* 0x28: module-ref table offset (rel)   */
    uint16_t imported_names_offset;      /* 0x2A: imported names table offset(rel)*/
    uint32_t nonresident_name_offset;    /* 0x2C: non-resident name table (abs)   */
    uint16_t movable_entry_count;        /* 0x30: number of movable entry points  */
    uint16_t align_shift;                /* 0x32: logical sector alignment shift  */
    uint16_t resource_seg_count;         /* 0x34: resource segments count         */
    uint8_t  target_os;                  /* 0x36: target OS (NE_OS_*)             */
    uint8_t  os2_flags;                  /* 0x37: OS/2 EXE flags                  */
    uint16_t return_thunks_offset;       /* 0x38: return thunks offset (rel)      */
    uint16_t seg_ref_thunks_offset;      /* 0x3A: segment-reference thunks offset */
    uint16_t min_code_swap_size;         /* 0x3C: minimum code swap area size     */
    uint8_t  expected_win_ver_minor;     /* 0x3E: expected Windows minor version  */
    uint8_t  expected_win_ver_major;     /* 0x3F: expected Windows major version  */
} NEHeader;

/*
 * One entry in the NE segment table (8 bytes each).
 * Segment data is located at: file_offset = offset << align_shift
 * A value of 0 for offset means the segment has no data in the file.
 * A value of 0 for length or min_alloc means 64 KB.
 */
typedef struct {
    uint16_t offset;    /* logical sector offset in file (0 = no file data) */
    uint16_t length;    /* segment length in bytes (0 = 64 KB)              */
    uint16_t flags;     /* NE_SEG_* flags                                   */
    uint16_t min_alloc; /* minimum allocation in bytes (0 = 64 KB)          */
} NESegmentDescriptor;

#pragma pack(pop)

/* -------------------------------------------------------------------------
 * Parser context – filled in by ne_parse_file() / ne_parse_buffer()
 * ---------------------------------------------------------------------- */
typedef struct {
    NEHeader             header;          /* parsed NE header                  */
    uint32_t             ne_offset;       /* absolute file offset of NE header */
    NESegmentDescriptor *segments;        /* heap-allocated segment array      */

    /* raw byte blobs for the variable-length tables */
    uint8_t             *resource_data;   /* resource table bytes              */
    uint16_t             resource_size;   /* size of resource_data in bytes    */
    uint8_t             *entry_data;      /* entry table bytes                 */
    uint16_t             entry_size;      /* size of entry_data in bytes       */
    uint8_t             *imported_names;  /* imported-names table bytes        */
    uint16_t             imported_names_size;
} NEParserContext;

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/*
 * ne_parse_file - open 'path', validate MZ+NE headers, and populate *ctx.
 *
 * Returns NE_OK on success or a negative NE_ERR_* code on failure.
 * On success the caller must call ne_free() when done with *ctx.
 */
int ne_parse_file(const char *path, NEParserContext *ctx);

/*
 * ne_parse_buffer - parse an NE file image already loaded in memory.
 *
 * 'buf' must point to at least 'len' bytes of the complete file image.
 * Returns NE_OK on success; caller must call ne_free() when done.
 */
int ne_parse_buffer(const uint8_t *buf, size_t len, NEParserContext *ctx);

/*
 * ne_free - release all heap memory owned by *ctx.
 * Safe to call even if ne_parse_* returned an error.
 */
void ne_free(NEParserContext *ctx);

/*
 * ne_print_info - print a human-readable summary of *ctx to 'out'.
 * 'out' is typically stdout or stderr.
 */
void ne_print_info(const NEParserContext *ctx, FILE *out);

/*
 * ne_strerror - return a static string describing error code 'err'.
 */
const char *ne_strerror(int err);

#endif /* NE_PARSER_H */

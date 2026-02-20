/*
 * test_ne_impexp.c - Tests for Step 5: import/export resolution
 *
 * Verifies:
 *   - ne_export_build: parsing the NE entry table and resident name table
 *   - ne_export_find_by_ordinal / ne_export_find_by_name
 *   - ne_import_resolve_ordinal / ne_import_resolve_name
 *   - Stub table: register, find, replace, capacity, and deduplication
 *
 * Build with:
 *   wcc -ml -za99 -wx -d2 -i=../src ../src/ne_parser.c ../src/ne_impexp.c
 *       test_ne_impexp.c
 *   wlink system dos name test_ne_impexp.exe file
 *       test_ne_impexp.obj,ne_parser.obj,ne_impexp.obj
 */

#include "../src/ne_parser.h"
#include "../src/ne_impexp.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Minimal test framework (mirrors test_ne_module.c)
 * ---------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_BEGIN(name) \
    do { \
        g_tests_run++; \
        printf("  %-60s ", (name)); \
        fflush(stdout); \
    } while (0)

#define TEST_PASS() \
    do { \
        g_tests_passed++; \
        printf("PASS\n"); \
    } while (0)

#define TEST_FAIL(msg) \
    do { \
        g_tests_failed++; \
        printf("FAIL - %s (line %d)\n", (msg), __LINE__); \
        return; \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            g_tests_failed++; \
            printf("FAIL - expected %ld got %ld (line %d)\n", \
                   (long)(b), (long)(a), __LINE__); \
            return; \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            g_tests_failed++; \
            printf("FAIL - unexpected equal value %ld (line %d)\n", \
                   (long)(a), __LINE__); \
            return; \
        } \
    } while (0)

#define ASSERT_NOT_NULL(p) \
    do { \
        if ((p) == NULL) { \
            g_tests_failed++; \
            printf("FAIL - unexpected NULL pointer (line %d)\n", __LINE__); \
            return; \
        } \
    } while (0)

#define ASSERT_NULL(p) \
    do { \
        if ((p) != NULL) { \
            g_tests_failed++; \
            printf("FAIL - expected NULL pointer (line %d)\n", __LINE__); \
            return; \
        } \
    } while (0)

#define ASSERT_STR_EQ(a, b) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            g_tests_failed++; \
            printf("FAIL - expected \"%s\" got \"%s\" (line %d)\n", \
                   (b), (a), __LINE__); \
            return; \
        } \
    } while (0)

/* -------------------------------------------------------------------------
 * NE image builder
 *
 * Layout (all offsets are absolute within the image buffer):
 *
 *   0x000 - 0x03F : MZ header (64 bytes)
 *   0x040 - 0x07F : NE header (64 bytes)
 *   0x080 - 0x087 : Segment descriptor 0 (CODE, 8 bytes, no file data)
 *
 * NE-relative table offsets:
 *   segment_table_offset          = 0x40  → abs 0x080
 *   resource_table_offset         = 0x48  → abs 0x088 (empty)
 *   resident_name_table_offset    = 0x48  → abs 0x088
 *   module_ref_table_offset       = varies (after resident name table)
 *   imported_names_offset         = same as module_ref (no imports)
 *   entry_table_offset            = same as above (no imported names)
 *   entry_table_length            = varies
 *
 * The helper build_image() writes both the resident name table and the
 * entry table into a caller-supplied buffer and fills in the NE header
 * fields accordingly.
 * ---------------------------------------------------------------------- */

#define MZ_SIZE         64u
#define NE_HDR_SIZE     64u
#define SEG_DESC_SIZE    8u
#define FIXED_HDR_SIZE  (MZ_SIZE + NE_HDR_SIZE + SEG_DESC_SIZE) /* 0x88 */

/* NE header field byte offsets relative to start of NE header */
#define NE_OFF_MAGIC        0x00u
#define NE_OFF_ENTRY_OFF    0x04u
#define NE_OFF_ENTRY_LEN    0x06u
#define NE_OFF_SEG_COUNT    0x1Cu
#define NE_OFF_MODREF_COUNT 0x1Eu
#define NE_OFF_SEG_TBL      0x22u
#define NE_OFF_RES_TBL      0x24u
#define NE_OFF_RNT          0x26u
#define NE_OFF_MODREF_TBL   0x28u
#define NE_OFF_IMP_NAMES    0x2Au
#define NE_OFF_ALIGN_SHIFT  0x32u
#define NE_OFF_TARGET_OS    0x36u

static void write_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void write_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v        & 0xFFu);
    p[1] = (uint8_t)((v >>  8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

/*
 * build_image
 *
 * Builds a minimal NE image into 'out' (which must be at least
 * FIXED_HDR_SIZE + rnt_size + entry_size bytes).
 *
 * rnt  / rnt_size  : raw resident name table bytes (may be NULL/0)
 * etbl / etbl_size : raw entry table bytes (may be NULL/0)
 *
 * Returns the total image size written.
 */
static size_t build_image(uint8_t       *out,
                           const uint8_t *rnt,   uint16_t rnt_size,
                           const uint8_t *etbl,  uint16_t etbl_size)
{
    uint8_t  *ne;        /* pointer to NE header inside out[] */
    uint16_t  rnt_off;   /* NE-relative offset of resident name table */
    uint16_t  ref_off;   /* NE-relative offset of module-ref table    */
    uint16_t  ent_off;   /* NE-relative offset of entry table         */
    size_t    total;

    memset(out, 0, FIXED_HDR_SIZE);

    /* MZ header */
    out[0] = 0x4Du; out[1] = 0x5Au; /* 'MZ' magic */
    write_u32le(out + 0x3C, (uint32_t)MZ_SIZE); /* ne_offset = 0x40 */

    ne = out + MZ_SIZE;

    /* NE header */
    ne[NE_OFF_MAGIC]        = 0x4Eu;  /* 'N' */
    ne[NE_OFF_MAGIC + 1u]   = 0x45u;  /* 'E' */
    ne[NE_OFF_TARGET_OS]    = 0x02u;  /* Windows */
    write_u16le(ne + NE_OFF_ALIGN_SHIFT, 4u); /* 16-byte sectors */
    write_u16le(ne + NE_OFF_SEG_COUNT,   1u); /* one segment */
    write_u16le(ne + NE_OFF_MODREF_COUNT, 0u);

    /* All table offsets are relative to the NE header start */
    write_u16le(ne + NE_OFF_SEG_TBL, (uint16_t)NE_HDR_SIZE);
    /* resource table = immediately after segment table (empty) */
    write_u16le(ne + NE_OFF_RES_TBL,
                (uint16_t)(NE_HDR_SIZE + SEG_DESC_SIZE));
    /* resident name table = same position (no resources) */
    rnt_off = (uint16_t)(NE_HDR_SIZE + SEG_DESC_SIZE);
    write_u16le(ne + NE_OFF_RNT, rnt_off);

    /* module-ref table immediately follows resident name table */
    ref_off = (uint16_t)(rnt_off + rnt_size);
    write_u16le(ne + NE_OFF_MODREF_TBL, ref_off);
    /* imported names immediately follow module-ref table (empty) */
    write_u16le(ne + NE_OFF_IMP_NAMES, ref_off);
    /* entry table immediately follows (module-ref is 0 bytes, imp-names 0) */
    ent_off = ref_off;
    write_u16le(ne + NE_OFF_ENTRY_OFF, ent_off);
    write_u16le(ne + NE_OFF_ENTRY_LEN, etbl_size);

    /* Segment descriptor 0: no file data, 4 KB, CODE */
    {
        uint8_t *seg = out + MZ_SIZE + NE_HDR_SIZE;
        write_u16le(seg + 0u, 0u);       /* file offset = 0 (no data)  */
        write_u16le(seg + 2u, 0x1000u);  /* length = 4 KB              */
        write_u16le(seg + 4u, 0u);       /* flags = CODE               */
        write_u16le(seg + 6u, 0x1000u);  /* min_alloc = 4 KB           */
    }

    total = FIXED_HDR_SIZE;

    /* Resident name table */
    if (rnt && rnt_size > 0u) {
        memcpy(out + total, rnt, rnt_size);
        total += rnt_size;
    }

    /* Entry table */
    if (etbl && etbl_size > 0u) {
        memcpy(out + total, etbl, etbl_size);
        total += etbl_size;
    }

    return total;
}

/* =========================================================================
 * Test cases – export table building
 * ===================================================================== */

/* -------------------------------------------------------------------------
 * Empty entry table → no exports
 * ---------------------------------------------------------------------- */
static void test_export_build_empty(void)
{
    uint8_t        imgbuf[256];
    NEParserContext parser;
    NEExportTable   tbl;
    size_t          sz;
    int             rc;

    /* null byte = end-of-entry-table */
    static const uint8_t etbl[] = { 0x00 };

    TEST_BEGIN("export build: empty entry table produces zero exports");

    sz = build_image(imgbuf, NULL, 0u, etbl, (uint16_t)sizeof(etbl));

    rc = ne_parse_buffer(imgbuf, sz, &parser);
    ASSERT_EQ(rc, NE_OK);

    rc = ne_export_build(imgbuf, sz, &parser, &tbl);
    ASSERT_EQ(rc, NE_IMPEXP_OK);
    ASSERT_EQ(tbl.count, (uint16_t)0);

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * Two fixed-segment entries (no resident name table)
 * ---------------------------------------------------------------------- */
static void test_export_build_fixed_entries(void)
{
    uint8_t         imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;
    size_t           sz;
    int              rc;
    const NEExportEntry *e;

    /*
     * Entry table:
     *   02 01         count=2, type=1 (segment 1 → 0-based index 0)
     *   00 00 01      entry 1: flags=0, offset=0x0100
     *   00 00 02      entry 2: flags=0, offset=0x0200
     *   00            end
     */
    static const uint8_t etbl[] = {
        0x02u, 0x01u,
        0x00u, 0x00u, 0x01u,
        0x00u, 0x00u, 0x02u,
        0x00u
    };

    TEST_BEGIN("export build: two fixed entries have correct ordinals/addresses");

    sz = build_image(imgbuf, NULL, 0u, etbl, (uint16_t)sizeof(etbl));

    rc = ne_parse_buffer(imgbuf, sz, &parser);
    ASSERT_EQ(rc, NE_OK);

    rc = ne_export_build(imgbuf, sz, &parser, &tbl);
    ASSERT_EQ(rc, NE_IMPEXP_OK);
    ASSERT_EQ(tbl.count, (uint16_t)2);

    e = ne_export_find_by_ordinal(&tbl, 1u);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->segment, (uint16_t)0);
    ASSERT_EQ(e->offset,  (uint16_t)0x0100u);

    e = ne_export_find_by_ordinal(&tbl, 2u);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->segment, (uint16_t)0);
    ASSERT_EQ(e->offset,  (uint16_t)0x0200u);

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * Null bundle in entry table – ordinal numbers are consumed but no entry
 * is added to the export table
 * ---------------------------------------------------------------------- */
static void test_export_build_null_bundle(void)
{
    uint8_t         imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;
    size_t           sz;
    int              rc;
    const NEExportEntry *e;

    /*
     * Entry table:
     *   02 00         null bundle: 2 null ordinals (1, 2 consumed)
     *   01 01         count=1, type=1
     *   00 00 05      entry: flags=0, offset=0x0500  → ordinal 3
     *   00            end
     */
    static const uint8_t etbl[] = {
        0x02u, 0x00u,
        0x01u, 0x01u,
        0x00u, 0x00u, 0x05u,
        0x00u
    };

    TEST_BEGIN("export build: null bundle skips ordinals correctly");

    sz = build_image(imgbuf, NULL, 0u, etbl, (uint16_t)sizeof(etbl));

    rc = ne_parse_buffer(imgbuf, sz, &parser);
    ASSERT_EQ(rc, NE_OK);

    rc = ne_export_build(imgbuf, sz, &parser, &tbl);
    ASSERT_EQ(rc, NE_IMPEXP_OK);
    ASSERT_EQ(tbl.count, (uint16_t)1);

    /* Ordinals 1 and 2 were null; ordinal 3 is the first real entry */
    ASSERT_NULL(ne_export_find_by_ordinal(&tbl, 1u));
    ASSERT_NULL(ne_export_find_by_ordinal(&tbl, 2u));

    e = ne_export_find_by_ordinal(&tbl, 3u);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->offset, (uint16_t)0x0500u);

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * Movable-segment entry
 * ---------------------------------------------------------------------- */
static void test_export_build_movable_entry(void)
{
    uint8_t         imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;
    size_t           sz;
    int              rc;
    const NEExportEntry *e;

    /*
     * Entry table:
     *   01 FF         count=1, type=0xFF (movable)
     *   00 CD 3F 01 00 03   flags=0, 0xCD, 0x3F, seg=1→0-based 0, off=0x0300
     *   00            end
     */
    static const uint8_t etbl[] = {
        0x01u, 0xFFu,
        0x00u, 0xCDu, 0x3Fu, 0x01u, 0x00u, 0x03u,
        0x00u
    };

    TEST_BEGIN("export build: movable entry produces correct segment/offset");

    sz = build_image(imgbuf, NULL, 0u, etbl, (uint16_t)sizeof(etbl));

    rc = ne_parse_buffer(imgbuf, sz, &parser);
    ASSERT_EQ(rc, NE_OK);

    rc = ne_export_build(imgbuf, sz, &parser, &tbl);
    ASSERT_EQ(rc, NE_IMPEXP_OK);
    ASSERT_EQ(tbl.count, (uint16_t)1);

    e = ne_export_find_by_ordinal(&tbl, 1u);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->segment, (uint16_t)0);   /* 1-based seg 1 → 0-based 0 */
    ASSERT_EQ(e->offset,  (uint16_t)0x0300u);

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * Export names from resident name table
 * ---------------------------------------------------------------------- */
static void test_export_build_names(void)
{
    uint8_t         imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;
    size_t           sz;
    int              rc;
    const NEExportEntry *e;

    /*
     * Resident name table (26 bytes):
     *   07 TESTMOD 00 00   module name (ordinal 0) – 10 bytes
     *   05 FuncA   01 00   export "FuncA", ordinal 1 – 8 bytes
     *   05 FuncB   02 00   export "FuncB", ordinal 2 – 8 bytes
     *   00                 end – 1 byte   Total = 27 bytes
     */
    static const uint8_t rnt[] = {
        0x07u, 'T','E','S','T','M','O','D', 0x00u, 0x00u,
        0x05u, 'F','u','n','c','A',         0x01u, 0x00u,
        0x05u, 'F','u','n','c','B',         0x02u, 0x00u,
        0x00u
    };
    static const uint8_t etbl[] = {
        0x02u, 0x01u,
        0x00u, 0x00u, 0x01u,   /* ordinal 1: offset 0x0100 */
        0x00u, 0x00u, 0x02u,   /* ordinal 2: offset 0x0200 */
        0x00u
    };

    TEST_BEGIN("export build: names attached from resident name table");

    sz = build_image(imgbuf,
                     rnt,  (uint16_t)sizeof(rnt),
                     etbl, (uint16_t)sizeof(etbl));

    rc = ne_parse_buffer(imgbuf, sz, &parser);
    ASSERT_EQ(rc, NE_OK);

    rc = ne_export_build(imgbuf, sz, &parser, &tbl);
    ASSERT_EQ(rc, NE_IMPEXP_OK);
    ASSERT_EQ(tbl.count, (uint16_t)2);

    e = ne_export_find_by_ordinal(&tbl, 1u);
    ASSERT_NOT_NULL(e);
    ASSERT_STR_EQ(e->name, "FuncA");
    ASSERT_EQ(e->offset, (uint16_t)0x0100u);

    e = ne_export_find_by_ordinal(&tbl, 2u);
    ASSERT_NOT_NULL(e);
    ASSERT_STR_EQ(e->name, "FuncB");
    ASSERT_EQ(e->offset, (uint16_t)0x0200u);

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

/* =========================================================================
 * Test cases – ne_export_find_by_ordinal / ne_export_find_by_name
 * ===================================================================== */

/*
 * Helper: builds and returns a populated export table with FuncA (ord 1)
 * and FuncB (ord 2).  Parser ctx is returned via *out_parser; caller must
 * call ne_export_free() and ne_free() when done.
 */
static int make_two_export_table(uint8_t         *imgbuf, /* at least 512 */
                                  size_t           imgmax,
                                  NEParserContext  *out_parser,
                                  NEExportTable    *out_tbl)
{
    size_t sz;
    int    rc;

    static const uint8_t rnt[] = {
        0x07u, 'T','E','S','T','M','O','D', 0x00u, 0x00u,
        0x05u, 'F','u','n','c','A',         0x01u, 0x00u,
        0x05u, 'F','u','n','c','B',         0x02u, 0x00u,
        0x00u
    };
    static const uint8_t etbl[] = {
        0x02u, 0x01u,
        0x00u, 0x00u, 0x01u,
        0x00u, 0x00u, 0x02u,
        0x00u
    };

    (void)imgmax;
    sz = build_image(imgbuf,
                     rnt,  (uint16_t)sizeof(rnt),
                     etbl, (uint16_t)sizeof(etbl));

    rc = ne_parse_buffer(imgbuf, sz, out_parser);
    if (rc != NE_OK) return rc;

    return ne_export_build(imgbuf, sz, out_parser, out_tbl);
}

static void test_find_by_ordinal_hit(void)
{
    uint8_t          imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;
    const NEExportEntry *e;

    TEST_BEGIN("find_by_ordinal: hit returns correct entry");

    ASSERT_EQ(make_two_export_table(imgbuf, sizeof(imgbuf), &parser, &tbl),
              NE_IMPEXP_OK);

    e = ne_export_find_by_ordinal(&tbl, 1u);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->ordinal, (uint16_t)1u);
    ASSERT_STR_EQ(e->name, "FuncA");

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

static void test_find_by_ordinal_miss(void)
{
    uint8_t          imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;

    TEST_BEGIN("find_by_ordinal: miss returns NULL");

    ASSERT_EQ(make_two_export_table(imgbuf, sizeof(imgbuf), &parser, &tbl),
              NE_IMPEXP_OK);

    ASSERT_NULL(ne_export_find_by_ordinal(&tbl, 99u));

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

static void test_find_by_ordinal_null_table(void)
{
    TEST_BEGIN("find_by_ordinal: NULL table returns NULL");
    ASSERT_NULL(ne_export_find_by_ordinal(NULL, 1u));
    TEST_PASS();
}

static void test_find_by_name_hit(void)
{
    uint8_t          imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;
    const NEExportEntry *e;

    TEST_BEGIN("find_by_name: hit returns correct entry");

    ASSERT_EQ(make_two_export_table(imgbuf, sizeof(imgbuf), &parser, &tbl),
              NE_IMPEXP_OK);

    e = ne_export_find_by_name(&tbl, "FuncB");
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->ordinal, (uint16_t)2u);
    ASSERT_EQ(e->offset,  (uint16_t)0x0200u);

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

static void test_find_by_name_miss(void)
{
    uint8_t          imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;

    TEST_BEGIN("find_by_name: miss returns NULL");

    ASSERT_EQ(make_two_export_table(imgbuf, sizeof(imgbuf), &parser, &tbl),
              NE_IMPEXP_OK);

    ASSERT_NULL(ne_export_find_by_name(&tbl, "DoesNotExist"));

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

static void test_find_by_name_case_sensitive(void)
{
    uint8_t          imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;

    TEST_BEGIN("find_by_name: lookup is case-sensitive");

    ASSERT_EQ(make_two_export_table(imgbuf, sizeof(imgbuf), &parser, &tbl),
              NE_IMPEXP_OK);

    /* "funca" != "FuncA" */
    ASSERT_NULL(ne_export_find_by_name(&tbl, "funca"));

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

static void test_find_by_name_null_args(void)
{
    uint8_t          imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;

    TEST_BEGIN("find_by_name: NULL table or empty name returns NULL");

    ASSERT_EQ(make_two_export_table(imgbuf, sizeof(imgbuf), &parser, &tbl),
              NE_IMPEXP_OK);

    ASSERT_NULL(ne_export_find_by_name(NULL, "FuncA"));
    ASSERT_NULL(ne_export_find_by_name(&tbl,  NULL));
    ASSERT_NULL(ne_export_find_by_name(&tbl,  ""));

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

/* =========================================================================
 * Test cases – import resolution
 * ===================================================================== */

static void test_resolve_ordinal_hit(void)
{
    uint8_t          imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;
    uint16_t seg = 0xFFFFu, off = 0xFFFFu;

    TEST_BEGIN("import_resolve_ordinal: hit sets seg/off and returns OK");

    ASSERT_EQ(make_two_export_table(imgbuf, sizeof(imgbuf), &parser, &tbl),
              NE_IMPEXP_OK);

    ASSERT_EQ(ne_import_resolve_ordinal(&tbl, 2u, &seg, &off), NE_IMPEXP_OK);
    ASSERT_EQ(seg, (uint16_t)0);
    ASSERT_EQ(off, (uint16_t)0x0200u);

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

static void test_resolve_ordinal_null_table(void)
{
    uint16_t seg, off;
    TEST_BEGIN("import_resolve_ordinal: NULL table returns UNRESOLVED");
    ASSERT_EQ(ne_import_resolve_ordinal(NULL, 1u, &seg, &off),
              NE_IMPEXP_ERR_UNRESOLVED);
    TEST_PASS();
}

static void test_resolve_ordinal_miss(void)
{
    uint8_t          imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;
    uint16_t seg, off;

    TEST_BEGIN("import_resolve_ordinal: ordinal not in table returns UNRESOLVED");

    ASSERT_EQ(make_two_export_table(imgbuf, sizeof(imgbuf), &parser, &tbl),
              NE_IMPEXP_OK);

    ASSERT_EQ(ne_import_resolve_ordinal(&tbl, 99u, &seg, &off),
              NE_IMPEXP_ERR_UNRESOLVED);

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

static void test_resolve_ordinal_null_out(void)
{
    uint8_t          imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;
    uint16_t         x;

    TEST_BEGIN("import_resolve_ordinal: NULL out pointer returns ERR_NULL");

    ASSERT_EQ(make_two_export_table(imgbuf, sizeof(imgbuf), &parser, &tbl),
              NE_IMPEXP_OK);

    ASSERT_EQ(ne_import_resolve_ordinal(&tbl, 1u, NULL, &x),
              NE_IMPEXP_ERR_NULL);
    ASSERT_EQ(ne_import_resolve_ordinal(&tbl, 1u, &x,   NULL),
              NE_IMPEXP_ERR_NULL);

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

static void test_resolve_name_hit(void)
{
    uint8_t          imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;
    uint16_t seg = 0xFFFFu, off = 0xFFFFu;

    TEST_BEGIN("import_resolve_name: hit sets seg/off and returns OK");

    ASSERT_EQ(make_two_export_table(imgbuf, sizeof(imgbuf), &parser, &tbl),
              NE_IMPEXP_OK);

    ASSERT_EQ(ne_import_resolve_name(&tbl, "FuncA", &seg, &off), NE_IMPEXP_OK);
    ASSERT_EQ(seg, (uint16_t)0);
    ASSERT_EQ(off, (uint16_t)0x0100u);

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

static void test_resolve_name_null_table(void)
{
    uint16_t seg, off;
    TEST_BEGIN("import_resolve_name: NULL table returns UNRESOLVED");
    ASSERT_EQ(ne_import_resolve_name(NULL, "FuncA", &seg, &off),
              NE_IMPEXP_ERR_UNRESOLVED);
    TEST_PASS();
}

static void test_resolve_name_miss(void)
{
    uint8_t          imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;
    uint16_t seg, off;

    TEST_BEGIN("import_resolve_name: unknown name returns UNRESOLVED");

    ASSERT_EQ(make_two_export_table(imgbuf, sizeof(imgbuf), &parser, &tbl),
              NE_IMPEXP_OK);

    ASSERT_EQ(ne_import_resolve_name(&tbl, "NoSuchFunc", &seg, &off),
              NE_IMPEXP_ERR_UNRESOLVED);

    ne_export_free(&tbl);
    ne_free(&parser);
    TEST_PASS();
}

/* =========================================================================
 * Test cases – stub table
 * ===================================================================== */

static void test_stub_init_free(void)
{
    NEStubTable tbl;
    TEST_BEGIN("stub table init and free");
    ASSERT_EQ(ne_stub_table_init(&tbl, NE_STUB_TABLE_CAP), NE_IMPEXP_OK);
    ASSERT_NOT_NULL(tbl.entries);
    ASSERT_EQ(tbl.count,    (uint16_t)0);
    ASSERT_EQ(tbl.capacity, (uint16_t)NE_STUB_TABLE_CAP);
    ne_stub_table_free(&tbl);
    ASSERT_EQ(tbl.entries,  NULL);
    TEST_PASS();
}

static void test_stub_init_null(void)
{
    TEST_BEGIN("stub table init NULL returns error");
    ASSERT_EQ(ne_stub_table_init(NULL, 4u), NE_IMPEXP_ERR_NULL);
    TEST_PASS();
}

static void test_stub_init_zero_capacity(void)
{
    NEStubTable tbl;
    TEST_BEGIN("stub table init zero capacity returns error");
    ASSERT_EQ(ne_stub_table_init(&tbl, 0u), NE_IMPEXP_ERR_NULL);
    TEST_PASS();
}

static void test_stub_free_null(void)
{
    TEST_BEGIN("stub table free NULL is safe");
    ne_stub_table_free(NULL); /* must not crash */
    TEST_PASS();
}

static void test_stub_register_and_find_by_ordinal(void)
{
    NEStubTable        tbl;
    const NEStubEntry *e;

    TEST_BEGIN("stub register and find by ordinal");

    ASSERT_EQ(ne_stub_table_init(&tbl, 8u), NE_IMPEXP_OK);

    ASSERT_EQ(ne_stub_register(&tbl, "KERNEL", "GetVersion", 3u,
                                "returns 0x030A", "Step 6"), NE_IMPEXP_OK);
    ASSERT_EQ(tbl.count, (uint16_t)1);

    e = ne_stub_find_by_ordinal(&tbl, "KERNEL", 3u);
    ASSERT_NOT_NULL(e);
    ASSERT_STR_EQ(e->module_name, "KERNEL");
    ASSERT_STR_EQ(e->api_name,    "GetVersion");
    ASSERT_EQ(e->ordinal, (uint16_t)3u);
    ASSERT_STR_EQ(e->behavior,  "returns 0x030A");
    ASSERT_STR_EQ(e->milestone, "Step 6");
    ASSERT_EQ(e->removed, 0);

    ne_stub_table_free(&tbl);
    TEST_PASS();
}

static void test_stub_register_and_find_by_name(void)
{
    NEStubTable        tbl;
    const NEStubEntry *e;

    TEST_BEGIN("stub register and find by name");

    ASSERT_EQ(ne_stub_table_init(&tbl, 8u), NE_IMPEXP_OK);

    ASSERT_EQ(ne_stub_register(&tbl, "USER", "MessageBox", 1u,
                                "returns IDOK always", "Step 8"),
              NE_IMPEXP_OK);

    e = ne_stub_find_by_name(&tbl, "USER", "MessageBox");
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->ordinal, (uint16_t)1u);

    ne_stub_table_free(&tbl);
    TEST_PASS();
}

static void test_stub_register_duplicate_ignored(void)
{
    NEStubTable tbl;

    TEST_BEGIN("stub register: duplicate (module, ordinal) is silently ignored");

    ASSERT_EQ(ne_stub_table_init(&tbl, 8u), NE_IMPEXP_OK);

    ASSERT_EQ(ne_stub_register(&tbl, "KERNEL", "FuncX", 5u, "stub", "Step 6"),
              NE_IMPEXP_OK);
    ASSERT_EQ(ne_stub_register(&tbl, "KERNEL", "FuncX", 5u, "stub", "Step 6"),
              NE_IMPEXP_OK); /* duplicate */

    ASSERT_EQ(tbl.count, (uint16_t)1); /* still only one entry */

    ne_stub_table_free(&tbl);
    TEST_PASS();
}

static void test_stub_replace(void)
{
    NEStubTable        tbl;
    const NEStubEntry *e;

    TEST_BEGIN("stub replace marks entry as removed");

    ASSERT_EQ(ne_stub_table_init(&tbl, 8u), NE_IMPEXP_OK);

    ASSERT_EQ(ne_stub_register(&tbl, "GDI", "BitBlt", 10u, "no-op", "Step 8"),
              NE_IMPEXP_OK);

    /* Before replacement: removed == 0 */
    e = ne_stub_find_by_ordinal(&tbl, "GDI", 10u);
    ASSERT_NOT_NULL(e);
    ASSERT_EQ(e->removed, 0);

    /* Replace it */
    ASSERT_EQ(ne_stub_replace(&tbl, "GDI", 10u), NE_IMPEXP_OK);

    /* After replacement: removed == 1, entry still findable */
    e = ne_stub_find_by_ordinal(&tbl, "GDI", 10u);
    ASSERT_NOT_NULL(e);
    ASSERT_NE(e->removed, 0);

    ne_stub_table_free(&tbl);
    TEST_PASS();
}

static void test_stub_replace_not_found(void)
{
    NEStubTable tbl;

    TEST_BEGIN("stub replace: unknown entry returns UNRESOLVED");

    ASSERT_EQ(ne_stub_table_init(&tbl, 4u), NE_IMPEXP_OK);

    ASSERT_EQ(ne_stub_replace(&tbl, "KERNEL", 99u),
              NE_IMPEXP_ERR_UNRESOLVED);

    ne_stub_table_free(&tbl);
    TEST_PASS();
}

static void test_stub_table_full(void)
{
    NEStubTable tbl;
    int         rc;

    TEST_BEGIN("stub table: insert past capacity returns ERR_FULL");

    ASSERT_EQ(ne_stub_table_init(&tbl, 2u), NE_IMPEXP_OK);

    ASSERT_EQ(ne_stub_register(&tbl, "K", "A", 1u, "s", "m"), NE_IMPEXP_OK);
    ASSERT_EQ(ne_stub_register(&tbl, "K", "B", 2u, "s", "m"), NE_IMPEXP_OK);

    rc = ne_stub_register(&tbl, "K", "C", 3u, "s", "m");
    ASSERT_EQ(rc, NE_IMPEXP_ERR_FULL);

    ne_stub_table_free(&tbl);
    TEST_PASS();
}

static void test_stub_find_by_name_empty_name(void)
{
    NEStubTable tbl;

    TEST_BEGIN("stub find_by_name: empty api_name returns NULL");

    ASSERT_EQ(ne_stub_table_init(&tbl, 4u), NE_IMPEXP_OK);
    ASSERT_EQ(ne_stub_register(&tbl, "K", "Func", 1u, "s", "m"), NE_IMPEXP_OK);

    ASSERT_NULL(ne_stub_find_by_name(&tbl, "K", ""));

    ne_stub_table_free(&tbl);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * Stub fallback: register a stub when the target module is not loaded,
 * then replace it after the target module is loaded.
 * ---------------------------------------------------------------------- */
static void test_stub_fallback_and_replace(void)
{
    uint8_t          imgbuf[512];
    NEParserContext  parser;
    NEExportTable    tbl;
    NEStubTable      stubs;
    uint16_t         seg, off;
    const NEStubEntry *se;

    TEST_BEGIN("stub fallback: unresolved import registers stub, replacement succeeds");

    ASSERT_EQ(ne_stub_table_init(&stubs, 16u), NE_IMPEXP_OK);

    /* Phase 1: target module not loaded – resolution fails */
    ASSERT_EQ(ne_import_resolve_ordinal(NULL, 42u, &seg, &off),
              NE_IMPEXP_ERR_UNRESOLVED);

    /* Caller registers a stub to track the unresolved import */
    ASSERT_EQ(ne_stub_register(&stubs, "KERNEL", "SomeFunc", 42u,
                                "returns 0", "Step 6"), NE_IMPEXP_OK);

    se = ne_stub_find_by_ordinal(&stubs, "KERNEL", 42u);
    ASSERT_NOT_NULL(se);
    ASSERT_EQ(se->removed, 0);

    /* Phase 2: target module is now loaded – build its export table */
    {
        static const uint8_t rnt[] = {
            0x06u, 'K','E','R','N','E','L', 0x00u, 0x00u,
            0x08u, 'S','o','m','e','F','u','n','c', 0x2Au, 0x00u,
            0x00u
        };
        static const uint8_t etbl[] = {
            /* 41 null entries to position ordinal 42 */
            0x29u, 0x00u,     /* count=41, type=0 (null) → ordinals 1..41 */
            0x01u, 0x01u,     /* count=1,  type=1 (segment 1) */
            0x00u, 0x40u, 0x00u, /* flags=0, offset=0x0040 → ordinal 42 */
            0x00u
        };
        size_t sz = build_image(imgbuf,
                                rnt,  (uint16_t)sizeof(rnt),
                                etbl, (uint16_t)sizeof(etbl));
        ASSERT_EQ(ne_parse_buffer(imgbuf, sz, &parser), NE_OK);
        ASSERT_EQ(ne_export_build(imgbuf, sz, &parser, &tbl), NE_IMPEXP_OK);
    }

    /* Resolve the import against the now-loaded export table */
    ASSERT_EQ(ne_import_resolve_ordinal(&tbl, 42u, &seg, &off),
              NE_IMPEXP_OK);
    ASSERT_EQ(off, (uint16_t)0x0040u);

    /* Mark the stub as replaced */
    ASSERT_EQ(ne_stub_replace(&stubs, "KERNEL", 42u), NE_IMPEXP_OK);
    se = ne_stub_find_by_ordinal(&stubs, "KERNEL", 42u);
    ASSERT_NOT_NULL(se);
    ASSERT_NE(se->removed, 0);

    ne_export_free(&tbl);
    ne_free(&parser);
    ne_stub_table_free(&stubs);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * ne_impexp_strerror
 * ---------------------------------------------------------------------- */
static void test_strerror(void)
{
    TEST_BEGIN("strerror returns non-NULL strings for known codes");
    ASSERT_NOT_NULL(ne_impexp_strerror(NE_IMPEXP_OK));
    ASSERT_NOT_NULL(ne_impexp_strerror(NE_IMPEXP_ERR_NULL));
    ASSERT_NOT_NULL(ne_impexp_strerror(NE_IMPEXP_ERR_ALLOC));
    ASSERT_NOT_NULL(ne_impexp_strerror(NE_IMPEXP_ERR_IO));
    ASSERT_NOT_NULL(ne_impexp_strerror(NE_IMPEXP_ERR_UNRESOLVED));
    ASSERT_NOT_NULL(ne_impexp_strerror(NE_IMPEXP_ERR_FULL));
    ASSERT_NOT_NULL(ne_impexp_strerror(-999));
    TEST_PASS();
}

/* =========================================================================
 * main
 * ===================================================================== */

int main(void)
{
    printf("=== NE Import/Export Resolution Tests (Step 5) ===\n\n");

    /* Export table building */
    test_export_build_empty();
    test_export_build_fixed_entries();
    test_export_build_null_bundle();
    test_export_build_movable_entry();
    test_export_build_names();

    /* Find by ordinal */
    test_find_by_ordinal_hit();
    test_find_by_ordinal_miss();
    test_find_by_ordinal_null_table();

    /* Find by name */
    test_find_by_name_hit();
    test_find_by_name_miss();
    test_find_by_name_case_sensitive();
    test_find_by_name_null_args();

    /* Import resolution – ordinal */
    test_resolve_ordinal_hit();
    test_resolve_ordinal_null_table();
    test_resolve_ordinal_miss();
    test_resolve_ordinal_null_out();

    /* Import resolution – name */
    test_resolve_name_hit();
    test_resolve_name_null_table();
    test_resolve_name_miss();

    /* Stub table */
    test_stub_init_free();
    test_stub_init_null();
    test_stub_init_zero_capacity();
    test_stub_free_null();
    test_stub_register_and_find_by_ordinal();
    test_stub_register_and_find_by_name();
    test_stub_register_duplicate_ignored();
    test_stub_replace();
    test_stub_replace_not_found();
    test_stub_table_full();
    test_stub_find_by_name_empty_name();

    /* Stub fallback integration */
    test_stub_fallback_and_replace();

    /* Error strings */
    test_strerror();

    printf("\n=== Results: %d/%d passed",
           g_tests_passed, g_tests_run);
    if (g_tests_failed > 0)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

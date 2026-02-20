/*
 * test_ne_reloc.c - Verification tests for the NE-file relocation manager
 *                  (Step 3 of the WinDOS kernel-replacement roadmap)
 *
 * Each test builds a minimal NE binary image in memory, parses it with
 * ne_parse_buffer, loads segments with ne_load_buffer, parses relocation
 * records with ne_reloc_parse, and then applies them with ne_reloc_apply,
 * verifying the patched segment bytes.
 *
 * Build with:
 *   wcc -ml -za99 -wx -d2 -i=../src ../src/ne_parser.c ../src/ne_loader.c
 *       ../src/ne_reloc.c test_ne_reloc.c
 *   wlink system dos name test_ne_reloc.exe file test_ne_reloc.obj,ne_parser.obj,ne_loader.obj,ne_reloc.obj
 */

#include "../src/ne_parser.h"
#include "../src/ne_loader.h"
#include "../src/ne_reloc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Minimal test framework (mirrors test_ne_parser.c / test_ne_loader.c)
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

/* -------------------------------------------------------------------------
 * Binary image builder helpers
 * ---------------------------------------------------------------------- */

#define MZ_SIZE       64u
#define NE_HDR_SIZE   64u
#define SEG_DESC_SIZE  8u

/* Align shift used throughout: 4 → 16-byte logical sectors */
#define TEST_ALIGN_SHIFT  4u
#define TEST_SECTOR_SIZE  16u   /* 1 << TEST_ALIGN_SHIFT */

/*
 * Segment layout (all file-absolute byte offsets):
 *
 *   0x000 - 0x03F : MZ header
 *   0x040 - 0x07F : NE header
 *   0x080 - 0x087 : Segment descriptor 0 (CODE, with NE_SEG_RELOC)
 *   0x088 - 0x08F : Segment descriptor 1 (DATA, no reloc)
 *   0x090         : other-tables placeholder (all tables at same offset)
 *   ... padding ...
 *   0x0A0         : CODE segment content (SEG_CONTENT_LEN bytes)
 *                   sector 10 = 10 * 16 = 0xA0
 *   0x0B0         : relocation block for CODE segment
 *                   [0..1] : uint16_t record count
 *                   [2..2+count*8-1] : raw relocation records
 *   0x0D0         : DATA segment content (SEG_CONTENT_LEN bytes)
 *                   sector 13 = 13 * 16 = 0xD0
 *
 * Total file size: 0xD0 + SEG_CONTENT_LEN = 0xE0
 *
 * With up to 3 relocation records the reloc block is at most
 *   2 + 3*8 = 26 bytes (0xB0 .. 0xC9), comfortably before DATA at 0xD0.
 */
#define SEG_CONTENT_LEN  16u
#define SEG1_SECTOR      10u   /* CODE segment: byte 0x0A0 */
#define SEG2_SECTOR      13u   /* DATA segment: byte 0x0D0 */
#define RELOC_BLOCK_OFF  (SEG1_SECTOR * TEST_SECTOR_SIZE + SEG_CONTENT_LEN)
                               /* = 0x0B0 */
#define TOTAL_FILE_SIZE  (SEG2_SECTOR * TEST_SECTOR_SIZE + SEG_CONTENT_LEN)
                               /* = 0x0E0 = 224 */

static void put_u16(uint8_t *buf, size_t off, uint16_t v)
{
    buf[off]     = (uint8_t)(v & 0xFF);
    buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put_u32(uint8_t *buf, size_t off, uint32_t v)
{
    buf[off]     = (uint8_t)(v & 0xFF);
    buf[off + 1] = (uint8_t)((v >>  8) & 0xFF);
    buf[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    buf[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}

/*
 * build_reloc_ne_image
 *
 * Build a 224-byte NE image containing:
 *   - Segment 0 (CODE): SEG_CONTENT_LEN bytes starting at sector SEG1_SECTOR,
 *     with the NE_SEG_RELOC flag set.  Content is copied from seg1_content[].
 *   - Segment 1 (DATA): SEG_CONTENT_LEN bytes starting at sector SEG2_SECTOR,
 *     filled with seg2_fill.  No relocation records.
 *   - A relocation block for segment 0 at RELOC_BLOCK_OFF, containing
 *     reloc_count records taken from reloc_recs[].  Each record is 8 bytes;
 *     the caller supplies raw bytes (address_type, reloc_type, offset16,
 *     ref1_lo, ref1_hi, ref2_lo, ref2_hi, pad).
 *
 * Returns a heap-allocated buffer of TOTAL_FILE_SIZE bytes; caller must free().
 */
static uint8_t *build_reloc_ne_image(const uint8_t *seg1_content,
                                      uint8_t        seg2_fill,
                                      const uint8_t *reloc_recs,
                                      uint16_t       reloc_count)
{
    uint8_t  *buf;
    uint8_t  *ne;
    uint8_t  *sd;
    uint16_t  seg_tbl_rel;
    uint16_t  other_rel;
    uint32_t  seg1_abs;
    uint32_t  seg2_abs;

    buf = (uint8_t *)calloc(1, TOTAL_FILE_SIZE);
    if (!buf) return NULL;

    seg_tbl_rel = (uint16_t)NE_HDR_SIZE;                       /* 0x40 */
    other_rel   = (uint16_t)(NE_HDR_SIZE + 2u * SEG_DESC_SIZE);/* 0x50 */
    seg1_abs    = (uint32_t)SEG1_SECTOR << TEST_ALIGN_SHIFT;   /* 0xA0 */
    seg2_abs    = (uint32_t)SEG2_SECTOR << TEST_ALIGN_SHIFT;   /* 0xD0 */

    /* ---- MZ header ---- */
    put_u16(buf, 0x00, MZ_MAGIC);
    put_u32(buf, 0x3C, (uint32_t)MZ_SIZE);

    /* ---- NE header ---- */
    ne = buf + MZ_SIZE;
    put_u16(ne, 0x00, NE_MAGIC);
    ne[0x02] = 5;
    ne[0x03] = 0;
    put_u16(ne, 0x04, other_rel);     /* entry_table_offset  */
    put_u16(ne, 0x06, 0);             /* entry_table_length  */
    ne[0x0C] = NE_PFLAG_MULTIDATA;
    ne[0x0D] = NE_AFLAG_WINAPI;
    put_u16(ne, 0x0E, 2);             /* auto_data_seg = 2   */
    put_u16(ne, 0x10, 0x0200);        /* heap_size           */
    put_u16(ne, 0x12, 0x0400);        /* stack_size          */
    put_u16(ne, 0x14, 0x0000);        /* initial_ip = 0      */
    put_u16(ne, 0x16, 1);             /* initial_cs = seg 1  */
    put_u16(ne, 0x18, 0x0400);        /* initial_sp          */
    put_u16(ne, 0x1A, 2);             /* initial_ss = seg 2  */
    put_u16(ne, 0x1C, 2);             /* segment_count = 2   */
    put_u16(ne, 0x1E, 0);             /* module_ref_count    */
    put_u16(ne, 0x20, 0);             /* nonresident_name_size */
    put_u16(ne, 0x22, seg_tbl_rel);   /* segment_table_offset */
    put_u16(ne, 0x24, other_rel);
    put_u16(ne, 0x26, other_rel);
    put_u16(ne, 0x28, other_rel);
    put_u16(ne, 0x2A, other_rel);
    put_u32(ne, 0x2C, 0);
    put_u16(ne, 0x30, 0);
    put_u16(ne, 0x32, (uint16_t)TEST_ALIGN_SHIFT);
    put_u16(ne, 0x34, 0);
    ne[0x36] = NE_OS_WINDOWS;
    ne[0x37] = 0;
    put_u16(ne, 0x38, 0);
    put_u16(ne, 0x3A, 0);
    put_u16(ne, 0x3C, 0);
    ne[0x3E] = 0x0A;
    ne[0x3F] = 0x03;

    /* ---- Segment descriptor 0: CODE with NE_SEG_RELOC ---- */
    sd = ne + seg_tbl_rel;
    put_u16(sd, 0, (uint16_t)SEG1_SECTOR);          /* sector offset       */
    put_u16(sd, 2, (uint16_t)SEG_CONTENT_LEN);      /* length in file      */
    put_u16(sd, 4, (uint16_t)NE_SEG_RELOC);         /* flags: CODE + RELOC */
    put_u16(sd, 6, (uint16_t)SEG_CONTENT_LEN);      /* min_alloc           */

    /* ---- Segment descriptor 1: DATA (no reloc) ---- */
    sd = ne + seg_tbl_rel + SEG_DESC_SIZE;
    put_u16(sd, 0, (uint16_t)SEG2_SECTOR);
    put_u16(sd, 2, (uint16_t)SEG_CONTENT_LEN);
    put_u16(sd, 4, (uint16_t)NE_SEG_DATA);          /* flags: DATA         */
    put_u16(sd, 6, (uint16_t)SEG_CONTENT_LEN);

    /* ---- CODE segment content ---- */
    if (seg1_content)
        memcpy(buf + seg1_abs, seg1_content, SEG_CONTENT_LEN);

    /* ---- DATA segment content ---- */
    memset(buf + seg2_abs, seg2_fill, SEG_CONTENT_LEN);

    /* ---- Relocation block for segment 0 ---- */
    put_u16(buf, RELOC_BLOCK_OFF, reloc_count);
    if (reloc_recs && reloc_count > 0)
        memcpy(buf + RELOC_BLOCK_OFF + 2u, reloc_recs,
               (size_t)reloc_count * 8u);

    return buf;
}

/*
 * Convenience: encode one raw relocation record into an 8-byte buffer.
 */
static void encode_reloc(uint8_t *dst,
                          uint8_t  addr_type,
                          uint8_t  reloc_type,
                          uint16_t target_off,
                          uint16_t ref1,
                          uint16_t ref2)
{
    dst[0] = addr_type;
    dst[1] = reloc_type;
    dst[2] = (uint8_t)(target_off & 0xFF);
    dst[3] = (uint8_t)((target_off >> 8) & 0xFF);
    dst[4] = (uint8_t)(ref1 & 0xFF);
    dst[5] = (uint8_t)((ref1 >> 8) & 0xFF);
    dst[6] = (uint8_t)(ref2 & 0xFF);
    dst[7] = (uint8_t)((ref2 >> 8) & 0xFF);
}

/*
 * parse_and_load – helper used by every apply test.
 * Fills *parser, *loader, *rctx from the image buffer.
 * Returns NE_RELOC_OK on success.
 */
static int parse_and_load(const uint8_t    *buf,
                           NEParserContext  *parser,
                           NELoaderContext  *loader,
                           NERelocContext   *rctx)
{
    int rc;

    rc = ne_parse_buffer(buf, TOTAL_FILE_SIZE, parser);
    if (rc != NE_OK) return rc;

    rc = ne_load_buffer(buf, TOTAL_FILE_SIZE, parser, loader);
    if (rc != NE_LOAD_OK) { ne_free(parser); return rc; }

    rc = ne_reloc_parse(buf, TOTAL_FILE_SIZE, parser, rctx);
    if (rc != NE_RELOC_OK) {
        ne_loader_free(loader);
        ne_free(parser);
    }
    return rc;
}

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

/* 1 - NULL arguments are rejected by ne_reloc_parse */
static void test_parse_null_args(void)
{
    NEParserContext parser;
    NERelocContext  rctx;
    uint8_t         dummy = 0;
    int rc;

    TEST_BEGIN("null args rejected by ne_reloc_parse");

    memset(&parser, 0, sizeof(parser));
    rc = ne_reloc_parse(NULL, 1, &parser, &rctx);
    ASSERT_EQ(rc, NE_RELOC_ERR_NULL);

    rc = ne_reloc_parse(&dummy, 1, NULL, &rctx);
    ASSERT_EQ(rc, NE_RELOC_ERR_NULL);

    rc = ne_reloc_parse(&dummy, 1, &parser, NULL);
    ASSERT_EQ(rc, NE_RELOC_ERR_NULL);

    TEST_PASS();
}

/* 2 - No segments with NE_SEG_RELOC → 0 tables in context */
static void test_parse_no_reloc_segments(void)
{
    NEParserContext parser;
    NERelocContext  rctx;
    uint8_t zeros[SEG_CONTENT_LEN];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("no reloc-flag segments -> 0 tables");

    memset(zeros, 0, sizeof(zeros));
    /* Build image but override seg 0's flags to remove NE_SEG_RELOC */
    buf = build_reloc_ne_image(zeros, 0x00, NULL, 0);
    ASSERT_NOT_NULL(buf);

    /* Clear NE_SEG_RELOC from segment descriptor 0 */
    {
        uint8_t *ne = buf + MZ_SIZE;
        uint16_t seg_tbl_off = (uint16_t)NE_HDR_SIZE;
        put_u16(ne + seg_tbl_off, 4, 0); /* flags = 0 (CODE, no RELOC) */
    }

    rc = ne_parse_buffer(buf, TOTAL_FILE_SIZE, &parser);
    ASSERT_EQ(rc, NE_OK);

    rc = ne_reloc_parse(buf, TOTAL_FILE_SIZE, &parser, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);
    ASSERT_EQ(rctx.count, 0);

    ne_reloc_free(&rctx);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 3 - Segment with NE_SEG_RELOC but offset=0 (no file data) → skipped */
static void test_parse_reloc_flag_no_file_data(void)
{
    NEParserContext parser;
    NERelocContext  rctx;
    uint8_t *buf;
    int rc;

    TEST_BEGIN("reloc flag but offset=0 -> skipped");

    buf = build_reloc_ne_image(NULL, 0x00, NULL, 0);
    ASSERT_NOT_NULL(buf);

    /* Set sector offset of seg 0 to 0 (no file data) */
    {
        uint8_t *ne = buf + MZ_SIZE;
        uint16_t seg_tbl_off = (uint16_t)NE_HDR_SIZE;
        put_u16(ne + seg_tbl_off, 0, 0); /* offset = 0 */
    }

    rc = ne_parse_buffer(buf, TOTAL_FILE_SIZE, &parser);
    ASSERT_EQ(rc, NE_OK);

    rc = ne_reloc_parse(buf, TOTAL_FILE_SIZE, &parser, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);
    ASSERT_EQ(rctx.count, 0);

    ne_reloc_free(&rctx);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 4 - Segment with reloc count = 0 → table entry skipped */
static void test_parse_reloc_count_zero(void)
{
    NEParserContext parser;
    NERelocContext  rctx;
    uint8_t zeros[SEG_CONTENT_LEN];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("reloc count = 0 -> table entry skipped");

    memset(zeros, 0, sizeof(zeros));
    /* Build image with reloc_count=0 (no records) */
    buf = build_reloc_ne_image(zeros, 0x00, NULL, 0);
    ASSERT_NOT_NULL(buf);

    rc = ne_parse_buffer(buf, TOTAL_FILE_SIZE, &parser);
    ASSERT_EQ(rc, NE_OK);

    rc = ne_reloc_parse(buf, TOTAL_FILE_SIZE, &parser, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);
    ASSERT_EQ(rctx.count, 0);

    ne_reloc_free(&rctx);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 5 - Valid parse: 2 records are stored correctly */
static void test_parse_records_stored(void)
{
    NEParserContext parser;
    NERelocContext  rctx;
    uint8_t zeros[SEG_CONTENT_LEN];
    uint8_t recs[2 * 8];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("2 reloc records parsed and stored correctly");

    memset(zeros, 0, sizeof(zeros));
    memset(recs,  0, sizeof(recs));

    /* Record 0: SEG16 INTERNAL, offset=0, ref1=2, ref2=0 */
    encode_reloc(recs,
                 NE_RELOC_ADDR_SEG16,
                 NE_RELOC_TYPE_INTERNAL,
                 0x0000, 2, 0);

    /* Record 1: FAR32 INTERNAL, offset=4, ref1=1, ref2=0x1234 */
    encode_reloc(recs + 8,
                 NE_RELOC_ADDR_FAR32,
                 NE_RELOC_TYPE_INTERNAL,
                 0x0004, 1, 0x1234);

    buf = build_reloc_ne_image(zeros, 0x55, recs, 2);
    ASSERT_NOT_NULL(buf);

    rc = ne_parse_buffer(buf, TOTAL_FILE_SIZE, &parser);
    ASSERT_EQ(rc, NE_OK);

    rc = ne_reloc_parse(buf, TOTAL_FILE_SIZE, &parser, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);
    ASSERT_EQ(rctx.count, 1);
    ASSERT_NOT_NULL(rctx.tables);
    ASSERT_EQ(rctx.tables[0].seg_idx, 0);
    ASSERT_EQ(rctx.tables[0].count, 2);
    ASSERT_NOT_NULL(rctx.tables[0].records);

    ASSERT_EQ(rctx.tables[0].records[0].address_type,  NE_RELOC_ADDR_SEG16);
    ASSERT_EQ(rctx.tables[0].records[0].reloc_type,    NE_RELOC_TYPE_INTERNAL);
    ASSERT_EQ(rctx.tables[0].records[0].target_offset, 0x0000);
    ASSERT_EQ(rctx.tables[0].records[0].ref1,          2);
    ASSERT_EQ(rctx.tables[0].records[0].ref2,          0);

    ASSERT_EQ(rctx.tables[0].records[1].address_type,  NE_RELOC_ADDR_FAR32);
    ASSERT_EQ(rctx.tables[0].records[1].target_offset, 0x0004);
    ASSERT_EQ(rctx.tables[0].records[1].ref1,          1);
    ASSERT_EQ(rctx.tables[0].records[1].ref2,          0x1234);

    ne_reloc_free(&rctx);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 6 - Relocation block extends past file end → IO error */
static void test_parse_reloc_block_out_of_bounds(void)
{
    NEParserContext parser;
    NERelocContext  rctx;
    uint8_t zeros[SEG_CONTENT_LEN];
    uint8_t recs[1 * 8];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("reloc block out of file bounds -> NE_RELOC_ERR_IO");

    memset(zeros, 0, sizeof(zeros));
    memset(recs,  0, sizeof(recs));
    encode_reloc(recs, NE_RELOC_ADDR_SEG16, NE_RELOC_TYPE_INTERNAL,
                 0x0000, 2, 0);

    buf = build_reloc_ne_image(zeros, 0x00, recs, 1);
    ASSERT_NOT_NULL(buf);

    rc = ne_parse_buffer(buf, TOTAL_FILE_SIZE, &parser);
    ASSERT_EQ(rc, NE_OK);

    /* Truncate the visible length so the reloc block is out of range */
    rc = ne_reloc_parse(buf,
                        RELOC_BLOCK_OFF, /* cut off before reloc block */
                        &parser, &rctx);
    ASSERT_EQ(rc, NE_RELOC_ERR_IO);

    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 7 - NULL arguments are rejected by ne_reloc_apply */
static void test_apply_null_args(void)
{
    NELoaderContext loader;
    NERelocContext  rctx;
    NEParserContext parser;
    int rc;

    TEST_BEGIN("null args rejected by ne_reloc_apply");

    memset(&loader, 0, sizeof(loader));
    memset(&rctx,   0, sizeof(rctx));
    memset(&parser, 0, sizeof(parser));

    rc = ne_reloc_apply(NULL, &rctx, &parser, NULL, NULL);
    ASSERT_EQ(rc, NE_RELOC_ERR_NULL);

    rc = ne_reloc_apply(&loader, NULL, &parser, NULL, NULL);
    ASSERT_EQ(rc, NE_RELOC_ERR_NULL);

    rc = ne_reloc_apply(&loader, &rctx, NULL, NULL, NULL);
    ASSERT_EQ(rc, NE_RELOC_ERR_NULL);

    TEST_PASS();
}

/* 8 - Apply SEG16 internal relocation: segment index written at target */
static void test_apply_internal_seg16(void)
{
    NEParserContext parser;
    NELoaderContext loader;
    NERelocContext  rctx;
    uint8_t seg1[SEG_CONTENT_LEN];
    uint8_t recs[1 * 8];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("internal SEG16 reloc -> segment index at target offset");

    memset(seg1, 0, sizeof(seg1));
    /* Pre-fill target with 0xFFFF (non-additive chain terminator) */
    seg1[0] = 0xFF; seg1[1] = 0xFF;

    memset(recs, 0, sizeof(recs));
    /* SEG16 INTERNAL, non-additive, target_offset=0, ref1=2 (DATA seg), ref2=0 */
    encode_reloc(recs, NE_RELOC_ADDR_SEG16, NE_RELOC_TYPE_INTERNAL,
                 0x0000, 2, 0);

    buf = build_reloc_ne_image(seg1, 0x55, recs, 1);
    ASSERT_NOT_NULL(buf);

    rc = parse_and_load(buf, &parser, &loader, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);

    rc = ne_reloc_apply(&loader, &rctx, &parser, NULL, NULL);
    ASSERT_EQ(rc, NE_RELOC_OK);

    /* Bytes [0..1] of CODE segment should now hold the 1-based segment index (2) */
    ASSERT_EQ(loader.segments[0].data[0], 0x02);
    ASSERT_EQ(loader.segments[0].data[1], 0x00);

    ne_reloc_free(&rctx);
    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 9 - Apply OFF16 internal relocation: offset written at target */
static void test_apply_internal_off16(void)
{
    NEParserContext parser;
    NELoaderContext loader;
    NERelocContext  rctx;
    uint8_t seg1[SEG_CONTENT_LEN];
    uint8_t recs[1 * 8];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("internal OFF16 reloc -> target offset value at patch site");

    memset(seg1, 0, sizeof(seg1));
    seg1[2] = 0xFF; seg1[3] = 0xFF; /* chain terminator at offset 2 */

    memset(recs, 0, sizeof(recs));
    /* OFF16 INTERNAL, non-additive, target_offset=2, ref1=1, ref2=0x0ABC */
    encode_reloc(recs, NE_RELOC_ADDR_OFF16, NE_RELOC_TYPE_INTERNAL,
                 0x0002, 1, 0x0ABC);

    buf = build_reloc_ne_image(seg1, 0x55, recs, 1);
    ASSERT_NOT_NULL(buf);

    rc = parse_and_load(buf, &parser, &loader, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);

    rc = ne_reloc_apply(&loader, &rctx, &parser, NULL, NULL);
    ASSERT_EQ(rc, NE_RELOC_OK);

    /* Bytes [2..3] of CODE segment should hold 0x0ABC */
    ASSERT_EQ(loader.segments[0].data[2], 0xBC);
    ASSERT_EQ(loader.segments[0].data[3], 0x0A);

    ne_reloc_free(&rctx);
    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 10 - Apply FAR32 internal relocation: offset in low word, seg in high word */
static void test_apply_internal_far32(void)
{
    NEParserContext parser;
    NELoaderContext loader;
    NERelocContext  rctx;
    uint8_t seg1[SEG_CONTENT_LEN];
    uint8_t recs[1 * 8];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("internal FAR32 reloc -> off16:seg16 written at target");

    memset(seg1, 0, sizeof(seg1));
    /* chain terminator in low word at offset 4 */
    seg1[4] = 0xFF; seg1[5] = 0xFF;

    memset(recs, 0, sizeof(recs));
    /* FAR32 INTERNAL, non-additive, target_offset=4, ref1=2, ref2=0x1234 */
    encode_reloc(recs, NE_RELOC_ADDR_FAR32, NE_RELOC_TYPE_INTERNAL,
                 0x0004, 2, 0x1234);

    buf = build_reloc_ne_image(seg1, 0x55, recs, 1);
    ASSERT_NOT_NULL(buf);

    rc = parse_and_load(buf, &parser, &loader, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);

    rc = ne_reloc_apply(&loader, &rctx, &parser, NULL, NULL);
    ASSERT_EQ(rc, NE_RELOC_OK);

    /* Bytes [4..5] = offset (0x1234), bytes [6..7] = segment (0x0002) */
    ASSERT_EQ(loader.segments[0].data[4], 0x34);
    ASSERT_EQ(loader.segments[0].data[5], 0x12);
    ASSERT_EQ(loader.segments[0].data[6], 0x02);
    ASSERT_EQ(loader.segments[0].data[7], 0x00);

    ne_reloc_free(&rctx);
    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 11 - Chain following: a SEG16 record patches 2 chained locations */
static void test_apply_chain_seg16(void)
{
    NEParserContext parser;
    NELoaderContext loader;
    NERelocContext  rctx;
    uint8_t seg1[SEG_CONTENT_LEN];
    uint8_t recs[1 * 8];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("SEG16 chain -> both chained locations patched");

    memset(seg1, 0, sizeof(seg1));
    /*
     * Build a 2-link chain:
     *   offset 0: chain_next = 2 (next link is at offset 2)
     *   offset 2: chain_next = 0xFFFF (end of chain)
     */
    seg1[0] = 0x02; seg1[1] = 0x00; /* chain -> offset 2 */
    seg1[2] = 0xFF; seg1[3] = 0xFF; /* chain end          */

    memset(recs, 0, sizeof(recs));
    /* SEG16 INTERNAL, non-additive, first chain head = offset 0, ref1=2 */
    encode_reloc(recs, NE_RELOC_ADDR_SEG16, NE_RELOC_TYPE_INTERNAL,
                 0x0000, 2, 0);

    buf = build_reloc_ne_image(seg1, 0x55, recs, 1);
    ASSERT_NOT_NULL(buf);

    rc = parse_and_load(buf, &parser, &loader, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);

    rc = ne_reloc_apply(&loader, &rctx, &parser, NULL, NULL);
    ASSERT_EQ(rc, NE_RELOC_OK);

    /* Both offset 0 and offset 2 should now hold segment index 2 */
    ASSERT_EQ(loader.segments[0].data[0], 0x02);
    ASSERT_EQ(loader.segments[0].data[1], 0x00);
    ASSERT_EQ(loader.segments[0].data[2], 0x02);
    ASSERT_EQ(loader.segments[0].data[3], 0x00);

    ne_reloc_free(&rctx);
    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 12 - Additive SEG16 relocation: value is added to existing content */
static void test_apply_additive_seg16(void)
{
    NEParserContext parser;
    NELoaderContext loader;
    NERelocContext  rctx;
    uint8_t seg1[SEG_CONTENT_LEN];
    uint8_t recs[1 * 8];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("additive SEG16 reloc -> value added to existing word");

    memset(seg1, 0, sizeof(seg1));
    /* Pre-fill offset 0 with 0x0010 */
    seg1[0] = 0x10; seg1[1] = 0x00;

    memset(recs, 0, sizeof(recs));
    /* SEG16 INTERNAL | ADDITIVE, target_offset=0, ref1=2 */
    encode_reloc(recs,
                 NE_RELOC_ADDR_SEG16,
                 (uint8_t)(NE_RELOC_TYPE_INTERNAL | NE_RELOC_FLAG_ADDITIVE),
                 0x0000, 2, 0);

    buf = build_reloc_ne_image(seg1, 0x55, recs, 1);
    ASSERT_NOT_NULL(buf);

    rc = parse_and_load(buf, &parser, &loader, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);

    rc = ne_reloc_apply(&loader, &rctx, &parser, NULL, NULL);
    ASSERT_EQ(rc, NE_RELOC_OK);

    /* 0x0010 + seg_val(2) = 0x0012 */
    ASSERT_EQ(loader.segments[0].data[0], 0x12);
    ASSERT_EQ(loader.segments[0].data[1], 0x00);

    ne_reloc_free(&rctx);
    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 13 - Additive OFF16 relocation: offset value is added */
static void test_apply_additive_off16(void)
{
    NEParserContext parser;
    NELoaderContext loader;
    NERelocContext  rctx;
    uint8_t seg1[SEG_CONTENT_LEN];
    uint8_t recs[1 * 8];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("additive OFF16 reloc -> offset added to existing word");

    memset(seg1, 0, sizeof(seg1));
    seg1[0] = 0x00; seg1[1] = 0x10; /* existing value = 0x1000 */

    memset(recs, 0, sizeof(recs));
    /* OFF16 INTERNAL | ADDITIVE, target_offset=0, ref1=1, ref2=0x0200 */
    encode_reloc(recs,
                 NE_RELOC_ADDR_OFF16,
                 (uint8_t)(NE_RELOC_TYPE_INTERNAL | NE_RELOC_FLAG_ADDITIVE),
                 0x0000, 1, 0x0200);

    buf = build_reloc_ne_image(seg1, 0x55, recs, 1);
    ASSERT_NOT_NULL(buf);

    rc = parse_and_load(buf, &parser, &loader, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);

    rc = ne_reloc_apply(&loader, &rctx, &parser, NULL, NULL);
    ASSERT_EQ(rc, NE_RELOC_OK);

    /* 0x1000 + 0x0200 = 0x1200 */
    ASSERT_EQ(loader.segments[0].data[0], 0x00);
    ASSERT_EQ(loader.segments[0].data[1], 0x12);

    ne_reloc_free(&rctx);
    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 14 - OS_FIXUP records are silently skipped */
static void test_apply_os_fixup_skipped(void)
{
    NEParserContext parser;
    NELoaderContext loader;
    NERelocContext  rctx;
    uint8_t seg1[SEG_CONTENT_LEN];
    uint8_t recs[1 * 8];
    uint8_t *buf;
    int rc;
    uint8_t before0, before1;

    TEST_BEGIN("OS_FIXUP records silently skipped");

    memset(seg1, 0xAB, sizeof(seg1));

    memset(recs, 0, sizeof(recs));
    /* OS_FIXUP type 1 */
    encode_reloc(recs, NE_RELOC_ADDR_SEG16, NE_RELOC_TYPE_OS_FIXUP,
                 0x0000, 1, 0);

    buf = build_reloc_ne_image(seg1, 0x55, recs, 1);
    ASSERT_NOT_NULL(buf);

    rc = parse_and_load(buf, &parser, &loader, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);

    before0 = loader.segments[0].data[0];
    before1 = loader.segments[0].data[1];

    rc = ne_reloc_apply(&loader, &rctx, &parser, NULL, NULL);
    ASSERT_EQ(rc, NE_RELOC_OK);

    /* Segment data must be unchanged */
    ASSERT_EQ(loader.segments[0].data[0], before0);
    ASSERT_EQ(loader.segments[0].data[1], before1);

    ne_reloc_free(&rctx);
    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * Dummy import resolver used by tests 15 and 16
 * ---------------------------------------------------------------------- */
static int dummy_resolver(uint16_t mod_idx, uint16_t ref2,
                           int by_name,
                           const uint8_t *imported_names,
                           uint16_t imp_names_size,
                           uint16_t *out_seg, uint16_t *out_offset,
                           void *userdata)
{
    (void)by_name; (void)imported_names; (void)imp_names_size; (void)userdata;
    /* Simulate: module 1, ordinal 7 → segment 1, offset 0x0042 */
    if (mod_idx == 1 && ref2 == 7) {
        *out_seg    = 1;
        *out_offset = 0x0042;
        return NE_RELOC_OK;
    }
    return NE_RELOC_ERR_UNRESOLVED;
}

/* 15 - Imported ordinal resolved via callback */
static void test_apply_imported_ordinal(void)
{
    NEParserContext parser;
    NELoaderContext loader;
    NERelocContext  rctx;
    uint8_t seg1[SEG_CONTENT_LEN];
    uint8_t recs[1 * 8];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("imported ordinal resolved via callback -> FAR32 patched");

    memset(seg1, 0, sizeof(seg1));
    seg1[0] = 0xFF; seg1[1] = 0xFF; /* chain terminator */

    memset(recs, 0, sizeof(recs));
    /* FAR32 IMP_ORD, non-additive, target_offset=0, mod=1, ordinal=7 */
    encode_reloc(recs, NE_RELOC_ADDR_FAR32, NE_RELOC_TYPE_IMP_ORD,
                 0x0000, 1, 7);

    buf = build_reloc_ne_image(seg1, 0x55, recs, 1);
    ASSERT_NOT_NULL(buf);

    rc = parse_and_load(buf, &parser, &loader, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);

    rc = ne_reloc_apply(&loader, &rctx, &parser, dummy_resolver, NULL);
    ASSERT_EQ(rc, NE_RELOC_OK);

    /* Low word = offset (0x0042), high word = seg (0x0001) */
    ASSERT_EQ(loader.segments[0].data[0], 0x42);
    ASSERT_EQ(loader.segments[0].data[1], 0x00);
    ASSERT_EQ(loader.segments[0].data[2], 0x01);
    ASSERT_EQ(loader.segments[0].data[3], 0x00);

    ne_reloc_free(&rctx);
    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 16 - Imported name resolved via callback */
static void test_apply_imported_name(void)
{
    NEParserContext parser;
    NELoaderContext loader;
    NERelocContext  rctx;
    uint8_t seg1[SEG_CONTENT_LEN];
    uint8_t recs[1 * 8];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("imported name resolved via callback -> SEG16 patched");

    memset(seg1, 0, sizeof(seg1));
    seg1[4] = 0xFF; seg1[5] = 0xFF; /* chain terminator at offset 4 */

    memset(recs, 0, sizeof(recs));
    /*
     * SEG16 IMP_NAME, non-additive, target_offset=4.
     * Use mod=1, name_off=7 which the dummy resolver maps to seg=1.
     */
    encode_reloc(recs, NE_RELOC_ADDR_SEG16, NE_RELOC_TYPE_IMP_NAME,
                 0x0004, 1, 7);

    buf = build_reloc_ne_image(seg1, 0x55, recs, 1);
    ASSERT_NOT_NULL(buf);

    rc = parse_and_load(buf, &parser, &loader, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);

    rc = ne_reloc_apply(&loader, &rctx, &parser, dummy_resolver, NULL);
    ASSERT_EQ(rc, NE_RELOC_OK);

    /* SEG16 writes out_seg (1) at offset 4 */
    ASSERT_EQ(loader.segments[0].data[4], 0x01);
    ASSERT_EQ(loader.segments[0].data[5], 0x00);

    ne_reloc_free(&rctx);
    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 17 - Unresolvable import (no resolver) → NE_RELOC_ERR_UNRESOLVED */
static void test_apply_unresolved_no_resolver(void)
{
    NEParserContext parser;
    NELoaderContext loader;
    NERelocContext  rctx;
    uint8_t seg1[SEG_CONTENT_LEN];
    uint8_t recs[1 * 8];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("IMP_ORD with no resolver -> NE_RELOC_ERR_UNRESOLVED");

    memset(seg1, 0, sizeof(seg1));
    seg1[0] = 0xFF; seg1[1] = 0xFF;
    memset(recs, 0, sizeof(recs));
    encode_reloc(recs, NE_RELOC_ADDR_SEG16, NE_RELOC_TYPE_IMP_ORD,
                 0x0000, 1, 3);

    buf = build_reloc_ne_image(seg1, 0x55, recs, 1);
    ASSERT_NOT_NULL(buf);

    rc = parse_and_load(buf, &parser, &loader, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);

    rc = ne_reloc_apply(&loader, &rctx, &parser, NULL, NULL);
    ASSERT_EQ(rc, NE_RELOC_ERR_UNRESOLVED);

    ne_reloc_free(&rctx);
    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 18 - Resolver returning failure → NE_RELOC_ERR_UNRESOLVED */
static void test_apply_unresolved_resolver_fails(void)
{
    NEParserContext parser;
    NELoaderContext loader;
    NERelocContext  rctx;
    uint8_t seg1[SEG_CONTENT_LEN];
    uint8_t recs[1 * 8];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("resolver returns failure -> NE_RELOC_ERR_UNRESOLVED");

    memset(seg1, 0, sizeof(seg1));
    seg1[0] = 0xFF; seg1[1] = 0xFF;
    memset(recs, 0, sizeof(recs));
    /* ordinal 99 is not in the dummy resolver's table */
    encode_reloc(recs, NE_RELOC_ADDR_SEG16, NE_RELOC_TYPE_IMP_ORD,
                 0x0000, 1, 99);

    buf = build_reloc_ne_image(seg1, 0x55, recs, 1);
    ASSERT_NOT_NULL(buf);

    rc = parse_and_load(buf, &parser, &loader, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);

    rc = ne_reloc_apply(&loader, &rctx, &parser, dummy_resolver, NULL);
    ASSERT_EQ(rc, NE_RELOC_ERR_UNRESOLVED);

    ne_reloc_free(&rctx);
    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 19 - Apply PTR32 internal relocation: low word set, high word zeroed */
static void test_apply_internal_ptr32(void)
{
    NEParserContext parser;
    NELoaderContext loader;
    NERelocContext  rctx;
    uint8_t seg1[SEG_CONTENT_LEN];
    uint8_t recs[1 * 8];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("internal PTR32 reloc -> off16 set, high word cleared");

    memset(seg1, 0xFF, sizeof(seg1)); /* pre-fill with 0xFF */
    /* chain terminator in the first 2 bytes */
    seg1[8] = 0xFF; seg1[9] = 0xFF;

    memset(recs, 0, sizeof(recs));
    /* PTR32 INTERNAL, non-additive, target_offset=8, ref1=1, ref2=0x1234 */
    encode_reloc(recs, NE_RELOC_ADDR_PTR32, NE_RELOC_TYPE_INTERNAL,
                 0x0008, 1, 0x1234);

    buf = build_reloc_ne_image(seg1, 0x55, recs, 1);
    ASSERT_NOT_NULL(buf);

    rc = parse_and_load(buf, &parser, &loader, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);

    rc = ne_reloc_apply(&loader, &rctx, &parser, NULL, NULL);
    ASSERT_EQ(rc, NE_RELOC_OK);

    /* Low word = 0x1234, high word = 0x0000 (cleared) */
    ASSERT_EQ(loader.segments[0].data[8],  0x34);
    ASSERT_EQ(loader.segments[0].data[9],  0x12);
    ASSERT_EQ(loader.segments[0].data[10], 0x00);
    ASSERT_EQ(loader.segments[0].data[11], 0x00);

    ne_reloc_free(&rctx);
    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 20 - Internal relocation with invalid segment number → NE_RELOC_ERR_BAD_SEG */
static void test_apply_bad_internal_seg(void)
{
    NEParserContext parser;
    NELoaderContext loader;
    NERelocContext  rctx;
    uint8_t seg1[SEG_CONTENT_LEN];
    uint8_t recs[1 * 8];
    uint8_t *buf;
    int rc;

    TEST_BEGIN("INTERNAL with invalid seg ref -> NE_RELOC_ERR_BAD_SEG");

    memset(seg1, 0, sizeof(seg1));
    seg1[0] = 0xFF; seg1[1] = 0xFF;
    memset(recs, 0, sizeof(recs));
    /* ref1=99: far beyond the 2-segment module */
    encode_reloc(recs, NE_RELOC_ADDR_SEG16, NE_RELOC_TYPE_INTERNAL,
                 0x0000, 99, 0);

    buf = build_reloc_ne_image(seg1, 0x55, recs, 1);
    ASSERT_NOT_NULL(buf);

    rc = parse_and_load(buf, &parser, &loader, &rctx);
    ASSERT_EQ(rc, NE_RELOC_OK);

    rc = ne_reloc_apply(&loader, &rctx, &parser, NULL, NULL);
    ASSERT_EQ(rc, NE_RELOC_ERR_BAD_SEG);

    ne_reloc_free(&rctx);
    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 21 - ne_reloc_free is safe on a zeroed context and NULL */
static void test_free_safe(void)
{
    NERelocContext rctx;

    TEST_BEGIN("ne_reloc_free on zeroed context and NULL is safe");
    memset(&rctx, 0, sizeof(rctx));
    ne_reloc_free(&rctx);
    ne_reloc_free(NULL);
    TEST_PASS();
}

/* 22 - ne_reloc_strerror covers all known error codes */
static void test_strerror(void)
{
    TEST_BEGIN("ne_reloc_strerror returns non-NULL for all codes");
    ASSERT_NOT_NULL(ne_reloc_strerror(NE_RELOC_OK));
    ASSERT_NOT_NULL(ne_reloc_strerror(NE_RELOC_ERR_NULL));
    ASSERT_NOT_NULL(ne_reloc_strerror(NE_RELOC_ERR_ALLOC));
    ASSERT_NOT_NULL(ne_reloc_strerror(NE_RELOC_ERR_IO));
    ASSERT_NOT_NULL(ne_reloc_strerror(NE_RELOC_ERR_BAD_SEG));
    ASSERT_NOT_NULL(ne_reloc_strerror(NE_RELOC_ERR_UNRESOLVED));
    ASSERT_NOT_NULL(ne_reloc_strerror(NE_RELOC_ERR_ADDR_TYPE));
    ASSERT_NOT_NULL(ne_reloc_strerror(-999)); /* unknown */
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */

int main(void)
{
    printf("=== NE Relocation Management Tests ===\n\n");

    test_parse_null_args();
    test_parse_no_reloc_segments();
    test_parse_reloc_flag_no_file_data();
    test_parse_reloc_count_zero();
    test_parse_records_stored();
    test_parse_reloc_block_out_of_bounds();
    test_apply_null_args();
    test_apply_internal_seg16();
    test_apply_internal_off16();
    test_apply_internal_far32();
    test_apply_chain_seg16();
    test_apply_additive_seg16();
    test_apply_additive_off16();
    test_apply_os_fixup_skipped();
    test_apply_imported_ordinal();
    test_apply_imported_name();
    test_apply_unresolved_no_resolver();
    test_apply_unresolved_resolver_fails();
    test_apply_bad_internal_seg();
    test_apply_internal_ptr32();
    test_free_safe();
    test_strerror();

    printf("\n=== Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

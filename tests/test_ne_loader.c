/*
 * test_ne_loader.c - Integration tests for the NE-file loader (Step 2)
 *
 * Each test builds a minimal NE binary image in memory (or writes it to a
 * temporary file), parses it with ne_parse_buffer / ne_parse_file, then
 * verifies that ne_load_buffer / ne_load_file produces the expected loaded
 * segment layout.
 *
 * Build with:
 *   gcc -std=c99 -Wall -Wextra -I../src ../src/ne_parser.c \
 *       ../src/ne_loader.c test_ne_loader.c -o test_ne_loader
 */

#include "../src/ne_parser.h"
#include "../src/ne_loader.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Minimal test framework (mirrors test_ne_parser.c)
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
        printf("FAIL \xe2\x80\x93 %s (line %d)\n", (msg), __LINE__); \
        return; \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            g_tests_failed++; \
            printf("FAIL \xe2\x80\x93 expected %lld got %lld (line %d)\n", \
                   (long long)(b), (long long)(a), __LINE__); \
            return; \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            g_tests_failed++; \
            printf("FAIL \xe2\x80\x93 unexpected equal value %lld (line %d)\n", \
                   (long long)(a), __LINE__); \
            return; \
        } \
    } while (0)

#define ASSERT_NOT_NULL(p) \
    do { \
        if ((p) == NULL) { \
            g_tests_failed++; \
            printf("FAIL \xe2\x80\x93 unexpected NULL pointer (line %d)\n", __LINE__); \
            return; \
        } \
    } while (0)

/* -------------------------------------------------------------------------
 * Binary image builder helpers
 * ---------------------------------------------------------------------- */

/* Layout constants */
#define MZ_SIZE       64u
#define NE_HDR_SIZE   64u
#define SEG_DESC_SIZE  8u

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
 * build_ne_image_no_data - image where all segments have offset=0
 * (no file-backed data; pure BSS-like segments).
 *
 * Parameters:
 *   segment_count  – number of segments
 *   seg_length     – the length / min_alloc value for each segment
 *   initial_cs     – NE initial_cs field (1-based; 0 = no entry point)
 *   initial_ip     – NE initial_ip field
 *   out_len        – receives the total image size
 *
 * Returns heap buffer; caller must free().
 */
static uint8_t *build_ne_image_no_data(uint16_t segment_count,
                                        uint16_t seg_length,
                                        uint16_t initial_cs,
                                        uint16_t initial_ip,
                                        size_t  *out_len)
{
    const uint16_t seg_table_rel = (uint16_t)NE_HDR_SIZE;
    const uint16_t seg_table_sz  = (uint16_t)(segment_count * SEG_DESC_SIZE);
    const uint16_t entry_rel     = (uint16_t)(seg_table_rel + seg_table_sz);
    const uint32_t ne_off        = MZ_SIZE;
    const size_t   total         = (size_t)ne_off + NE_HDR_SIZE + seg_table_sz;
    uint16_t i;

    uint8_t *buf = (uint8_t *)calloc(1, total);
    if (!buf) return NULL;

    /* MZ header */
    put_u16(buf, 0x00, MZ_MAGIC);
    put_u32(buf, 0x3C, ne_off);

    /* NE header */
    uint8_t *ne = buf + ne_off;
    put_u16(ne, 0x00, NE_MAGIC);
    ne[0x02] = 5;
    ne[0x03] = 0;
    put_u16(ne, 0x04, entry_rel);        /* entry_table_offset  */
    put_u16(ne, 0x06, 0);                /* entry_table_length  */
    ne[0x0C] = NE_PFLAG_MULTIDATA;
    ne[0x0D] = NE_AFLAG_WINAPI;
    put_u16(ne, 0x0E, 1);                /* auto_data_seg       */
    put_u16(ne, 0x10, 0x1000);
    put_u16(ne, 0x12, 0x2000);
    put_u16(ne, 0x14, initial_ip);       /* initial_ip          */
    put_u16(ne, 0x16, initial_cs);       /* initial_cs          */
    put_u16(ne, 0x18, 0x2000);
    put_u16(ne, 0x1A, (segment_count >= 2) ? 2u : 0u);
    put_u16(ne, 0x1C, segment_count);
    put_u16(ne, 0x1E, 0);
    put_u16(ne, 0x20, 0);
    put_u16(ne, 0x22, seg_table_rel);
    put_u16(ne, 0x24, entry_rel);
    put_u16(ne, 0x26, entry_rel);
    put_u16(ne, 0x28, entry_rel);
    put_u16(ne, 0x2A, entry_rel);
    put_u32(ne, 0x2C, 0);
    put_u16(ne, 0x30, 0);
    put_u16(ne, 0x32, 4);                /* align_shift = 4 (16-byte sectors) */
    put_u16(ne, 0x34, 0);
    ne[0x36] = NE_OS_WINDOWS;
    ne[0x37] = 0;
    put_u16(ne, 0x38, 0);
    put_u16(ne, 0x3A, 0);
    put_u16(ne, 0x3C, 0);
    ne[0x3E] = 0x0A;
    ne[0x3F] = 0x03;

    /* Segment descriptors: offset=0 (no file data), length/min_alloc=seg_length */
    for (i = 0; i < segment_count; i++) {
        uint8_t *sd = ne + seg_table_rel + i * SEG_DESC_SIZE;
        put_u16(sd, 0, 0);                             /* offset = 0 (no data) */
        put_u16(sd, 2, seg_length);                    /* length               */
        put_u16(sd, 4, (i == 0) ? 0u : NE_SEG_DATA);  /* CODE / DATA          */
        put_u16(sd, 6, seg_length);                    /* min_alloc            */
    }

    if (out_len) *out_len = total;
    return buf;
}

/*
 * build_ne_image_with_data - image where segments contain actual file data.
 *
 * Uses align_shift=4 (16-byte sectors).  Two segments are placed immediately
 * after the header area.  Each segment is SEG_CONTENT_LEN bytes long and
 * filled with a caller-supplied fill byte pattern.
 *
 *   Segment 1: CODE, fill byte = fill1
 *   Segment 2: DATA, fill byte = fill2
 *
 * Header layout (file-absolute bytes):
 *   0x000 .. 0x03F : MZ header
 *   0x040 .. 0x07F : NE header
 *   0x080 .. 0x08F : segment descriptor 1
 *   0x090 .. 0x09F : segment descriptor 2
 *   0x0A0 .. 0x0AF : (other table placeholders – all at same offset, 0 bytes)
 *   sector 10 = 0xA0 : segment 1 data  (SEG_CONTENT_LEN bytes)
 *   sector 11 = 0xB0 : segment 2 data  (SEG_CONTENT_LEN bytes)
 */
#define SEG_CONTENT_LEN  16u  /* bytes of content per segment              */
/* NE header starts at 0x40; seg table at ne+64=0x80; 2 segs → ends at 0xA0.
 * With align_shift=4: sector 10 = 10*16 = 0xA0, sector 11 = 0xB0.        */
#define SEG1_SECTOR  10u
#define SEG2_SECTOR  11u

static uint8_t *build_ne_image_with_data(uint8_t fill1, uint8_t fill2,
                                          size_t *out_len)
{
    const uint32_t ne_off       = MZ_SIZE;           /* 0x40 */
    const uint16_t seg_tbl_rel  = (uint16_t)NE_HDR_SIZE; /* 0x40 rel to ne */
    const uint16_t other_rel    = (uint16_t)(seg_tbl_rel + 2u * SEG_DESC_SIZE);
    const uint32_t seg1_off     = (uint32_t)SEG1_SECTOR << 4; /* 0xA0 */
    const uint32_t seg2_off     = (uint32_t)SEG2_SECTOR << 4; /* 0xB0 */
    const size_t   total        = seg2_off + SEG_CONTENT_LEN; /* 0xC0 */
    uint32_t k;

    uint8_t *buf = (uint8_t *)calloc(1, total);
    if (!buf) return NULL;

    /* MZ header */
    put_u16(buf, 0x00, MZ_MAGIC);
    put_u32(buf, 0x3C, ne_off);

    /* NE header */
    uint8_t *ne = buf + ne_off;
    put_u16(ne, 0x00, NE_MAGIC);
    ne[0x02] = 5;
    ne[0x03] = 0;
    put_u16(ne, 0x04, other_rel);   /* entry_table_offset */
    put_u16(ne, 0x06, 0);           /* entry_table_length */
    ne[0x0C] = NE_PFLAG_MULTIDATA;
    ne[0x0D] = NE_AFLAG_WINAPI;
    put_u16(ne, 0x0E, 2);           /* auto_data_seg = 2  */
    put_u16(ne, 0x10, 0x0200);      /* heap_size          */
    put_u16(ne, 0x12, 0x0400);      /* stack_size         */
    put_u16(ne, 0x14, 0x0000);      /* initial_ip = 0     */
    put_u16(ne, 0x16, 1);           /* initial_cs = seg 1 */
    put_u16(ne, 0x18, 0x0400);      /* initial_sp         */
    put_u16(ne, 0x1A, 2);           /* initial_ss = seg 2 */
    put_u16(ne, 0x1C, 2);           /* segment_count = 2  */
    put_u16(ne, 0x1E, 0);
    put_u16(ne, 0x20, 0);
    put_u16(ne, 0x22, seg_tbl_rel);
    put_u16(ne, 0x24, other_rel);
    put_u16(ne, 0x26, other_rel);
    put_u16(ne, 0x28, other_rel);
    put_u16(ne, 0x2A, other_rel);
    put_u32(ne, 0x2C, 0);
    put_u16(ne, 0x30, 0);
    put_u16(ne, 0x32, 4);           /* align_shift = 4    */
    put_u16(ne, 0x34, 0);
    ne[0x36] = NE_OS_WINDOWS;
    ne[0x37] = 0;
    put_u16(ne, 0x38, 0);
    put_u16(ne, 0x3A, 0);
    put_u16(ne, 0x3C, 0);
    ne[0x3E] = 0x0A;
    ne[0x3F] = 0x03;

    /* Segment descriptor 1: CODE at sector SEG1_SECTOR */
    uint8_t *sd1 = ne + seg_tbl_rel;
    put_u16(sd1, 0, (uint16_t)SEG1_SECTOR);
    put_u16(sd1, 2, (uint16_t)SEG_CONTENT_LEN);
    put_u16(sd1, 4, 0);                          /* CODE */
    put_u16(sd1, 6, (uint16_t)SEG_CONTENT_LEN);

    /* Segment descriptor 2: DATA at sector SEG2_SECTOR */
    uint8_t *sd2 = ne + seg_tbl_rel + SEG_DESC_SIZE;
    put_u16(sd2, 0, (uint16_t)SEG2_SECTOR);
    put_u16(sd2, 2, (uint16_t)SEG_CONTENT_LEN);
    put_u16(sd2, 4, NE_SEG_DATA);
    put_u16(sd2, 6, (uint16_t)SEG_CONTENT_LEN);

    /* Fill segment data regions */
    for (k = 0; k < SEG_CONTENT_LEN; k++) {
        buf[seg1_off + k] = fill1;
        buf[seg2_off + k] = fill2;
    }

    if (out_len) *out_len = total;
    return buf;
}

/* -------------------------------------------------------------------------
 * Tests
 * ---------------------------------------------------------------------- */

/* 1 – NULL arguments are rejected */
static void test_null_args(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    uint8_t          dummy = 0;
    int ret;

    TEST_BEGIN("null args rejected");

    ret = ne_load_buffer(NULL, 1, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_ERR_NULL);

    ret = ne_load_buffer(&dummy, 1, NULL, &loader);
    ASSERT_EQ(ret, NE_LOAD_ERR_NULL);

    ret = ne_load_buffer(&dummy, 1, &parser, NULL);
    ASSERT_EQ(ret, NE_LOAD_ERR_NULL);

    ret = ne_load_file(NULL, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_ERR_NULL);

    ret = ne_load_file("/tmp/x", NULL, &loader);
    ASSERT_EQ(ret, NE_LOAD_ERR_NULL);

    ret = ne_load_file("/tmp/x", &parser, NULL);
    ASSERT_EQ(ret, NE_LOAD_ERR_NULL);

    TEST_PASS();
}

/* 2 – NE with 0 segments loads successfully */
static void test_zero_segments(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    size_t   len;
    uint8_t *buf = build_ne_image_no_data(0, 0, 0, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("NE with 0 segments loads OK");
    int ret = ne_parse_buffer(buf, len, &parser);
    ASSERT_EQ(ret, NE_OK);

    ret = ne_load_buffer(buf, len, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_OK);
    ASSERT_EQ(loader.count, 0);
    ASSERT_EQ(loader.segments, NULL);

    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 3 – Two BSS segments are allocated with the right sizes */
static void test_bss_segments_allocated(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    size_t   len;
    uint8_t *buf = build_ne_image_no_data(2, 0x80, 0, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("BSS segments allocated to correct size");
    int ret = ne_parse_buffer(buf, len, &parser);
    ASSERT_EQ(ret, NE_OK);

    ret = ne_load_buffer(buf, len, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_OK);
    ASSERT_EQ(loader.count, 2);
    ASSERT_NOT_NULL(loader.segments);

    /* Both segments have no file data */
    ASSERT_EQ((int)loader.segments[0].data_size, 0);
    ASSERT_EQ((int)loader.segments[1].data_size, 0);
    /* alloc_size = seg_length = 0x80 */
    ASSERT_EQ((int)loader.segments[0].alloc_size, 0x80);
    ASSERT_EQ((int)loader.segments[1].alloc_size, 0x80);
    /* Data buffers are allocated */
    ASSERT_NOT_NULL(loader.segments[0].data);
    ASSERT_NOT_NULL(loader.segments[1].data);

    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 4 – BSS segments are zero-filled */
static void test_bss_segments_zero_filled(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    size_t   len;
    uint16_t k;
    uint8_t *buf = build_ne_image_no_data(1, 0x40, 0, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("BSS segment memory is zero-filled");
    int ret = ne_parse_buffer(buf, len, &parser);
    ASSERT_EQ(ret, NE_OK);

    ret = ne_load_buffer(buf, len, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_OK);
    ASSERT_NOT_NULL(loader.segments[0].data);

    for (k = 0; k < 0x40; k++) {
        if (loader.segments[0].data[k] != 0) {
            ne_loader_free(&loader);
            ne_free(&parser);
            free(buf);
            TEST_FAIL("BSS byte is not zero");
        }
    }

    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 5 – Segment data is correctly copied from the file buffer */
static void test_segment_data_content(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    size_t   len;
    uint32_t k;
    const uint8_t fill1 = 0xAA, fill2 = 0x55;

    uint8_t *buf = build_ne_image_with_data(fill1, fill2, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("segment data bytes correctly loaded from file buffer");
    int ret = ne_parse_buffer(buf, len, &parser);
    ASSERT_EQ(ret, NE_OK);

    ret = ne_load_buffer(buf, len, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_OK);
    ASSERT_EQ(loader.count, 2);

    /* Verify CODE segment content */
    ASSERT_EQ((int)loader.segments[0].data_size, (int)SEG_CONTENT_LEN);
    for (k = 0; k < SEG_CONTENT_LEN; k++) {
        if (loader.segments[0].data[k] != fill1) {
            ne_loader_free(&loader);
            ne_free(&parser);
            free(buf);
            TEST_FAIL("CODE segment byte mismatch");
        }
    }

    /* Verify DATA segment content */
    ASSERT_EQ((int)loader.segments[1].data_size, (int)SEG_CONTENT_LEN);
    for (k = 0; k < SEG_CONTENT_LEN; k++) {
        if (loader.segments[1].data[k] != fill2) {
            ne_loader_free(&loader);
            ne_free(&parser);
            free(buf);
            TEST_FAIL("DATA segment byte mismatch");
        }
    }

    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 6 – Segment flags (CODE / DATA) are preserved */
static void test_segment_flags_preserved(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    size_t   len;
    uint8_t *buf = build_ne_image_with_data(0x11, 0x22, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("segment CODE/DATA flags preserved in loader context");
    int ret = ne_parse_buffer(buf, len, &parser);
    ASSERT_EQ(ret, NE_OK);

    ret = ne_load_buffer(buf, len, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_OK);

    ASSERT_EQ((int)(loader.segments[0].flags & NE_SEG_DATA), 0);   /* CODE */
    ASSERT_NE((int)(loader.segments[1].flags & NE_SEG_DATA), 0);   /* DATA */

    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 7 – Valid CS:IP entry point passes validation */
static void test_entry_point_valid(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    size_t   len;
    /* 1 segment, length=0x100; CS=1, IP=0x00 (within bounds) */
    uint8_t *buf = build_ne_image_no_data(1, 0x100, 1, 0x00, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("valid CS:IP entry point accepted");
    int ret = ne_parse_buffer(buf, len, &parser);
    ASSERT_EQ(ret, NE_OK);

    ret = ne_load_buffer(buf, len, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_OK);

    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 8 – CS index beyond segment count is rejected */
static void test_entry_cs_out_of_range(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    size_t   len;
    /* 1 segment; CS=2 is out of range (only segment 1 exists) */
    uint8_t *buf = build_ne_image_no_data(1, 0x100, 2, 0x00, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("CS index beyond segment count -> NE_LOAD_ERR_BOUNDS");
    int ret = ne_parse_buffer(buf, len, &parser);
    ASSERT_EQ(ret, NE_OK);

    ret = ne_load_buffer(buf, len, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_ERR_BOUNDS);

    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 9 – IP beyond segment alloc_size is rejected */
static void test_entry_ip_out_of_range(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    size_t   len;
    /* 1 segment, alloc=0x40; CS=1, IP=0x40 (== alloc_size, not < it) */
    uint8_t *buf = build_ne_image_no_data(1, 0x40, 1, 0x40, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("IP >= alloc_size -> NE_LOAD_ERR_BOUNDS");
    int ret = ne_parse_buffer(buf, len, &parser);
    ASSERT_EQ(ret, NE_OK);

    ret = ne_load_buffer(buf, len, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_ERR_BOUNDS);

    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 10 – CS=0 (DLL, no entry point) passes without bounds check */
static void test_no_entry_point(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    size_t   len;
    /* CS=0 means no entry point; any IP value must be ignored */
    uint8_t *buf = build_ne_image_no_data(2, 0x80, 0, 0xFFFF, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("CS=0 (no entry point) skips bounds check");
    int ret = ne_parse_buffer(buf, len, &parser);
    ASSERT_EQ(ret, NE_OK);

    ret = ne_load_buffer(buf, len, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_OK);

    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 11 – alloc_size is always >= data_size */
static void test_alloc_ge_data(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    size_t   len;
    uint8_t *buf = build_ne_image_with_data(0xCC, 0xDD, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("alloc_size >= data_size for every segment");
    int ret = ne_parse_buffer(buf, len, &parser);
    ASSERT_EQ(ret, NE_OK);

    ret = ne_load_buffer(buf, len, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_OK);

    for (uint16_t i = 0; i < loader.count; i++) {
        if (loader.segments[i].alloc_size < loader.segments[i].data_size) {
            ne_loader_free(&loader);
            ne_free(&parser);
            free(buf);
            TEST_FAIL("alloc_size < data_size");
        }
    }

    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 12 – file_off is stored for file-backed segments; 0 for BSS */
static void test_file_off_stored(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    size_t   len;
    uint8_t *buf = build_ne_image_with_data(0x01, 0x02, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("file_off stored correctly for file-backed segments");
    int ret = ne_parse_buffer(buf, len, &parser);
    ASSERT_EQ(ret, NE_OK);

    ret = ne_load_buffer(buf, len, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_OK);

    /* Segment 1: sector SEG1_SECTOR → file byte SEG1_SECTOR * 16 */
    ASSERT_EQ((int)loader.segments[0].file_off,
              (int)((uint32_t)SEG1_SECTOR << 4));
    /* Segment 2: sector SEG2_SECTOR → file byte SEG2_SECTOR * 16 */
    ASSERT_EQ((int)loader.segments[1].file_off,
              (int)((uint32_t)SEG2_SECTOR << 4));

    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 13 – ne_load_file round-trip with a temporary file */
static void test_load_file_roundtrip(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    const char *path = "/tmp/test_ne_loader_roundtrip.exe";
    size_t   len;
    const uint8_t fill1 = 0xBE, fill2 = 0xEF;

    uint8_t *buf = build_ne_image_with_data(fill1, fill2, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("ne_load_file round-trip with 2 segments");

    /* First parse the buffer to get the parser context */
    int ret = ne_parse_buffer(buf, len, &parser);
    ASSERT_EQ(ret, NE_OK);

    /* Write image to temp file */
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        ne_free(&parser);
        free(buf);
        TEST_FAIL("could not create temp file");
    }
    fwrite(buf, 1, len, fp);
    fclose(fp);
    free(buf);

    ret = ne_load_file(path, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_OK);
    ASSERT_EQ(loader.count, 2);
    ASSERT_NOT_NULL(loader.segments[0].data);
    ASSERT_EQ(loader.segments[0].data[0], fill1);
    ASSERT_EQ(loader.segments[1].data[0], fill2);

    ne_loader_free(&loader);
    ne_free(&parser);
    remove(path);
    TEST_PASS();
}

/* 14 – ne_load_file on non-existent path returns IO error */
static void test_load_file_not_found(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    size_t   len;
    uint8_t *buf = build_ne_image_no_data(0, 0, 0, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("ne_load_file on missing path -> NE_LOAD_ERR_IO");
    int ret = ne_parse_buffer(buf, len, &parser);
    ASSERT_EQ(ret, NE_OK);

    ret = ne_load_file("/tmp/__no_such_file_windos_loader__.exe", &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_ERR_IO);

    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* 15 – ne_loader_free is safe on zeroed context and NULL */
static void test_free_safe(void)
{
    NELoaderContext loader;

    TEST_BEGIN("ne_loader_free on zeroed context and NULL is safe");
    memset(&loader, 0, sizeof(loader));
    ne_loader_free(&loader);
    ne_loader_free(NULL);
    TEST_PASS();
}

/* 16 – ne_loader_strerror covers all known error codes */
static void test_strerror(void)
{
    TEST_BEGIN("ne_loader_strerror returns non-NULL for all codes");
    ASSERT_NOT_NULL(ne_loader_strerror(NE_LOAD_OK));
    ASSERT_NOT_NULL(ne_loader_strerror(NE_LOAD_ERR_NULL));
    ASSERT_NOT_NULL(ne_loader_strerror(NE_LOAD_ERR_NOMEM));
    ASSERT_NOT_NULL(ne_loader_strerror(NE_LOAD_ERR_IO));
    ASSERT_NOT_NULL(ne_loader_strerror(NE_LOAD_ERR_BOUNDS));
    ASSERT_NOT_NULL(ne_loader_strerror(-999));  /* unknown */
    TEST_PASS();
}

/* 17 – ne_loader_print_info does not crash */
static void test_print_info(void)
{
    NEParserContext  parser;
    NELoaderContext  loader;
    size_t   len;
    uint8_t *buf = build_ne_image_with_data(0x12, 0x34, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("ne_loader_print_info completes without crash");
    int ret = ne_parse_buffer(buf, len, &parser);
    ASSERT_EQ(ret, NE_OK);

    ret = ne_load_buffer(buf, len, &parser, &loader);
    ASSERT_EQ(ret, NE_LOAD_OK);

    FILE *devnull = fopen("/dev/null", "w");
    if (devnull) {
        ne_loader_print_info(&loader, &parser, devnull);
        fclose(devnull);
    } else {
        ne_loader_print_info(&loader, &parser, stdout);
    }
    /* NULL safety */
    ne_loader_print_info(NULL, NULL, stdout);

    ne_loader_free(&loader);
    ne_free(&parser);
    free(buf);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */

int main(void)
{
    printf("=== NE Loader Integration Tests ===\n\n");

    test_null_args();
    test_zero_segments();
    test_bss_segments_allocated();
    test_bss_segments_zero_filled();
    test_segment_data_content();
    test_segment_flags_preserved();
    test_entry_point_valid();
    test_entry_cs_out_of_range();
    test_entry_ip_out_of_range();
    test_no_entry_point();
    test_alloc_ge_data();
    test_file_off_stored();
    test_load_file_roundtrip();
    test_load_file_not_found();
    test_free_safe();
    test_strerror();
    test_print_info();

    printf("\n=== Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

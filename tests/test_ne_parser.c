/*
 * test_ne_parser.c - Unit tests for the NE-file parser
 *
 * Tests are self-contained: each test function builds a minimal binary
 * image in memory (or on disk) and verifies the parser's behaviour.
 *
 * Build with:
 *   gcc -std=c99 -Wall -Wextra -I../src ../src/ne_parser.c \
 *       test_ne_parser.c -o test_ne_parser
 */

#include "../src/ne_parser.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Minimal test framework
 * ---------------------------------------------------------------------- */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_BEGIN(name) \
    do { \
        g_tests_run++; \
        printf("  %-55s ", (name)); \
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
        printf("FAIL – %s (line %d)\n", (msg), __LINE__); \
        return; \
    } while (0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            g_tests_failed++; \
            printf("FAIL – expected %lld got %lld (line %d)\n", \
                   (long long)(b), (long long)(a), __LINE__); \
            return; \
        } \
    } while (0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            g_tests_failed++; \
            printf("FAIL – unexpected equal value %lld (line %d)\n", \
                   (long long)(a), __LINE__); \
            return; \
        } \
    } while (0)

#define ASSERT_NOT_NULL(p) \
    do { \
        if ((p) == NULL) { \
            g_tests_failed++; \
            printf("FAIL – unexpected NULL pointer (line %d)\n", __LINE__); \
            return; \
        } \
    } while (0)

/* -------------------------------------------------------------------------
 * Binary image builder helpers
 * ---------------------------------------------------------------------- */

/* Layout constants for our synthetic NE images */
#define MZ_SIZE      64u   /* sizeof MZHeader  */
#define NE_HDR_SIZE  64u   /* sizeof NEHeader  */
#define SEG_DESC_SIZE 8u   /* sizeof NESegmentDescriptor */

/* Write a little-endian uint16 into buf[offset] */
static void put_u16(uint8_t *buf, size_t offset, uint16_t v)
{
    buf[offset]     = (uint8_t)(v & 0xFF);
    buf[offset + 1] = (uint8_t)((v >> 8) & 0xFF);
}

/* Write a little-endian uint32 into buf[offset] */
static void put_u32(uint8_t *buf, size_t offset, uint32_t v)
{
    buf[offset]     = (uint8_t)(v & 0xFF);
    buf[offset + 1] = (uint8_t)((v >>  8) & 0xFF);
    buf[offset + 2] = (uint8_t)((v >> 16) & 0xFF);
    buf[offset + 3] = (uint8_t)((v >> 24) & 0xFF);
}

/*
 * Build a minimal valid NE image.
 *
 * Layout (all offsets are file-absolute):
 *   0x00 - 0x3F : MZ header (64 bytes)
 *   0x40 - 0x7F : NE header (64 bytes)
 *   0x80 - ...  : segment table  (segment_count * 8 bytes)
 *   ...         : entry table    (entry_size bytes, immediately after seg table)
 *
 * All other optional tables are pointed at the same offset as the entry
 * table (size = 0 from the parser's perspective).
 *
 * Parameters
 *   segment_count  – number of segment descriptors to include
 *   entry_size     – byte length of the entry table
 *   out_len        – receives the total image length
 *
 * Returns a heap-allocated buffer; caller must free().
 */
static uint8_t *build_ne_image(uint16_t segment_count,
                                uint16_t entry_size,
                                size_t  *out_len)
{
    /* Offsets relative to the NE header */
    const uint16_t seg_table_rel  = (uint16_t)NE_HDR_SIZE;
    const uint16_t seg_table_size = (uint16_t)(segment_count * SEG_DESC_SIZE);
    const uint16_t entry_rel      = (uint16_t)(seg_table_rel + seg_table_size);
    /* All other tables (resource, resident names, module-ref, imported names)
     * are placed at the same relative offset as the entry table so they have
     * 0 bytes of content from the parser's perspective. */
    const uint16_t other_rel      = entry_rel;

    const uint32_t ne_off = MZ_SIZE;
    const size_t   total  = (size_t)ne_off + NE_HDR_SIZE
                          + seg_table_size + entry_size;

    uint8_t *buf = (uint8_t *)calloc(1, total);
    if (!buf) return NULL;

    /* --- MZ header --- */
    put_u16(buf, 0x00, MZ_MAGIC);
    put_u32(buf, 0x3C, ne_off);  /* e_lfanew */

    /* --- NE header --- */
    uint8_t *ne = buf + ne_off;
    put_u16(ne, 0x00, NE_MAGIC);
    ne[0x02] = 5;   /* linker major */
    ne[0x03] = 0;   /* linker minor */
    put_u16(ne, 0x04, entry_rel);         /* entry_table_offset   */
    put_u16(ne, 0x06, entry_size);        /* entry_table_length   */
    ne[0x0C] = NE_PFLAG_MULTIDATA;       /* program_flags        */
    ne[0x0D] = NE_AFLAG_WINAPI;          /* app_flags            */
    put_u16(ne, 0x0E, 1);                 /* auto_data_seg        */
    put_u16(ne, 0x10, 0x1000);            /* heap_size            */
    put_u16(ne, 0x12, 0x2000);            /* stack_size           */
    put_u16(ne, 0x14, 0x0100);            /* initial_ip           */
    put_u16(ne, 0x16, 1);                 /* initial_cs           */
    put_u16(ne, 0x18, 0x2000);            /* initial_sp           */
    put_u16(ne, 0x1A, 2);                 /* initial_ss           */
    put_u16(ne, 0x1C, segment_count);     /* segment_count        */
    put_u16(ne, 0x1E, 0);                 /* module_ref_count     */
    put_u16(ne, 0x20, 0);                 /* nonresident_name_size*/
    put_u16(ne, 0x22, seg_table_rel);     /* segment_table_offset */
    put_u16(ne, 0x24, other_rel);         /* resource_table_offset*/
    put_u16(ne, 0x26, other_rel);         /* resident_name_table_offset */
    put_u16(ne, 0x28, other_rel);         /* module_ref_table_offset */
    put_u16(ne, 0x2A, other_rel);         /* imported_names_offset*/
    put_u32(ne, 0x2C, 0);                 /* nonresident_name (abs, unused) */
    put_u16(ne, 0x30, 0);                 /* movable_entry_count  */
    put_u16(ne, 0x32, 9);                 /* align_shift (512 B)  */
    put_u16(ne, 0x34, 0);                 /* resource_seg_count   */
    ne[0x36] = NE_OS_WINDOWS;            /* target_os            */
    ne[0x37] = 0;                         /* os2_flags            */
    put_u16(ne, 0x38, 0);
    put_u16(ne, 0x3A, 0);
    put_u16(ne, 0x3C, 0);
    ne[0x3E] = 0x0A;                     /* expected Win minor   */
    ne[0x3F] = 0x03;                     /* expected Win major = 3.10 */

    /* --- Segment descriptors --- */
    for (uint16_t i = 0; i < segment_count; i++) {
        uint8_t *sd = ne + seg_table_rel + i * SEG_DESC_SIZE;
        put_u16(sd, 0, (uint16_t)(i + 1)); /* offset (logical sector) */
        put_u16(sd, 2, 0x1000);            /* length: 4096 bytes      */
        /* first segment = CODE, second+ = DATA */
        put_u16(sd, 4, (i == 0) ? 0 : NE_SEG_DATA);
        put_u16(sd, 6, 0x1000);            /* min_alloc               */
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
    NEParserContext ctx;
    int ret;

    TEST_BEGIN("null args rejected");
    ret = ne_parse_buffer(NULL, 64, &ctx);
    ASSERT_EQ(ret, NE_ERR_NULL_ARG);

    ret = ne_parse_buffer((const uint8_t *)"x", 1, NULL);
    ASSERT_EQ(ret, NE_ERR_NULL_ARG);

    ret = ne_parse_file(NULL, &ctx);
    ASSERT_EQ(ret, NE_ERR_NULL_ARG);

    ret = ne_parse_file("any.exe", NULL);
    ASSERT_EQ(ret, NE_ERR_NULL_ARG);

    TEST_PASS();
}

/* 2 – Buffer too small to hold MZ header */
static void test_too_small(void)
{
    NEParserContext ctx;
    uint8_t buf[10] = {0};

    TEST_BEGIN("buffer too small -> NE_ERR_NOT_MZ");
    int ret = ne_parse_buffer(buf, sizeof(buf), &ctx);
    ASSERT_EQ(ret, NE_ERR_NOT_MZ);
    TEST_PASS();
}

/* 3 – Wrong MZ magic */
static void test_bad_mz_magic(void)
{
    NEParserContext ctx;
    size_t  len;
    uint8_t *buf = build_ne_image(0, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("bad MZ magic -> NE_ERR_NOT_MZ");
    buf[0] = 0xFF; buf[1] = 0xFF;   /* corrupt MZ magic */
    int ret = ne_parse_buffer(buf, len, &ctx);
    ASSERT_EQ(ret, NE_ERR_NOT_MZ);
    free(buf);
    TEST_PASS();
}

/* 4 – Wrong NE magic */
static void test_bad_ne_magic(void)
{
    NEParserContext ctx;
    size_t  len;
    uint8_t *buf = build_ne_image(0, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("bad NE magic -> NE_ERR_NOT_NE");
    /* MZ header is valid; corrupt NE magic */
    buf[MZ_SIZE]     = 0xAA;
    buf[MZ_SIZE + 1] = 0xBB;
    int ret = ne_parse_buffer(buf, len, &ctx);
    ASSERT_EQ(ret, NE_ERR_NOT_NE);
    free(buf);
    TEST_PASS();
}

/* 5 – NE offset points past end of file */
static void test_ne_offset_out_of_range(void)
{
    NEParserContext ctx;
    size_t  len;
    uint8_t *buf = build_ne_image(0, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("NE offset out of file -> NE_ERR_BAD_OFFSET");
    /* Set ne_offset to well beyond the file end */
    put_u32(buf, 0x3C, 0xFFFF0000u);
    int ret = ne_parse_buffer(buf, len, &ctx);
    ASSERT_EQ(ret, NE_ERR_BAD_OFFSET);
    free(buf);
    TEST_PASS();
}

/* 6 – Valid minimal NE with 0 segments */
static void test_valid_zero_segments(void)
{
    NEParserContext ctx;
    size_t  len;
    uint8_t *buf = build_ne_image(0, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("valid NE with 0 segments parses OK");
    int ret = ne_parse_buffer(buf, len, &ctx);
    ASSERT_EQ(ret, NE_OK);
    ASSERT_EQ(ctx.header.magic, NE_MAGIC);
    ASSERT_EQ(ctx.header.segment_count, 0);
    ASSERT_EQ(ctx.segments, NULL);
    ASSERT_EQ(ctx.header.target_os, NE_OS_WINDOWS);
    ne_free(&ctx);
    free(buf);
    TEST_PASS();
}

/* 7 – Valid NE with 2 segments; verify descriptors */
static void test_valid_two_segments(void)
{
    NEParserContext ctx;
    size_t  len;
    uint8_t *buf = build_ne_image(2, 4, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("valid NE with 2 segments – descriptors correct");
    int ret = ne_parse_buffer(buf, len, &ctx);
    ASSERT_EQ(ret, NE_OK);
    ASSERT_EQ(ctx.header.segment_count, 2);
    ASSERT_NOT_NULL(ctx.segments);

    /* Segment 1: CODE */
    ASSERT_EQ(ctx.segments[0].length,    0x1000);
    ASSERT_EQ(ctx.segments[0].min_alloc, 0x1000);
    ASSERT_EQ((int)(ctx.segments[0].flags & NE_SEG_DATA), 0);

    /* Segment 2: DATA */
    ASSERT_EQ(ctx.segments[1].length,    0x1000);
    ASSERT_NE((int)(ctx.segments[1].flags & NE_SEG_DATA), 0);

    ne_free(&ctx);
    free(buf);
    TEST_PASS();
}

/* 8 – Verify linker version fields */
static void test_linker_version(void)
{
    NEParserContext ctx;
    size_t  len;
    uint8_t *buf = build_ne_image(0, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("linker version fields parsed correctly");
    int ret = ne_parse_buffer(buf, len, &ctx);
    ASSERT_EQ(ret, NE_OK);
    ASSERT_EQ(ctx.header.linker_major, 5);
    ASSERT_EQ(ctx.header.linker_minor, 0);
    ne_free(&ctx);
    free(buf);
    TEST_PASS();
}

/* 9 – Verify entry-point offsets */
static void test_entry_point(void)
{
    NEParserContext ctx;
    size_t  len;
    uint8_t *buf = build_ne_image(2, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("entry-point CS/IP fields parsed correctly");
    int ret = ne_parse_buffer(buf, len, &ctx);
    ASSERT_EQ(ret, NE_OK);
    ASSERT_EQ(ctx.header.initial_cs, 1);
    ASSERT_EQ(ctx.header.initial_ip, 0x0100);
    ASSERT_EQ(ctx.header.initial_ss, 2);
    ASSERT_EQ(ctx.header.initial_sp, 0x2000);
    ne_free(&ctx);
    free(buf);
    TEST_PASS();
}

/* 10 – Verify Windows version fields */
static void test_windows_version(void)
{
    NEParserContext ctx;
    size_t  len;
    uint8_t *buf = build_ne_image(0, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("expected Windows version 3.10 parsed correctly");
    int ret = ne_parse_buffer(buf, len, &ctx);
    ASSERT_EQ(ret, NE_OK);
    ASSERT_EQ(ctx.header.expected_win_ver_major, 3);
    ASSERT_EQ(ctx.header.expected_win_ver_minor, 0x0A);
    ne_free(&ctx);
    free(buf);
    TEST_PASS();
}

/* 11 – Entry table data is captured */
static void test_entry_table_captured(void)
{
    NEParserContext ctx;
    size_t  len;
    const uint16_t entry_size = 8;
    uint8_t *buf = build_ne_image(1, entry_size, &len);
    ASSERT_NOT_NULL(buf);

    /* Write a recognisable pattern into the entry table bytes */
    uint32_t ne_off = MZ_SIZE;
    uint16_t seg_rel = NE_HDR_SIZE;
    uint16_t entry_rel = (uint16_t)(seg_rel + 1 * SEG_DESC_SIZE);
    uint8_t *entry_start = buf + ne_off + entry_rel;
    for (uint16_t i = 0; i < entry_size; i++)
        entry_start[i] = (uint8_t)(0xA0 + i);

    TEST_BEGIN("entry table bytes captured into ctx.entry_data");
    int ret = ne_parse_buffer(buf, len, &ctx);
    ASSERT_EQ(ret, NE_OK);
    ASSERT_EQ(ctx.entry_size, entry_size);
    ASSERT_NOT_NULL(ctx.entry_data);
    ASSERT_EQ(ctx.entry_data[0], 0xA0);
    ASSERT_EQ(ctx.entry_data[entry_size - 1], (uint8_t)(0xA0 + entry_size - 1));
    ne_free(&ctx);
    free(buf);
    TEST_PASS();
}

/* 12 – Segment table overflow is detected */
static void test_segment_table_overflow(void)
{
    NEParserContext ctx;
    size_t  len;
    /* Build a normal 2-segment image then lie about the segment count */
    uint8_t *buf = build_ne_image(2, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("segment table overflow -> NE_ERR_BAD_OFFSET");
    /* Overwrite segment_count with a huge number */
    put_u16(buf + MZ_SIZE, 0x1C, 0x7FFF);
    int ret = ne_parse_buffer(buf, len, &ctx);
    ASSERT_EQ(ret, NE_ERR_BAD_OFFSET);
    free(buf);
    TEST_PASS();
}

/* 13 – ne_free is safe to call with zeroed context */
static void test_free_zeroed_ctx(void)
{
    NEParserContext ctx;
    TEST_BEGIN("ne_free on zeroed context is safe");
    memset(&ctx, 0, sizeof(ctx));
    ne_free(&ctx);  /* must not crash */
    ne_free(NULL);  /* NULL is also safe */
    TEST_PASS();
}

/* 14 – ne_strerror covers all known error codes */
static void test_strerror(void)
{
    TEST_BEGIN("ne_strerror returns non-NULL for all codes");
    ASSERT_NOT_NULL(ne_strerror(NE_OK));
    ASSERT_NOT_NULL(ne_strerror(NE_ERR_IO));
    ASSERT_NOT_NULL(ne_strerror(NE_ERR_NOT_MZ));
    ASSERT_NOT_NULL(ne_strerror(NE_ERR_NOT_NE));
    ASSERT_NOT_NULL(ne_strerror(NE_ERR_BAD_OFFSET));
    ASSERT_NOT_NULL(ne_strerror(NE_ERR_ALLOC));
    ASSERT_NOT_NULL(ne_strerror(NE_ERR_BAD_HEADER));
    ASSERT_NOT_NULL(ne_strerror(NE_ERR_NULL_ARG));
    ASSERT_NOT_NULL(ne_strerror(-999));   /* unknown */
    TEST_PASS();
}

/* 15 – File-based API round-trip */
static void test_file_roundtrip(void)
{
    NEParserContext ctx;
    size_t  len;
    const char *path = "/tmp/test_ne_roundtrip.exe";

    uint8_t *buf = build_ne_image(3, 6, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("ne_parse_file round-trip with 3 segments");

    FILE *fp = fopen(path, "wb");
    if (!fp) { free(buf); TEST_FAIL("could not create temp file"); }
    fwrite(buf, 1, len, fp);
    fclose(fp);
    free(buf);

    int ret = ne_parse_file(path, &ctx);
    ASSERT_EQ(ret, NE_OK);
    ASSERT_EQ(ctx.header.segment_count, 3);
    ASSERT_NOT_NULL(ctx.segments);
    ASSERT_EQ(ctx.header.target_os, NE_OS_WINDOWS);
    ne_free(&ctx);
    remove(path);
    TEST_PASS();
}

/* 16 – ne_parse_file on non-existent path */
static void test_file_not_found(void)
{
    NEParserContext ctx;
    TEST_BEGIN("ne_parse_file on non-existent path -> NE_ERR_IO");
    int ret = ne_parse_file("/tmp/__no_such_file_windos_test__.exe", &ctx);
    ASSERT_EQ(ret, NE_ERR_IO);
    TEST_PASS();
}

/* 17 – ne_print_info does not crash */
static void test_print_info(void)
{
    NEParserContext ctx;
    size_t  len;
    uint8_t *buf = build_ne_image(2, 4, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("ne_print_info completes without crash");
    int ret = ne_parse_buffer(buf, len, &ctx);
    ASSERT_EQ(ret, NE_OK);

    /* Redirect to /dev/null to avoid cluttering test output */
    FILE *devnull = fopen("/dev/null", "w");
    if (devnull) {
        ne_print_info(&ctx, devnull);
        fclose(devnull);
    } else {
        ne_print_info(&ctx, stdout);
    }
    ne_free(&ctx);
    free(buf);
    TEST_PASS();
}

/* 18 – align_shift and segment file offset */
static void test_segment_align_shift(void)
{
    NEParserContext ctx;
    size_t  len;
    uint8_t *buf = build_ne_image(1, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("align_shift=9 -> sector size 512 bytes");
    int ret = ne_parse_buffer(buf, len, &ctx);
    ASSERT_EQ(ret, NE_OK);
    ASSERT_EQ(ctx.header.align_shift, 9);
    /* Segment 1 offset=1, so file offset = 1 << 9 = 512 */
    ASSERT_EQ((int)(ctx.segments[0].offset << ctx.header.align_shift), 512);
    ne_free(&ctx);
    free(buf);
    TEST_PASS();
}

/* 19 – program and application flags */
static void test_flags(void)
{
    NEParserContext ctx;
    size_t  len;
    uint8_t *buf = build_ne_image(0, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("program_flags and app_flags parsed correctly");
    int ret = ne_parse_buffer(buf, len, &ctx);
    ASSERT_EQ(ret, NE_OK);
    ASSERT_EQ(ctx.header.program_flags, NE_PFLAG_MULTIDATA);
    ASSERT_EQ(ctx.header.app_flags, NE_AFLAG_WINAPI);
    ne_free(&ctx);
    free(buf);
    TEST_PASS();
}

/* 20 – ne_offset stored correctly in context */
static void test_ne_offset_stored(void)
{
    NEParserContext ctx;
    size_t  len;
    uint8_t *buf = build_ne_image(0, 0, &len);
    ASSERT_NOT_NULL(buf);

    TEST_BEGIN("ctx.ne_offset reflects MZ e_lfanew field");
    int ret = ne_parse_buffer(buf, len, &ctx);
    ASSERT_EQ(ret, NE_OK);
    ASSERT_EQ((int)ctx.ne_offset, (int)MZ_SIZE);
    ne_free(&ctx);
    free(buf);
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */

int main(void)
{
    printf("=== NE Parser Unit Tests ===\n\n");

    test_null_args();
    test_too_small();
    test_bad_mz_magic();
    test_bad_ne_magic();
    test_ne_offset_out_of_range();
    test_valid_zero_segments();
    test_valid_two_segments();
    test_linker_version();
    test_entry_point();
    test_windows_version();
    test_entry_table_captured();
    test_segment_table_overflow();
    test_free_zeroed_ctx();
    test_strerror();
    test_file_roundtrip();
    test_file_not_found();
    test_print_info();
    test_segment_align_shift();
    test_flags();
    test_ne_offset_stored();

    printf("\n=== Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed)
        printf(", %d FAILED", g_tests_failed);
    printf(" ===\n");

    return (g_tests_failed == 0) ? 0 : 1;
}

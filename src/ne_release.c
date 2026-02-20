/*
 * ne_release.c - Phase 7 Release Readiness implementation
 *
 * Provides release readiness validation, regression suite tracking,
 * known-issue management, release metadata, and reproducible build
 * verification.
 *
 * Host-side: uses standard C malloc / calloc / free (via ne_dosalloc.h).
 * Watcom/DOS 16-bit target: the NE_MALLOC / NE_CALLOC / NE_FREE macros
 * expand to DOS INT 21h conventional-memory allocation.
 *
 * Reference: Microsoft Windows 3.1 SDK;
 *            Microsoft "New Executable" format specification.
 */

#include "ne_release.h"
#include "ne_dosalloc.h"

#include <string.h>

/* =========================================================================
 * ne_release_init / ne_release_free
 * ===================================================================== */

int ne_release_init(NEReleaseContext *ctx)
{
    uint8_t i;

    if (!ctx)
        return NE_RELEASE_ERR_NULL;

    memset(ctx, 0, sizeof(*ctx));

    for (i = 0; i < NE_RELEASE_ITEM_COUNT; i++) {
        ctx->items[i].item_id = i;
        ctx->items[i].status  = NE_RELEASE_STATUS_PENDING;
    }

    ctx->initialized = 1;
    return NE_RELEASE_OK;
}

void ne_release_free(NEReleaseContext *ctx)
{
    if (!ctx)
        return;
    memset(ctx, 0, sizeof(*ctx));
}

/* =========================================================================
 * Readiness item management
 * ===================================================================== */

int ne_release_set_status(NEReleaseContext *ctx,
                          uint8_t           item_id,
                          uint8_t           status)
{
    if (!ctx || !ctx->initialized)
        return NE_RELEASE_ERR_NULL;
    if (item_id >= NE_RELEASE_ITEM_COUNT)
        return NE_RELEASE_ERR_BAD_ITEM;
    if (status >= NE_RELEASE_STATUS_COUNT)
        return NE_RELEASE_ERR_BAD_STATUS;

    ctx->items[item_id].status = status;
    return NE_RELEASE_OK;
}

int ne_release_get_status(const NEReleaseContext *ctx,
                          uint8_t                 item_id)
{
    if (!ctx || !ctx->initialized)
        return NE_RELEASE_ERR_NULL;
    if (item_id >= NE_RELEASE_ITEM_COUNT)
        return NE_RELEASE_ERR_BAD_ITEM;

    return (int)ctx->items[item_id].status;
}

int ne_release_set_notes(NEReleaseContext *ctx,
                         uint8_t           item_id,
                         const char       *notes)
{
    if (!ctx || !ctx->initialized || !notes)
        return NE_RELEASE_ERR_NULL;
    if (item_id >= NE_RELEASE_ITEM_COUNT)
        return NE_RELEASE_ERR_BAD_ITEM;

    strncpy(ctx->items[item_id].notes, notes, NE_RELEASE_NOTES_MAX - 1);
    ctx->items[item_id].notes[NE_RELEASE_NOTES_MAX - 1] = '\0';
    return NE_RELEASE_OK;
}

const char *ne_release_get_notes(const NEReleaseContext *ctx,
                                 uint8_t                 item_id)
{
    if (!ctx || !ctx->initialized)
        return NULL;
    if (item_id >= NE_RELEASE_ITEM_COUNT)
        return NULL;

    return ctx->items[item_id].notes;
}

/* =========================================================================
 * Regression test suite
 * ===================================================================== */

int ne_release_add_regression(NEReleaseContext *ctx,
                              const char       *suite_name,
                              uint16_t          tests_total,
                              uint16_t          tests_passed,
                              uint16_t          tests_failed,
                              uint8_t           result)
{
    NEReleaseRegrEntry *entry;

    if (!ctx || !ctx->initialized || !suite_name)
        return NE_RELEASE_ERR_NULL;
    if (ctx->regression_count >= NE_RELEASE_REGR_CAP)
        return NE_RELEASE_ERR_FULL;

    entry = &ctx->regression[ctx->regression_count++];
    memset(entry, 0, sizeof(*entry));

    strncpy(entry->suite_name, suite_name, NE_RELEASE_NAME_MAX - 1);
    entry->suite_name[NE_RELEASE_NAME_MAX - 1] = '\0';
    entry->tests_total  = tests_total;
    entry->tests_passed = tests_passed;
    entry->tests_failed = tests_failed;
    entry->result       = result;

    return NE_RELEASE_OK;
}

uint16_t ne_release_regression_count(const NEReleaseContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;
    return ctx->regression_count;
}

const NEReleaseRegrEntry *ne_release_get_regression(
    const NEReleaseContext *ctx, uint16_t index)
{
    if (!ctx || !ctx->initialized)
        return NULL;
    if (index >= ctx->regression_count)
        return NULL;
    return &ctx->regression[index];
}

int ne_release_regression_all_pass(const NEReleaseContext *ctx)
{
    uint16_t i;

    if (!ctx || !ctx->initialized)
        return 0;
    if (ctx->regression_count == 0)
        return 0;

    for (i = 0; i < ctx->regression_count; i++) {
        if (ctx->regression[i].result != NE_RELEASE_TEST_PASS)
            return 0;
    }
    return 1;
}

/* =========================================================================
 * Known issues
 * ===================================================================== */

int ne_release_add_known_issue(NEReleaseContext *ctx,
                               const char       *description,
                               uint8_t           severity)
{
    NEReleaseKnownIssue *issue;

    if (!ctx || !ctx->initialized || !description)
        return NE_RELEASE_ERR_NULL;
    if (ctx->known_issue_count >= NE_RELEASE_KNOWN_ISSUES_CAP)
        return NE_RELEASE_ERR_FULL;

    issue = &ctx->known_issues[ctx->known_issue_count++];
    memset(issue, 0, sizeof(*issue));

    strncpy(issue->description, description, NE_RELEASE_NOTES_MAX - 1);
    issue->description[NE_RELEASE_NOTES_MAX - 1] = '\0';
    issue->severity = severity;

    return NE_RELEASE_OK;
}

uint16_t ne_release_known_issue_count(const NEReleaseContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;
    return ctx->known_issue_count;
}

const NEReleaseKnownIssue *ne_release_get_known_issue(
    const NEReleaseContext *ctx, uint16_t index)
{
    if (!ctx || !ctx->initialized)
        return NULL;
    if (index >= ctx->known_issue_count)
        return NULL;
    return &ctx->known_issues[index];
}

/* =========================================================================
 * Release metadata
 * ===================================================================== */

int ne_release_set_version(NEReleaseContext *ctx, const char *version)
{
    if (!ctx || !ctx->initialized || !version)
        return NE_RELEASE_ERR_NULL;

    strncpy(ctx->metadata.version, version, NE_RELEASE_NAME_MAX - 1);
    ctx->metadata.version[NE_RELEASE_NAME_MAX - 1] = '\0';
    return NE_RELEASE_OK;
}

int ne_release_set_tag(NEReleaseContext *ctx, const char *tag)
{
    if (!ctx || !ctx->initialized || !tag)
        return NE_RELEASE_ERR_NULL;

    strncpy(ctx->metadata.tag, tag, NE_RELEASE_NAME_MAX - 1);
    ctx->metadata.tag[NE_RELEASE_NAME_MAX - 1] = '\0';
    ctx->metadata.tagged = 1;
    return NE_RELEASE_OK;
}

int ne_release_set_date(NEReleaseContext *ctx, const char *date)
{
    if (!ctx || !ctx->initialized || !date)
        return NE_RELEASE_ERR_NULL;

    strncpy(ctx->metadata.date, date, NE_RELEASE_NAME_MAX - 1);
    ctx->metadata.date[NE_RELEASE_NAME_MAX - 1] = '\0';
    return NE_RELEASE_OK;
}

const NEReleaseMetadata *ne_release_get_metadata(
    const NEReleaseContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return NULL;
    return &ctx->metadata;
}

/* =========================================================================
 * Reproducible build verification
 * ===================================================================== */

int ne_release_add_build_hash(NEReleaseContext *ctx,
                              const char       *build_env,
                              uint32_t          hash)
{
    NEReleaseBuildHash *entry;

    if (!ctx || !ctx->initialized || !build_env)
        return NE_RELEASE_ERR_NULL;
    if (ctx->build_hash_count >= NE_RELEASE_BUILD_HASH_CAP)
        return NE_RELEASE_ERR_FULL;

    entry = &ctx->build_hashes[ctx->build_hash_count++];
    memset(entry, 0, sizeof(*entry));

    strncpy(entry->build_env, build_env, NE_RELEASE_NAME_MAX - 1);
    entry->build_env[NE_RELEASE_NAME_MAX - 1] = '\0';
    entry->hash = hash;

    return NE_RELEASE_OK;
}

uint16_t ne_release_build_hash_count(const NEReleaseContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;
    return ctx->build_hash_count;
}

int ne_release_verify_reproducible(const NEReleaseContext *ctx)
{
    uint16_t i;

    if (!ctx || !ctx->initialized)
        return 0;
    if (ctx->build_hash_count < 2)
        return 0;

    for (i = 1; i < ctx->build_hash_count; i++) {
        if (ctx->build_hashes[i].hash != ctx->build_hashes[0].hash)
            return 0;
    }
    return 1;
}

/* =========================================================================
 * Overall readiness check
 * ===================================================================== */

int ne_release_is_ready(const NEReleaseContext *ctx)
{
    uint8_t i;

    if (!ctx || !ctx->initialized)
        return 0;

    for (i = 0; i < NE_RELEASE_ITEM_COUNT; i++) {
        if (ctx->items[i].status != NE_RELEASE_STATUS_PASS)
            return 0;
    }
    return 1;
}

/* =========================================================================
 * String helpers
 * ===================================================================== */

const char *ne_release_item_name(uint8_t item_id)
{
    switch (item_id) {
    case NE_RELEASE_ITEM_BOOT_SEQ:      return "Boot sequence validation";
    case NE_RELEASE_ITEM_REGRESSION:    return "Regression test suite";
    case NE_RELEASE_ITEM_INSTALL_GUIDE: return "Installation guide";
    case NE_RELEASE_ITEM_DEV_GUIDE:     return "Developer guide";
    case NE_RELEASE_ITEM_REPRO_BUILD:   return "Reproducible build";
    case NE_RELEASE_ITEM_RELEASE_TAG:   return "Release tag";
    default:                            return "UNKNOWN";
    }
}

const char *ne_release_status_name(uint8_t status)
{
    switch (status) {
    case NE_RELEASE_STATUS_PENDING:     return "PENDING";
    case NE_RELEASE_STATUS_IN_PROGRESS: return "IN_PROGRESS";
    case NE_RELEASE_STATUS_PASS:        return "PASS";
    case NE_RELEASE_STATUS_FAIL:        return "FAIL";
    default:                            return "UNKNOWN";
    }
}

const char *ne_release_strerror(int err)
{
    switch (err) {
    case NE_RELEASE_OK:             return "success";
    case NE_RELEASE_ERR_NULL:       return "NULL pointer argument";
    case NE_RELEASE_ERR_BAD_ITEM:   return "item ID out of range";
    case NE_RELEASE_ERR_BAD_STATUS: return "invalid status value";
    case NE_RELEASE_ERR_FULL:       return "table at capacity";
    case NE_RELEASE_ERR_INIT:       return "context not initialised";
    default:                        return "unknown error";
    }
}

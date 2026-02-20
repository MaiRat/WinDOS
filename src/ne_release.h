/*
 * ne_release.h - Phase 7 Release Readiness
 *
 * Implements the release readiness validation and tracking capabilities
 * required for the final phase of the WinDOS kernel replacement project:
 *
 *   1. Boot sequence validation: end-to-end boot on target DOS hardware
 *      or emulator.
 *
 *   2. Regression test suite: execute the complete test suite and confirm
 *      zero regressions across all prior phases.
 *
 *   3. Installation guide tracking: record completion of user-facing
 *      documentation (DOS setup, file placement, boot procedure).
 *
 *   4. Developer guide tracking: record completion of developer-facing
 *      documentation (architecture, build instructions, contribution
 *      workflow).
 *
 *   5. Reproducible build verification: confirm bit-identical output
 *      across clean environments.
 *
 *   6. Release tagging: track versioned release metadata, changelog,
 *      and known-issues list.
 *
 * Reference: Microsoft Windows 3.1 SDK;
 *            Microsoft "New Executable" format specification.
 */

#ifndef NE_RELEASE_H
#define NE_RELEASE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_RELEASE_OK                0
#define NE_RELEASE_ERR_NULL         -1   /* NULL pointer argument             */
#define NE_RELEASE_ERR_BAD_ITEM     -2   /* item ID out of range              */
#define NE_RELEASE_ERR_BAD_STATUS   -3   /* invalid status value              */
#define NE_RELEASE_ERR_FULL         -4   /* table at capacity                 */
#define NE_RELEASE_ERR_INIT         -5   /* context not initialised           */

/* -------------------------------------------------------------------------
 * Configuration constants
 * ---------------------------------------------------------------------- */
#define NE_RELEASE_NAME_MAX         64u  /* max name/string field length      */
#define NE_RELEASE_NOTES_MAX       128u  /* max notes field length            */
#define NE_RELEASE_KNOWN_ISSUES_CAP 32u  /* max known-issue entries           */
#define NE_RELEASE_REGR_CAP         32u  /* max regression suite entries      */

/* -------------------------------------------------------------------------
 * Release readiness item identifiers
 * ---------------------------------------------------------------------- */
#define NE_RELEASE_ITEM_BOOT_SEQ      0u /* end-to-end boot validation       */
#define NE_RELEASE_ITEM_REGRESSION    1u /* regression test suite execution   */
#define NE_RELEASE_ITEM_INSTALL_GUIDE 2u /* installation guide written       */
#define NE_RELEASE_ITEM_DEV_GUIDE     3u /* developer guide written          */
#define NE_RELEASE_ITEM_REPRO_BUILD   4u /* reproducible build verified      */
#define NE_RELEASE_ITEM_RELEASE_TAG   5u /* versioned release tagged         */

#define NE_RELEASE_ITEM_COUNT         6u /* total readiness items             */

/* -------------------------------------------------------------------------
 * Readiness status codes
 * ---------------------------------------------------------------------- */
#define NE_RELEASE_STATUS_PENDING     0u
#define NE_RELEASE_STATUS_IN_PROGRESS 1u
#define NE_RELEASE_STATUS_PASS        2u
#define NE_RELEASE_STATUS_FAIL        3u

#define NE_RELEASE_STATUS_COUNT       4u

/* -------------------------------------------------------------------------
 * Known-issue severity
 * ---------------------------------------------------------------------- */
#define NE_RELEASE_SEV_LOW            0u
#define NE_RELEASE_SEV_MEDIUM         1u
#define NE_RELEASE_SEV_HIGH           2u
#define NE_RELEASE_SEV_CRITICAL       3u

/* -------------------------------------------------------------------------
 * Regression test result codes
 * ---------------------------------------------------------------------- */
#define NE_RELEASE_TEST_NOT_RUN       0u
#define NE_RELEASE_TEST_PASS          1u
#define NE_RELEASE_TEST_FAIL          2u
#define NE_RELEASE_TEST_SKIP          3u

/* -------------------------------------------------------------------------
 * Per-item readiness record
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t item_id;                       /* NE_RELEASE_ITEM_*              */
    uint8_t status;                        /* NE_RELEASE_STATUS_*            */
    char    notes[NE_RELEASE_NOTES_MAX];   /* human-readable notes           */
} NEReleaseItem;

/* -------------------------------------------------------------------------
 * Regression test entry
 * ---------------------------------------------------------------------- */
typedef struct {
    char    suite_name[NE_RELEASE_NAME_MAX]; /* test suite / phase name      */
    uint16_t tests_total;                    /* total tests in suite         */
    uint16_t tests_passed;                   /* tests passed                 */
    uint16_t tests_failed;                   /* tests failed                 */
    uint8_t  result;                         /* NE_RELEASE_TEST_*            */
} NEReleaseRegrEntry;

/* -------------------------------------------------------------------------
 * Known-issue entry
 * ---------------------------------------------------------------------- */
typedef struct {
    char    description[NE_RELEASE_NOTES_MAX]; /* issue description          */
    uint8_t severity;                          /* NE_RELEASE_SEV_*           */
} NEReleaseKnownIssue;

/* -------------------------------------------------------------------------
 * Release metadata
 * ---------------------------------------------------------------------- */
typedef struct {
    char    version[NE_RELEASE_NAME_MAX];    /* version string (e.g. "1.0.0") */
    char    tag[NE_RELEASE_NAME_MAX];        /* git tag name                  */
    char    date[NE_RELEASE_NAME_MAX];       /* release date string           */
    int     tagged;                          /* non-zero if tag is set        */
} NEReleaseMetadata;

/* -------------------------------------------------------------------------
 * Build hash record for reproducible build verification
 * ---------------------------------------------------------------------- */
typedef struct {
    char     build_env[NE_RELEASE_NAME_MAX]; /* environment description      */
    uint32_t hash;                           /* build output hash             */
} NEReleaseBuildHash;

#define NE_RELEASE_BUILD_HASH_CAP  8u

/* -------------------------------------------------------------------------
 * Release readiness context
 * ---------------------------------------------------------------------- */
typedef struct {
    /* Readiness items */
    NEReleaseItem      items[NE_RELEASE_ITEM_COUNT];
    /* Regression suite results */
    NEReleaseRegrEntry regression[NE_RELEASE_REGR_CAP];
    uint16_t           regression_count;
    /* Known issues */
    NEReleaseKnownIssue known_issues[NE_RELEASE_KNOWN_ISSUES_CAP];
    uint16_t            known_issue_count;
    /* Release metadata */
    NEReleaseMetadata  metadata;
    /* Reproducible build hashes */
    NEReleaseBuildHash build_hashes[NE_RELEASE_BUILD_HASH_CAP];
    uint16_t           build_hash_count;

    int                initialized;
} NEReleaseContext;

/* =========================================================================
 * Public API – initialisation and teardown
 * ===================================================================== */

int  ne_release_init(NEReleaseContext *ctx);
void ne_release_free(NEReleaseContext *ctx);

/* =========================================================================
 * Public API – readiness item management
 * ===================================================================== */

int ne_release_set_status(NEReleaseContext *ctx,
                          uint8_t           item_id,
                          uint8_t           status);

int ne_release_get_status(const NEReleaseContext *ctx,
                          uint8_t                 item_id);

int ne_release_set_notes(NEReleaseContext *ctx,
                         uint8_t           item_id,
                         const char       *notes);

const char *ne_release_get_notes(const NEReleaseContext *ctx,
                                 uint8_t                 item_id);

/* =========================================================================
 * Public API – regression test suite
 * ===================================================================== */

int ne_release_add_regression(NEReleaseContext *ctx,
                              const char       *suite_name,
                              uint16_t          tests_total,
                              uint16_t          tests_passed,
                              uint16_t          tests_failed,
                              uint8_t           result);

uint16_t ne_release_regression_count(const NEReleaseContext *ctx);

const NEReleaseRegrEntry *ne_release_get_regression(
    const NEReleaseContext *ctx, uint16_t index);

int ne_release_regression_all_pass(const NEReleaseContext *ctx);

/* =========================================================================
 * Public API – known issues
 * ===================================================================== */

int ne_release_add_known_issue(NEReleaseContext *ctx,
                               const char       *description,
                               uint8_t           severity);

uint16_t ne_release_known_issue_count(const NEReleaseContext *ctx);

const NEReleaseKnownIssue *ne_release_get_known_issue(
    const NEReleaseContext *ctx, uint16_t index);

/* =========================================================================
 * Public API – release metadata
 * ===================================================================== */

int ne_release_set_version(NEReleaseContext *ctx, const char *version);
int ne_release_set_tag(NEReleaseContext *ctx, const char *tag);
int ne_release_set_date(NEReleaseContext *ctx, const char *date);

const NEReleaseMetadata *ne_release_get_metadata(
    const NEReleaseContext *ctx);

/* =========================================================================
 * Public API – reproducible build verification
 * ===================================================================== */

int ne_release_add_build_hash(NEReleaseContext *ctx,
                              const char       *build_env,
                              uint32_t          hash);

uint16_t ne_release_build_hash_count(const NEReleaseContext *ctx);

int ne_release_verify_reproducible(const NEReleaseContext *ctx);

/* =========================================================================
 * Public API – overall readiness check
 * ===================================================================== */

int ne_release_is_ready(const NEReleaseContext *ctx);

/* =========================================================================
 * Public API – reporting and string helpers
 * ===================================================================== */

const char *ne_release_item_name(uint8_t item_id);
const char *ne_release_status_name(uint8_t status);
const char *ne_release_strerror(int err);

#endif /* NE_RELEASE_H */

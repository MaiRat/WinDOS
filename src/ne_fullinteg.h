/*
 * ne_fullinteg.h - Full integration validation for the WinDOS kernel
 *
 * Implements Step 9 of the WinDOS kernel-replacement roadmap:
 *   - End-to-end boot sequence validation with the custom kernel replacing
 *     the original.
 *   - Full runtime stability validation across all integrated subsystems.
 *   - Regression suite execution covering all prior steps (1–8).
 *   - Documentation: complete test procedure for reproducible verification.
 *   - Documentation: known limitations, unsupported configurations, and
 *     deferred work.
 *   - Documentation: supported configurations and minimum hardware/emulator
 *     requirements.
 *   - Release checklist: build steps, test steps, and sign-off criteria.
 *   - Reproducible build verification (bit-identical output across clean
 *     environments).
 *
 * Toolchain: Open Watcom C compiler (wcc) targeting 16-bit real-mode DOS.
 *   All data structures use fixed-width integer types compatible with the
 *   Watcom large-memory-model (-ml) compilation mode.  String fields use
 *   fixed-size arrays to avoid dynamic allocation in constrained environments.
 *
 * Watcom/DOS 16-bit real-mode notes:
 *   - Use wcc -ml -za99 -wx -d2 to compile this module.
 *   - The log_fp field may point to a DOS file handle wrapper; set to NULL
 *     to suppress all diagnostic output in memory-constrained environments.
 *   - No dynamic memory allocation is performed by this module; all state
 *     is stored in the fixed NEFullIntegTable struct.
 *
 * Reference: Microsoft Windows 3.1 SDK; DOS 5.0+ Programmer's Reference.
 */

#ifndef NE_FULLINTEG_H
#define NE_FULLINTEG_H

#include <stdint.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Validation item identifiers
 *
 * Each constant identifies one of the Step 9 deliverables that must be
 * validated before the full-integration milestone is considered complete.
 * ---------------------------------------------------------------------- */
#define NE_FULLINTEG_ITEM_BOOT_SEQ       0u  /* boot sequence end-to-end    */
#define NE_FULLINTEG_ITEM_RUNTIME_STABLE 1u  /* full runtime stability      */
#define NE_FULLINTEG_ITEM_REGRESSION     2u  /* regression suite (steps 1-8)*/
#define NE_FULLINTEG_ITEM_TEST_PROC      3u  /* test procedure documentation*/
#define NE_FULLINTEG_ITEM_LIMITATIONS    4u  /* limitations documentation   */
#define NE_FULLINTEG_ITEM_CONFIG_REQS    5u  /* config/hw requirements doc  */
#define NE_FULLINTEG_ITEM_REPRO_BUILD    6u  /* reproducible build verified */

#define NE_FULLINTEG_ITEM_COUNT          7u  /* total validation items      */

/* -------------------------------------------------------------------------
 * Validation item status codes
 *
 * Progression order:
 *   PENDING → IN_PROGRESS → PASS
 *
 * A validation item may also be set to FAIL to record an explicit failure
 * that blocks completion of the full-integration milestone.
 * ---------------------------------------------------------------------- */
#define NE_FULLINTEG_STATUS_PENDING     0u  /* work not yet begun           */
#define NE_FULLINTEG_STATUS_IN_PROGRESS 1u  /* validation underway          */
#define NE_FULLINTEG_STATUS_PASS        2u  /* validation complete and pass */
#define NE_FULLINTEG_STATUS_FAIL        3u  /* validation failed            */

#define NE_FULLINTEG_STATUS_COUNT       4u  /* total status values          */

/* -------------------------------------------------------------------------
 * Release checklist item identifiers
 *
 * Three categories of sign-off criteria that must all be satisfied for a
 * production release of the replacement kernel.
 * ---------------------------------------------------------------------- */
#define NE_FULLINTEG_CL_BUILD_STEPS 0u  /* build procedure verified        */
#define NE_FULLINTEG_CL_TEST_STEPS  1u  /* test procedure verified         */
#define NE_FULLINTEG_CL_SIGNOFF     2u  /* sign-off criteria met           */

#define NE_FULLINTEG_CL_COUNT       3u  /* total checklist items           */

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_FULLINTEG_OK             0
#define NE_FULLINTEG_ERR_NULL      -1   /* NULL pointer argument           */
#define NE_FULLINTEG_ERR_BAD_ITEM  -2   /* item ID >= NE_FULLINTEG_ITEM_COUNT   */
#define NE_FULLINTEG_ERR_BAD_STATUS -3  /* status >= NE_FULLINTEG_STATUS_COUNT  */
#define NE_FULLINTEG_ERR_BAD_CL    -4   /* checklist ID >= NE_FULLINTEG_CL_COUNT*/

/* -------------------------------------------------------------------------
 * String field widths
 * ---------------------------------------------------------------------- */
#define NE_FULLINTEG_NOTES_MAX 128u  /* max length of notes string + NUL   */

/* -------------------------------------------------------------------------
 * Per-item validation record
 *
 * Tracks the current status and free-text notes for one Step 9 deliverable.
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t item_id;                      /* NE_FULLINTEG_ITEM_*           */
    uint8_t status;                       /* NE_FULLINTEG_STATUS_*         */
    char    notes[NE_FULLINTEG_NOTES_MAX];/* human-readable notes/details  */
} NEFullIntegItem;

/* -------------------------------------------------------------------------
 * Release checklist record
 *
 * Tracks completion and notes for one release-checklist criterion.
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t cl_id;                        /* NE_FULLINTEG_CL_*             */
    uint8_t done;                         /* non-zero when criterion is met*/
    char    notes[NE_FULLINTEG_NOTES_MAX];/* human-readable notes/details  */
} NEFullIntegCLItem;

/* -------------------------------------------------------------------------
 * Full integration table
 *
 * Holds one NEFullIntegItem per validation item and one NEFullIntegCLItem
 * per release-checklist criterion.  All state is stored inline so the
 * table requires no heap allocation.
 *
 * Initialise with ne_fullinteg_table_init(); release with
 * ne_fullinteg_table_free().  'log_fp' receives diagnostic output; set to
 * NULL to suppress all output.  Defaults to stderr after init.
 * ---------------------------------------------------------------------- */
typedef struct {
    NEFullIntegItem   items[NE_FULLINTEG_ITEM_COUNT];  /* validation items */
    NEFullIntegCLItem checklist[NE_FULLINTEG_CL_COUNT];/* release checklist*/
    FILE             *log_fp;                           /* diagnostic stream*/
} NEFullIntegTable;

/* =========================================================================
 * Public API
 * ===================================================================== */

/*
 * ne_fullinteg_table_init - initialise *tbl with PENDING status for every
 * item, zeroed checklist, and log_fp set to stderr.
 *
 * Must be called before any other ne_fullinteg_* function on *tbl.
 *
 * Returns NE_FULLINTEG_OK or NE_FULLINTEG_ERR_NULL.
 */
int ne_fullinteg_table_init(NEFullIntegTable *tbl);

/*
 * ne_fullinteg_table_free - zero the table and release logical ownership.
 *
 * Does NOT close log_fp; the caller owns that handle.
 * Safe to call on a zeroed or partially-initialised table and on NULL.
 */
void ne_fullinteg_table_free(NEFullIntegTable *tbl);

/*
 * ne_fullinteg_set_status - update the validation status for item_id.
 *
 * Logs the status transition to tbl->log_fp when non-NULL.
 *
 * Returns NE_FULLINTEG_OK, NE_FULLINTEG_ERR_NULL, NE_FULLINTEG_ERR_BAD_ITEM,
 *         or NE_FULLINTEG_ERR_BAD_STATUS.
 */
int ne_fullinteg_set_status(NEFullIntegTable *tbl,
                             uint8_t           item_id,
                             uint8_t           new_status);

/*
 * ne_fullinteg_set_notes - store a free-text note for item_id.
 *
 * Strings longer than NE_FULLINTEG_NOTES_MAX-1 bytes are silently
 * truncated.  Overwrites any previously stored text.
 *
 * Returns NE_FULLINTEG_OK, NE_FULLINTEG_ERR_NULL, or
 *         NE_FULLINTEG_ERR_BAD_ITEM.
 */
int ne_fullinteg_set_notes(NEFullIntegTable *tbl,
                            uint8_t           item_id,
                            const char       *notes);

/*
 * ne_fullinteg_checklist_set - mark a release-checklist item done or not.
 *
 * 'done' non-zero marks the criterion as satisfied; zero clears it.
 * Logs the change to tbl->log_fp when non-NULL.
 *
 * Returns NE_FULLINTEG_OK, NE_FULLINTEG_ERR_NULL, or
 *         NE_FULLINTEG_ERR_BAD_CL.
 */
int ne_fullinteg_checklist_set(NEFullIntegTable *tbl,
                                uint8_t           cl_id,
                                uint8_t           done);

/*
 * ne_fullinteg_checklist_set_notes - store a free-text note for cl_id.
 *
 * Strings longer than NE_FULLINTEG_NOTES_MAX-1 bytes are silently
 * truncated.  Overwrites any previously stored text.
 *
 * Returns NE_FULLINTEG_OK, NE_FULLINTEG_ERR_NULL, or
 *         NE_FULLINTEG_ERR_BAD_CL.
 */
int ne_fullinteg_checklist_set_notes(NEFullIntegTable *tbl,
                                      uint8_t           cl_id,
                                      const char       *notes);

/*
 * ne_fullinteg_is_complete - test whether the full-integration milestone is
 * complete.
 *
 * Returns non-zero (true) if and only if:
 *   - every validation item has status NE_FULLINTEG_STATUS_PASS, and
 *   - every release-checklist criterion has done != 0.
 *
 * Returns 0 (false) for any other combination, and also when tbl is NULL.
 */
int ne_fullinteg_is_complete(const NEFullIntegTable *tbl);

/*
 * ne_fullinteg_report - write a human-readable full-integration status
 * report to tbl->log_fp.
 *
 * Does nothing if tbl is NULL or tbl->log_fp is NULL.
 */
void ne_fullinteg_report(const NEFullIntegTable *tbl);

/*
 * ne_fullinteg_item_name - return a human-readable name for item_id.
 *
 * Returns a pointer to a static string; never NULL.
 * Returns "UNKNOWN" for unrecognised IDs.
 */
const char *ne_fullinteg_item_name(uint8_t item_id);

/*
 * ne_fullinteg_status_name - return a human-readable name for a status value.
 *
 * Returns a pointer to a static string; never NULL.
 * Returns "UNKNOWN" for unrecognised status values.
 */
const char *ne_fullinteg_status_name(uint8_t status);

/*
 * ne_fullinteg_cl_name - return a human-readable name for a checklist item.
 *
 * Returns a pointer to a static string; never NULL.
 * Returns "UNKNOWN" for unrecognised IDs.
 */
const char *ne_fullinteg_cl_name(uint8_t cl_id);

/*
 * ne_fullinteg_strerror - return a static string describing error code err.
 */
const char *ne_fullinteg_strerror(int err);

#endif /* NE_FULLINTEG_H */

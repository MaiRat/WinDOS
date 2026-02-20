/*
 * ne_integrate.h - Windows 3.1 subsystem integration management
 *
 * Implements Step 8 of the WinDOS kernel-replacement roadmap:
 *   - Identify the minimal set of kernel services needed by the
 *     Windows 3.1 GUI layer.
 *   - Track integration status per subsystem (KERNEL, USER, GDI,
 *     and device drivers: keyboard, timer, display) incrementally.
 *   - Compatibility gate: enforce that all prerequisite subsystems
 *     reach COMPAT_TESTED before a dependent subsystem may advance.
 *   - Track regressions per subsystem and document fallback/bypass paths.
 *   - Document integration status, known gaps, and workarounds per subsystem.
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
 *     is stored in the fixed NEIntegTable struct.
 *
 * Reference: Microsoft Windows 3.1 SDK – KERNEL.EXE, USER.EXE, GDI.EXE
 *            module interface specifications.
 */

#ifndef NE_INTEGRATE_H
#define NE_INTEGRATE_H

#include <stdint.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Subsystem identifiers
 *
 * Each constant identifies one of the Windows 3.1 subsystems tracked by
 * the integration table.  KERNEL.EXE, USER.EXE, and GDI.EXE are the three
 * core system DLLs; the remaining entries are the device drivers whose
 * interfaces must be validated before the GUI layer can operate.
 *
 * Dependency ordering (a subsystem may not advance past NOT_STARTED until
 * all listed prerequisites reach at least COMPAT_TESTED):
 *   KERNEL       – no prerequisites (foundation layer)
 *   DRV_KEYBOARD – requires KERNEL
 *   DRV_TIMER    – requires KERNEL
 *   DRV_DISPLAY  – requires KERNEL
 *   GDI          – requires KERNEL and DRV_DISPLAY
 *   USER         – requires KERNEL and GDI
 * ---------------------------------------------------------------------- */
#define NE_INTEG_SUBSYS_KERNEL        0u   /* KERNEL.EXE – base kernel svc  */
#define NE_INTEG_SUBSYS_USER          1u   /* USER.EXE   – window manager   */
#define NE_INTEG_SUBSYS_GDI           2u   /* GDI.EXE    – graphics layer   */
#define NE_INTEG_SUBSYS_DRV_KEYBOARD  3u   /* KEYBOARD.DRV                  */
#define NE_INTEG_SUBSYS_DRV_TIMER     4u   /* TIMER.DRV                     */
#define NE_INTEG_SUBSYS_DRV_DISPLAY   5u   /* DISPLAY.DRV                   */

#define NE_INTEG_SUBSYS_COUNT         6u   /* total number of tracked subsys */

/* -------------------------------------------------------------------------
 * Integration status codes (per-subsystem)
 *
 * Progression order:
 *   NOT_STARTED → IN_PROGRESS → COMPAT_TESTED → COMPLETE
 *
 * The compatibility gate (ne_integ_gate_check) enforces that all required
 * prerequisites reach at least COMPAT_TESTED before a dependent subsystem
 * may advance to IN_PROGRESS or beyond.
 * ---------------------------------------------------------------------- */
#define NE_INTEG_STATUS_NOT_STARTED   0u  /* work not yet begun              */
#define NE_INTEG_STATUS_IN_PROGRESS   1u  /* integration work underway       */
#define NE_INTEG_STATUS_COMPAT_TESTED 2u  /* compat tests written & passing  */
#define NE_INTEG_STATUS_COMPLETE      3u  /* fully integrated                */

#define NE_INTEG_STATUS_COUNT         4u  /* total number of status values   */

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_INTEG_OK              0
#define NE_INTEG_ERR_NULL       -1   /* NULL pointer argument               */
#define NE_INTEG_ERR_BAD_SUBSYS -2   /* subsystem ID >= NE_INTEG_SUBSYS_COUNT */
#define NE_INTEG_ERR_BAD_STATUS -3   /* status value >= NE_INTEG_STATUS_COUNT */
#define NE_INTEG_ERR_GATE       -4   /* prerequisite not yet COMPAT_TESTED  */
#define NE_INTEG_ERR_REGRESSION -5   /* regression counter would overflow   */

/* -------------------------------------------------------------------------
 * String field widths
 *
 * Fixed-size string buffers make the struct suitable for static allocation
 * in a 16-bit DOS environment where the heap may be constrained.
 * ---------------------------------------------------------------------- */
#define NE_INTEG_DESC_MAX  128u  /* max length of gap/workaround text + NUL */

/* -------------------------------------------------------------------------
 * Per-subsystem integration record
 *
 * Tracks the current integration status, regression count, fallback state,
 * and descriptive documentation fields for one Windows 3.1 subsystem.
 * ---------------------------------------------------------------------- */
typedef struct {
    uint8_t  subsys_id;                     /* NE_INTEG_SUBSYS_*             */
    uint8_t  status;                        /* NE_INTEG_STATUS_*             */
    uint16_t regression_count;              /* regressions logged so far     */
    uint8_t  fallback_active;               /* non-zero when fallback in use */
    char     gap[NE_INTEG_DESC_MAX];        /* known gap description         */
    char     workaround[NE_INTEG_DESC_MAX]; /* workaround/fallback path desc */
} NEIntegEntry;

/* -------------------------------------------------------------------------
 * Integration table
 *
 * Holds one NEIntegEntry per tracked subsystem.  All state is stored
 * inline so the table requires no heap allocation.
 *
 * Initialise with ne_integ_table_init(); release with ne_integ_table_free().
 * 'log_fp' receives diagnostic output; set to NULL to suppress all output.
 * Defaults to stderr after ne_integ_table_init().
 * ---------------------------------------------------------------------- */
typedef struct {
    NEIntegEntry entries[NE_INTEG_SUBSYS_COUNT]; /* one entry per subsystem */
    FILE        *log_fp;                          /* diagnostic output stream */
} NEIntegTable;

/* =========================================================================
 * Public API
 * ===================================================================== */

/*
 * ne_integ_table_init - initialise *tbl with NOT_STARTED status for every
 * subsystem and set log_fp to stderr.
 *
 * Must be called before any other ne_integ_* function on *tbl.
 *
 * Returns NE_INTEG_OK or NE_INTEG_ERR_NULL.
 */
int ne_integ_table_init(NEIntegTable *tbl);

/*
 * ne_integ_table_free - zero the table and release logical ownership.
 *
 * Does NOT close log_fp; the caller owns that handle.
 * Safe to call on a zeroed or partially-initialised table and on NULL.
 */
void ne_integ_table_free(NEIntegTable *tbl);

/*
 * ne_integ_set_status - update the integration status for subsys_id.
 *
 * Enforces the compatibility gate: to advance any subsystem beyond
 * NOT_STARTED all of its prerequisites (as defined by the internal
 * dependency table) must be at least COMPAT_TESTED.
 *
 * Logs the status transition to tbl->log_fp when non-NULL.
 *
 * Returns NE_INTEG_OK, NE_INTEG_ERR_NULL, NE_INTEG_ERR_BAD_SUBSYS,
 *         NE_INTEG_ERR_BAD_STATUS, or NE_INTEG_ERR_GATE.
 */
int ne_integ_set_status(NEIntegTable *tbl,
                        uint8_t       subsys_id,
                        uint8_t       new_status);

/*
 * ne_integ_gate_check - verify prerequisites for a subsystem transition.
 *
 * Returns NE_INTEG_OK if subsys_id may advance to new_status, or
 * NE_INTEG_ERR_GATE if one or more prerequisites are not yet COMPAT_TESTED.
 *
 * Does not modify any table state; safe to call speculatively.
 *
 * Returns NE_INTEG_ERR_NULL, NE_INTEG_ERR_BAD_SUBSYS, or
 * NE_INTEG_ERR_BAD_STATUS for invalid arguments.
 */
int ne_integ_gate_check(const NEIntegTable *tbl,
                        uint8_t             subsys_id,
                        uint8_t             new_status);

/*
 * ne_integ_log_regression - record a regression for subsys_id.
 *
 * Increments the per-subsystem regression counter and logs a message to
 * tbl->log_fp.  'desc' is a short human-readable description of the
 * regression; may be NULL.
 *
 * Returns NE_INTEG_OK, NE_INTEG_ERR_NULL, NE_INTEG_ERR_BAD_SUBSYS, or
 * NE_INTEG_ERR_REGRESSION (counter would wrap from UINT16_MAX).
 */
int ne_integ_log_regression(NEIntegTable *tbl,
                             uint8_t       subsys_id,
                             const char   *desc);

/*
 * ne_integ_set_gap - document a known integration gap for subsys_id.
 *
 * 'gap' is a short human-readable description of missing or incomplete
 * functionality.  Overwrites any previously stored text.  Strings longer
 * than NE_INTEG_DESC_MAX-1 bytes are silently truncated.
 *
 * Returns NE_INTEG_OK, NE_INTEG_ERR_NULL, or NE_INTEG_ERR_BAD_SUBSYS.
 */
int ne_integ_set_gap(NEIntegTable *tbl,
                     uint8_t       subsys_id,
                     const char   *gap);

/*
 * ne_integ_set_workaround - document a workaround or fallback path for
 * subsys_id.
 *
 * 'workaround' is a short human-readable description of the bypass or
 * mitigation path.  Overwrites any previously stored text.  Truncated to
 * NE_INTEG_DESC_MAX-1 if longer.
 *
 * Returns NE_INTEG_OK, NE_INTEG_ERR_NULL, or NE_INTEG_ERR_BAD_SUBSYS.
 */
int ne_integ_set_workaround(NEIntegTable *tbl,
                             uint8_t       subsys_id,
                             const char   *workaround);

/*
 * ne_integ_set_fallback - enable or disable the fallback path for subsys_id.
 *
 * 'active' non-zero enables the fallback bypass; zero disables it.
 *
 * Returns NE_INTEG_OK, NE_INTEG_ERR_NULL, or NE_INTEG_ERR_BAD_SUBSYS.
 */
int ne_integ_set_fallback(NEIntegTable *tbl,
                          uint8_t       subsys_id,
                          uint8_t       active);

/*
 * ne_integ_report - write a human-readable integration status report for
 * all subsystems to tbl->log_fp.
 *
 * Does nothing if tbl is NULL or tbl->log_fp is NULL.
 */
void ne_integ_report(const NEIntegTable *tbl);

/*
 * ne_integ_subsystem_name - return a human-readable name for subsys_id.
 *
 * Returns a pointer to a static string; never NULL.
 * Returns "UNKNOWN" for unrecognised IDs.
 */
const char *ne_integ_subsystem_name(uint8_t subsys_id);

/*
 * ne_integ_status_name - return a human-readable name for a status value.
 *
 * Returns a pointer to a static string; never NULL.
 * Returns "UNKNOWN" for unrecognised status values.
 */
const char *ne_integ_status_name(uint8_t status);

/*
 * ne_integ_strerror - return a static string describing error code err.
 */
const char *ne_integ_strerror(int err);

#endif /* NE_INTEGRATE_H */

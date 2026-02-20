/*
 * ne_compat.h - Phase 6 Compatibility Testing and Hardening
 *
 * Implements the compatibility testing and hardening capabilities
 * required to validate the WinDOS replacement kernel against real
 * Windows 3.1 binaries and applications:
 *
 *   1. System DLL validation: load and validate KERNEL.EXE, USER.EXE,
 *      and GDI.EXE from a stock Windows 3.1 installation, verifying
 *      module loading, relocation, and inter-module import resolution.
 *
 *   2. Application compatibility testing: exercise a stock Windows 3.1
 *      application (e.g. Notepad, Calculator, Write) through the loader
 *      pipeline.
 *
 *   3. Memory profiling: track allocation counts, byte totals, and
 *      detect leaks or excessive consumption under sustained workloads.
 *
 *   4. Scheduler stress testing: run multiple concurrent cooperative
 *      tasks and verify scheduler correctness under load.
 *
 *   5. Known limitations documentation: maintain a queryable list of
 *      unsupported APIs, deferred functionality, and known issues.
 *
 *   6. Compatibility matrix: record per-application, per-subsystem
 *      pass/fail/partial status for reporting.
 *
 * Reference: Microsoft Windows 3.1 SDK;
 *            Microsoft "New Executable" format specification.
 */

#ifndef NE_COMPAT_H
#define NE_COMPAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_COMPAT_OK                0
#define NE_COMPAT_ERR_NULL         -1   /* NULL pointer argument             */
#define NE_COMPAT_ERR_ALLOC        -2   /* memory allocation failure         */
#define NE_COMPAT_ERR_FULL         -3   /* table at capacity                 */
#define NE_COMPAT_ERR_NOT_FOUND    -4   /* item not found                    */
#define NE_COMPAT_ERR_BAD_DATA     -5   /* invalid or malformed data         */
#define NE_COMPAT_ERR_VALIDATION   -6   /* validation check failed           */
#define NE_COMPAT_ERR_INIT         -7   /* context not initialised           */

/* -------------------------------------------------------------------------
 * Configuration constants
 * ---------------------------------------------------------------------- */
#define NE_COMPAT_NAME_MAX          64u  /* max name length incl. NUL        */
#define NE_COMPAT_MATRIX_CAP        32u  /* max entries in compat matrix     */
#define NE_COMPAT_LIMITATION_CAP    64u  /* max known-limitation entries     */
#define NE_COMPAT_DLL_CAP            8u  /* max system DLLs to validate      */
#define NE_COMPAT_STRESS_MAX_TASKS  16u  /* max tasks for stress testing     */

/* -------------------------------------------------------------------------
 * DLL validation status
 * ---------------------------------------------------------------------- */
#define NE_COMPAT_DLL_NOT_TESTED   0u
#define NE_COMPAT_DLL_LOAD_OK      1u
#define NE_COMPAT_DLL_RELOC_OK     2u
#define NE_COMPAT_DLL_IMPORT_OK    4u
#define NE_COMPAT_DLL_VALIDATED    7u   /* all three checks passed           */
#define NE_COMPAT_DLL_LOAD_FAIL    8u
#define NE_COMPAT_DLL_RELOC_FAIL  16u
#define NE_COMPAT_DLL_IMPORT_FAIL 32u

/* -------------------------------------------------------------------------
 * Compatibility matrix status codes
 * ---------------------------------------------------------------------- */
#define NE_COMPAT_STATUS_UNKNOWN   0u
#define NE_COMPAT_STATUS_PASS      1u
#define NE_COMPAT_STATUS_PARTIAL   2u
#define NE_COMPAT_STATUS_FAIL      3u
#define NE_COMPAT_STATUS_SKIP      4u

/* -------------------------------------------------------------------------
 * Limitation severity levels
 * ---------------------------------------------------------------------- */
#define NE_COMPAT_SEV_INFO         0u
#define NE_COMPAT_SEV_WARNING      1u
#define NE_COMPAT_SEV_CRITICAL     2u

/* -------------------------------------------------------------------------
 * Subsystem identifiers for the compatibility matrix
 * ---------------------------------------------------------------------- */
#define NE_COMPAT_SUB_KERNEL       0u
#define NE_COMPAT_SUB_USER         1u
#define NE_COMPAT_SUB_GDI          2u
#define NE_COMPAT_SUB_LOADER       3u
#define NE_COMPAT_SUB_SCHEDULER    4u
#define NE_COMPAT_SUB_MEMORY       5u
#define NE_COMPAT_SUB_DRIVER       6u
#define NE_COMPAT_SUB_COUNT        7u

/* -------------------------------------------------------------------------
 * System DLL validation entry
 *
 * Records the validation status for one system DLL (e.g. KERNEL.EXE).
 * ---------------------------------------------------------------------- */
typedef struct {
    char     name[NE_COMPAT_NAME_MAX];     /* DLL file name                  */
    uint16_t status;                        /* bitmask of NE_COMPAT_DLL_*    */
    uint16_t export_count;                  /* number of exports resolved    */
    uint16_t import_count;                  /* number of imports resolved    */
    uint16_t reloc_count;                   /* number of relocations applied */
} NECompatDLLEntry;

/* -------------------------------------------------------------------------
 * Memory profile snapshot
 *
 * Captures allocation statistics at a point in time.
 * ---------------------------------------------------------------------- */
typedef struct {
    uint32_t total_allocs;       /* cumulative allocation count              */
    uint32_t total_frees;        /* cumulative free count                    */
    uint32_t current_blocks;     /* currently live blocks (allocs - frees)   */
    uint32_t total_bytes_alloc;  /* cumulative bytes allocated               */
    uint32_t total_bytes_freed;  /* cumulative bytes freed                   */
    uint32_t current_bytes;      /* currently live bytes                     */
    uint32_t peak_bytes;         /* peak live bytes observed                 */
    uint32_t peak_blocks;        /* peak live block count observed           */
} NEMemProfile;

/* -------------------------------------------------------------------------
 * Scheduler stress result
 *
 * Records results from a scheduler stress test run.
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t tasks_created;      /* number of tasks created                  */
    uint16_t tasks_completed;    /* number of tasks that ran to completion   */
    uint16_t total_yields;       /* total yield calls observed               */
    uint16_t schedule_passes;    /* number of scheduling passes executed     */
    int      all_completed;      /* non-zero if all tasks completed          */
} NESchedStressResult;

/* -------------------------------------------------------------------------
 * Known limitation entry
 * ---------------------------------------------------------------------- */
typedef struct {
    char     api_name[NE_COMPAT_NAME_MAX];    /* API or feature name        */
    char     description[NE_COMPAT_NAME_MAX]; /* brief description          */
    uint8_t  severity;                         /* NE_COMPAT_SEV_*           */
    uint8_t  subsystem;                        /* NE_COMPAT_SUB_*           */
} NECompatLimitation;

/* -------------------------------------------------------------------------
 * Compatibility matrix entry
 *
 * Records compatibility status for one (application, subsystem) pair.
 * ---------------------------------------------------------------------- */
typedef struct {
    char     app_name[NE_COMPAT_NAME_MAX];    /* application name           */
    uint8_t  subsystem_status[NE_COMPAT_SUB_COUNT]; /* per-subsystem status */
    uint8_t  overall_status;                   /* NE_COMPAT_STATUS_*        */
} NECompatMatrixEntry;

/* -------------------------------------------------------------------------
 * Compatibility context
 *
 * Initialise with ne_compat_init(); release with ne_compat_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    /* System DLL validation */
    NECompatDLLEntry   dlls[NE_COMPAT_DLL_CAP];
    uint16_t           dll_count;

    /* Memory profiling */
    NEMemProfile       mem_profile;

    /* Scheduler stress results */
    NESchedStressResult sched_result;

    /* Known limitations */
    NECompatLimitation  limitations[NE_COMPAT_LIMITATION_CAP];
    uint16_t            limitation_count;

    /* Compatibility matrix */
    NECompatMatrixEntry matrix[NE_COMPAT_MATRIX_CAP];
    uint16_t            matrix_count;

    int                 initialized;
} NECompatContext;

/* =========================================================================
 * Public API – initialisation and teardown
 * ===================================================================== */

/*
 * ne_compat_init - initialise a compatibility testing context.
 *
 * Zeroes all internal tables and marks the context as initialised.
 * Returns NE_COMPAT_OK on success.
 */
int ne_compat_init(NECompatContext *ctx);

/*
 * ne_compat_free - release all resources owned by *ctx.
 *
 * Zeroes the context.  Safe to call on NULL.
 */
void ne_compat_free(NECompatContext *ctx);

/* =========================================================================
 * Public API – system DLL validation
 * ===================================================================== */

/*
 * ne_compat_validate_dll - validate a system DLL by name.
 *
 * Simulates loading, relocation, and import resolution for the named
 * DLL.  The NE file image is provided via 'ne_data' / 'ne_size'; if
 * both are NULL/0 the function performs a dry-run validation using
 * internal heuristics (header signature check).
 *
 * Records the result in the context's DLL table.
 *
 * Returns the DLL status bitmask (NE_COMPAT_DLL_*) or a negative
 * NE_COMPAT_ERR_* code on error.
 */
int ne_compat_validate_dll(NECompatContext *ctx,
                           const char      *dll_name,
                           const uint8_t   *ne_data,
                           uint32_t         ne_size);

/*
 * ne_compat_get_dll_status - retrieve the validation entry for a DLL.
 *
 * Returns a pointer to the entry or NULL if not found.
 */
const NECompatDLLEntry *ne_compat_get_dll_status(const NECompatContext *ctx,
                                                  const char           *dll_name);

/* =========================================================================
 * Public API – memory profiling
 * ===================================================================== */

/*
 * ne_compat_mem_profile_reset - reset the memory profile counters.
 */
void ne_compat_mem_profile_reset(NECompatContext *ctx);

/*
 * ne_compat_mem_profile_alloc - record an allocation of 'size' bytes.
 */
void ne_compat_mem_profile_alloc(NECompatContext *ctx, uint32_t size);

/*
 * ne_compat_mem_profile_free - record a deallocation of 'size' bytes.
 */
void ne_compat_mem_profile_free(NECompatContext *ctx, uint32_t size);

/*
 * ne_compat_mem_profile_snapshot - return a copy of the current profile.
 *
 * Returns NE_COMPAT_OK or NE_COMPAT_ERR_NULL.
 */
int ne_compat_mem_profile_snapshot(const NECompatContext *ctx,
                                   NEMemProfile          *out);

/*
 * ne_compat_mem_profile_has_leaks - check whether any allocations are
 * outstanding (current_blocks > 0).
 *
 * Returns 1 if leaks are detected, 0 if clean, or a negative error code.
 */
int ne_compat_mem_profile_has_leaks(const NECompatContext *ctx);

/* =========================================================================
 * Public API – scheduler stress testing
 * ===================================================================== */

/*
 * ne_compat_stress_scheduler - run a stress test with 'num_tasks'
 * concurrent cooperative tasks.
 *
 * Each task increments a shared counter 'iterations_per_task' times,
 * yielding after each increment.  The function verifies that all tasks
 * complete and the counter reaches the expected total.
 *
 * 'num_tasks' must be in the range [1, NE_COMPAT_STRESS_MAX_TASKS].
 *
 * Results are stored in ctx->sched_result.
 *
 * Returns NE_COMPAT_OK if all tasks completed successfully, or a
 * negative NE_COMPAT_ERR_* code on failure.
 */
int ne_compat_stress_scheduler(NECompatContext *ctx,
                               uint16_t         num_tasks,
                               uint16_t         iterations_per_task);

/* =========================================================================
 * Public API – known limitations
 * ===================================================================== */

/*
 * ne_compat_add_limitation - record a known limitation.
 *
 * 'api_name'    : name of the unsupported API or feature.
 * 'description' : brief description of the limitation.
 * 'severity'    : NE_COMPAT_SEV_* constant.
 * 'subsystem'   : NE_COMPAT_SUB_* constant.
 *
 * Returns NE_COMPAT_OK or NE_COMPAT_ERR_FULL.
 */
int ne_compat_add_limitation(NECompatContext *ctx,
                             const char      *api_name,
                             const char      *description,
                             uint8_t          severity,
                             uint8_t          subsystem);

/*
 * ne_compat_get_limitation_count - return the number of recorded
 * limitations.
 */
uint16_t ne_compat_get_limitation_count(const NECompatContext *ctx);

/*
 * ne_compat_get_limitation - return a pointer to limitation at 'index'.
 *
 * Returns NULL if 'index' is out of range.
 */
const NECompatLimitation *ne_compat_get_limitation(
    const NECompatContext *ctx, uint16_t index);

/* =========================================================================
 * Public API – compatibility matrix
 * ===================================================================== */

/*
 * ne_compat_matrix_add - add or update an entry in the compatibility
 * matrix.
 *
 * If 'app_name' already exists, updates its statuses.  Otherwise adds
 * a new entry.
 *
 * 'subsystem_status' is an array of NE_COMPAT_SUB_COUNT status codes.
 * 'overall_status'   is the overall NE_COMPAT_STATUS_* code.
 *
 * Returns NE_COMPAT_OK or NE_COMPAT_ERR_FULL.
 */
int ne_compat_matrix_add(NECompatContext *ctx,
                         const char      *app_name,
                         const uint8_t   *subsystem_status,
                         uint8_t          overall_status);

/*
 * ne_compat_matrix_get - retrieve a matrix entry by application name.
 *
 * Returns a pointer to the entry or NULL if not found.
 */
const NECompatMatrixEntry *ne_compat_matrix_get(
    const NECompatContext *ctx, const char *app_name);

/*
 * ne_compat_matrix_count - return the number of matrix entries.
 */
uint16_t ne_compat_matrix_count(const NECompatContext *ctx);

/* =========================================================================
 * Public API – error string
 * ===================================================================== */

/*
 * ne_compat_strerror - return a static string describing error code 'err'.
 */
const char *ne_compat_strerror(int err);

#endif /* NE_COMPAT_H */

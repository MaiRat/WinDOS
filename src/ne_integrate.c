/*
 * ne_integrate.c - Windows 3.1 subsystem integration management
 *
 * Implements Step 8 of the WinDOS kernel-replacement roadmap.
 *
 * Toolchain: Open Watcom C compiler (wcc) targeting 16-bit real-mode DOS.
 *   Compile with: wcc -ml -za99 -wx -d2 -i=../src ne_integrate.c
 *
 * All state is stored in the caller-supplied NEIntegTable struct; this
 * module performs no dynamic memory allocation.
 */

#include "ne_integrate.h"

#include <string.h>

/* -------------------------------------------------------------------------
 * Internal dependency table
 *
 * For each subsystem, lists the subsystem IDs that must be at least
 * COMPAT_TESTED before the subsystem may advance beyond NOT_STARTED.
 * Each row is terminated by NE_INTEG_SUBSYS_COUNT (used as a sentinel).
 *
 * Windows 3.1 layering:
 *   KERNEL       – foundation; no prerequisites
 *   DRV_KEYBOARD – requires KERNEL
 *   DRV_TIMER    – requires KERNEL
 *   DRV_DISPLAY  – requires KERNEL
 *   GDI          – requires KERNEL and DRV_DISPLAY
 *   USER         – requires KERNEL and GDI
 * ---------------------------------------------------------------------- */

#define DEP_END  NE_INTEG_SUBSYS_COUNT   /* sentinel marking end of dep list */
#define MAX_DEPS 3u                       /* maximum dependencies per subsys  */

static const uint8_t k_deps[NE_INTEG_SUBSYS_COUNT][MAX_DEPS] = {
    /* NE_INTEG_SUBSYS_KERNEL       */ { DEP_END, DEP_END, DEP_END },
    /* NE_INTEG_SUBSYS_USER         */ { NE_INTEG_SUBSYS_KERNEL,
                                         NE_INTEG_SUBSYS_GDI,
                                         DEP_END },
    /* NE_INTEG_SUBSYS_GDI          */ { NE_INTEG_SUBSYS_KERNEL,
                                         NE_INTEG_SUBSYS_DRV_DISPLAY,
                                         DEP_END },
    /* NE_INTEG_SUBSYS_DRV_KEYBOARD */ { NE_INTEG_SUBSYS_KERNEL,
                                         DEP_END,
                                         DEP_END },
    /* NE_INTEG_SUBSYS_DRV_TIMER    */ { NE_INTEG_SUBSYS_KERNEL,
                                         DEP_END,
                                         DEP_END },
    /* NE_INTEG_SUBSYS_DRV_DISPLAY  */ { NE_INTEG_SUBSYS_KERNEL,
                                         DEP_END,
                                         DEP_END },
};

/* =========================================================================
 * ne_integ_subsystem_name
 * ===================================================================== */

const char *ne_integ_subsystem_name(uint8_t subsys_id)
{
    switch (subsys_id) {
    case NE_INTEG_SUBSYS_KERNEL:        return "KERNEL";
    case NE_INTEG_SUBSYS_USER:          return "USER";
    case NE_INTEG_SUBSYS_GDI:           return "GDI";
    case NE_INTEG_SUBSYS_DRV_KEYBOARD:  return "DRV_KEYBOARD";
    case NE_INTEG_SUBSYS_DRV_TIMER:     return "DRV_TIMER";
    case NE_INTEG_SUBSYS_DRV_DISPLAY:   return "DRV_DISPLAY";
    default:                             return "UNKNOWN";
    }
}

/* =========================================================================
 * ne_integ_status_name
 * ===================================================================== */

const char *ne_integ_status_name(uint8_t status)
{
    switch (status) {
    case NE_INTEG_STATUS_NOT_STARTED:   return "NOT_STARTED";
    case NE_INTEG_STATUS_IN_PROGRESS:   return "IN_PROGRESS";
    case NE_INTEG_STATUS_COMPAT_TESTED: return "COMPAT_TESTED";
    case NE_INTEG_STATUS_COMPLETE:      return "COMPLETE";
    default:                             return "UNKNOWN";
    }
}

/* =========================================================================
 * ne_integ_strerror
 * ===================================================================== */

const char *ne_integ_strerror(int err)
{
    switch (err) {
    case NE_INTEG_OK:              return "success";
    case NE_INTEG_ERR_NULL:        return "NULL pointer argument";
    case NE_INTEG_ERR_BAD_SUBSYS:  return "subsystem ID out of range";
    case NE_INTEG_ERR_BAD_STATUS:  return "status value out of range";
    case NE_INTEG_ERR_GATE:        return "prerequisite subsystem not yet COMPAT_TESTED";
    case NE_INTEG_ERR_REGRESSION:  return "regression counter overflow";
    default:                        return "unknown error";
    }
}

/* =========================================================================
 * ne_integ_table_init / ne_integ_table_free
 * ===================================================================== */

int ne_integ_table_init(NEIntegTable *tbl)
{
    uint8_t i;

    if (!tbl)
        return NE_INTEG_ERR_NULL;

    memset(tbl, 0, sizeof(*tbl));

    for (i = 0; i < NE_INTEG_SUBSYS_COUNT; i++) {
        tbl->entries[i].subsys_id        = i;
        tbl->entries[i].status           = NE_INTEG_STATUS_NOT_STARTED;
        tbl->entries[i].regression_count = 0u;
        tbl->entries[i].fallback_active  = 0u;
    }

    tbl->log_fp = stderr;

    return NE_INTEG_OK;
}

void ne_integ_table_free(NEIntegTable *tbl)
{
    if (!tbl)
        return;

    /*
     * No heap memory to release; zero the struct so that stale pointers
     * (e.g. log_fp) are not used after free.
     */
    memset(tbl, 0, sizeof(*tbl));
}

/* =========================================================================
 * ne_integ_gate_check
 * ===================================================================== */

int ne_integ_gate_check(const NEIntegTable *tbl,
                        uint8_t             subsys_id,
                        uint8_t             new_status)
{
    uint8_t i;
    uint8_t dep;

    if (!tbl)
        return NE_INTEG_ERR_NULL;
    if (subsys_id >= NE_INTEG_SUBSYS_COUNT)
        return NE_INTEG_ERR_BAD_SUBSYS;
    if (new_status >= NE_INTEG_STATUS_COUNT)
        return NE_INTEG_ERR_BAD_STATUS;

    /*
     * Gate only applies when advancing beyond NOT_STARTED.
     * Resetting a subsystem to NOT_STARTED is always permitted.
     */
    if (new_status == NE_INTEG_STATUS_NOT_STARTED)
        return NE_INTEG_OK;

    /* Check every prerequisite for this subsystem. */
    for (i = 0; i < MAX_DEPS; i++) {
        dep = k_deps[subsys_id][i];
        if (dep == DEP_END)
            break;

        if (tbl->entries[dep].status < NE_INTEG_STATUS_COMPAT_TESTED)
            return NE_INTEG_ERR_GATE;
    }

    return NE_INTEG_OK;
}

/* =========================================================================
 * ne_integ_set_status
 * ===================================================================== */

int ne_integ_set_status(NEIntegTable *tbl,
                        uint8_t       subsys_id,
                        uint8_t       new_status)
{
    int rc;

    if (!tbl)
        return NE_INTEG_ERR_NULL;
    if (subsys_id >= NE_INTEG_SUBSYS_COUNT)
        return NE_INTEG_ERR_BAD_SUBSYS;
    if (new_status >= NE_INTEG_STATUS_COUNT)
        return NE_INTEG_ERR_BAD_STATUS;

    rc = ne_integ_gate_check(tbl, subsys_id, new_status);
    if (rc != NE_INTEG_OK)
        return rc;

    if (tbl->log_fp) {
        fprintf(tbl->log_fp,
                "[INTEG] %s: %s -> %s\n",
                ne_integ_subsystem_name(subsys_id),
                ne_integ_status_name(tbl->entries[subsys_id].status),
                ne_integ_status_name(new_status));
        fflush(tbl->log_fp);
    }

    tbl->entries[subsys_id].status = new_status;

    return NE_INTEG_OK;
}

/* =========================================================================
 * ne_integ_log_regression
 * ===================================================================== */

int ne_integ_log_regression(NEIntegTable *tbl,
                             uint8_t       subsys_id,
                             const char   *desc)
{
    if (!tbl)
        return NE_INTEG_ERR_NULL;
    if (subsys_id >= NE_INTEG_SUBSYS_COUNT)
        return NE_INTEG_ERR_BAD_SUBSYS;

    if (tbl->entries[subsys_id].regression_count == (uint16_t)0xFFFFu)
        return NE_INTEG_ERR_REGRESSION;

    tbl->entries[subsys_id].regression_count++;

    if (tbl->log_fp) {
        fprintf(tbl->log_fp,
                "[INTEG] REGRESSION %s #%u: %s\n",
                ne_integ_subsystem_name(subsys_id),
                (unsigned)tbl->entries[subsys_id].regression_count,
                desc ? desc : "(no description)");
        fflush(tbl->log_fp);
    }

    return NE_INTEG_OK;
}

/* =========================================================================
 * ne_integ_set_gap
 * ===================================================================== */

int ne_integ_set_gap(NEIntegTable *tbl,
                     uint8_t       subsys_id,
                     const char   *gap)
{
    if (!tbl || !gap)
        return NE_INTEG_ERR_NULL;
    if (subsys_id >= NE_INTEG_SUBSYS_COUNT)
        return NE_INTEG_ERR_BAD_SUBSYS;

    strncpy(tbl->entries[subsys_id].gap, gap, NE_INTEG_DESC_MAX - 1u);
    tbl->entries[subsys_id].gap[NE_INTEG_DESC_MAX - 1u] = '\0';

    return NE_INTEG_OK;
}

/* =========================================================================
 * ne_integ_set_workaround
 * ===================================================================== */

int ne_integ_set_workaround(NEIntegTable *tbl,
                             uint8_t       subsys_id,
                             const char   *workaround)
{
    if (!tbl || !workaround)
        return NE_INTEG_ERR_NULL;
    if (subsys_id >= NE_INTEG_SUBSYS_COUNT)
        return NE_INTEG_ERR_BAD_SUBSYS;

    strncpy(tbl->entries[subsys_id].workaround,
            workaround,
            NE_INTEG_DESC_MAX - 1u);
    tbl->entries[subsys_id].workaround[NE_INTEG_DESC_MAX - 1u] = '\0';

    return NE_INTEG_OK;
}

/* =========================================================================
 * ne_integ_set_fallback
 * ===================================================================== */

int ne_integ_set_fallback(NEIntegTable *tbl,
                          uint8_t       subsys_id,
                          uint8_t       active)
{
    if (!tbl)
        return NE_INTEG_ERR_NULL;
    if (subsys_id >= NE_INTEG_SUBSYS_COUNT)
        return NE_INTEG_ERR_BAD_SUBSYS;

    tbl->entries[subsys_id].fallback_active = active ? 1u : 0u;

    return NE_INTEG_OK;
}

/* =========================================================================
 * ne_integ_report
 * ===================================================================== */

void ne_integ_report(const NEIntegTable *tbl)
{
    uint8_t i;

    if (!tbl || !tbl->log_fp)
        return;

    fprintf(tbl->log_fp,
            "=== WinDOS Integration Status Report (Step 8) ===\n");

    for (i = 0; i < NE_INTEG_SUBSYS_COUNT; i++) {
        const NEIntegEntry *e = &tbl->entries[i];

        fprintf(tbl->log_fp,
                "  %-14s  status=%-14s  regressions=%u  fallback=%s\n",
                ne_integ_subsystem_name(e->subsys_id),
                ne_integ_status_name(e->status),
                (unsigned)e->regression_count,
                e->fallback_active ? "yes" : "no");

        if (e->gap[0] != '\0')
            fprintf(tbl->log_fp,
                    "    gap:         %s\n", e->gap);

        if (e->workaround[0] != '\0')
            fprintf(tbl->log_fp,
                    "    workaround:  %s\n", e->workaround);
    }

    fprintf(tbl->log_fp,
            "=================================================\n");
    fflush(tbl->log_fp);
}

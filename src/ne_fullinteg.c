/*
 * ne_fullinteg.c - Full integration validation for the WinDOS kernel
 *
 * Implements Step 9 of the WinDOS kernel-replacement roadmap.
 *
 * Toolchain: Open Watcom C compiler (wcc) targeting 16-bit real-mode DOS.
 *   Compile with: wcc -ml -za99 -wx -d2 -i=../src ne_fullinteg.c
 *
 * All state is stored in the caller-supplied NEFullIntegTable struct; this
 * module performs no dynamic memory allocation.
 */

#include "ne_fullinteg.h"

#include <string.h>

/* =========================================================================
 * ne_fullinteg_item_name
 * ===================================================================== */

const char *ne_fullinteg_item_name(uint8_t item_id)
{
    switch (item_id) {
    case NE_FULLINTEG_ITEM_BOOT_SEQ:       return "BOOT_SEQ";
    case NE_FULLINTEG_ITEM_RUNTIME_STABLE: return "RUNTIME_STABLE";
    case NE_FULLINTEG_ITEM_REGRESSION:     return "REGRESSION";
    case NE_FULLINTEG_ITEM_TEST_PROC:      return "TEST_PROC";
    case NE_FULLINTEG_ITEM_LIMITATIONS:    return "LIMITATIONS";
    case NE_FULLINTEG_ITEM_CONFIG_REQS:    return "CONFIG_REQS";
    case NE_FULLINTEG_ITEM_REPRO_BUILD:    return "REPRO_BUILD";
    default:                                return "UNKNOWN";
    }
}

/* =========================================================================
 * ne_fullinteg_status_name
 * ===================================================================== */

const char *ne_fullinteg_status_name(uint8_t status)
{
    switch (status) {
    case NE_FULLINTEG_STATUS_PENDING:     return "PENDING";
    case NE_FULLINTEG_STATUS_IN_PROGRESS: return "IN_PROGRESS";
    case NE_FULLINTEG_STATUS_PASS:        return "PASS";
    case NE_FULLINTEG_STATUS_FAIL:        return "FAIL";
    default:                               return "UNKNOWN";
    }
}

/* =========================================================================
 * ne_fullinteg_cl_name
 * ===================================================================== */

const char *ne_fullinteg_cl_name(uint8_t cl_id)
{
    switch (cl_id) {
    case NE_FULLINTEG_CL_BUILD_STEPS: return "BUILD_STEPS";
    case NE_FULLINTEG_CL_TEST_STEPS:  return "TEST_STEPS";
    case NE_FULLINTEG_CL_SIGNOFF:     return "SIGNOFF";
    default:                           return "UNKNOWN";
    }
}

/* =========================================================================
 * ne_fullinteg_strerror
 * ===================================================================== */

const char *ne_fullinteg_strerror(int err)
{
    switch (err) {
    case NE_FULLINTEG_OK:             return "success";
    case NE_FULLINTEG_ERR_NULL:       return "NULL pointer argument";
    case NE_FULLINTEG_ERR_BAD_ITEM:   return "item ID out of range";
    case NE_FULLINTEG_ERR_BAD_STATUS: return "status value out of range";
    case NE_FULLINTEG_ERR_BAD_CL:     return "checklist ID out of range";
    default:                           return "unknown error";
    }
}

/* =========================================================================
 * ne_fullinteg_table_init / ne_fullinteg_table_free
 * ===================================================================== */

int ne_fullinteg_table_init(NEFullIntegTable *tbl)
{
    uint8_t i;

    if (!tbl)
        return NE_FULLINTEG_ERR_NULL;

    memset(tbl, 0, sizeof(*tbl));

    for (i = 0; i < NE_FULLINTEG_ITEM_COUNT; i++) {
        tbl->items[i].item_id = i;
        tbl->items[i].status  = NE_FULLINTEG_STATUS_PENDING;
    }

    for (i = 0; i < NE_FULLINTEG_CL_COUNT; i++) {
        tbl->checklist[i].cl_id = i;
        tbl->checklist[i].done  = 0u;
    }

    tbl->log_fp = stderr;

    return NE_FULLINTEG_OK;
}

void ne_fullinteg_table_free(NEFullIntegTable *tbl)
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
 * ne_fullinteg_set_status
 * ===================================================================== */

int ne_fullinteg_set_status(NEFullIntegTable *tbl,
                             uint8_t           item_id,
                             uint8_t           new_status)
{
    if (!tbl)
        return NE_FULLINTEG_ERR_NULL;
    if (item_id >= NE_FULLINTEG_ITEM_COUNT)
        return NE_FULLINTEG_ERR_BAD_ITEM;
    if (new_status >= NE_FULLINTEG_STATUS_COUNT)
        return NE_FULLINTEG_ERR_BAD_STATUS;

    if (tbl->log_fp) {
        fprintf(tbl->log_fp,
                "[FULLINTEG] %s: %s -> %s\n",
                ne_fullinteg_item_name(item_id),
                ne_fullinteg_status_name(tbl->items[item_id].status),
                ne_fullinteg_status_name(new_status));
        fflush(tbl->log_fp);
    }

    tbl->items[item_id].status = new_status;

    return NE_FULLINTEG_OK;
}

/* =========================================================================
 * ne_fullinteg_set_notes
 * ===================================================================== */

int ne_fullinteg_set_notes(NEFullIntegTable *tbl,
                            uint8_t           item_id,
                            const char       *notes)
{
    if (!tbl || !notes)
        return NE_FULLINTEG_ERR_NULL;
    if (item_id >= NE_FULLINTEG_ITEM_COUNT)
        return NE_FULLINTEG_ERR_BAD_ITEM;

    strncpy(tbl->items[item_id].notes, notes, NE_FULLINTEG_NOTES_MAX - 1u);
    tbl->items[item_id].notes[NE_FULLINTEG_NOTES_MAX - 1u] = '\0';

    return NE_FULLINTEG_OK;
}

/* =========================================================================
 * ne_fullinteg_checklist_set
 * ===================================================================== */

int ne_fullinteg_checklist_set(NEFullIntegTable *tbl,
                                uint8_t           cl_id,
                                uint8_t           done)
{
    if (!tbl)
        return NE_FULLINTEG_ERR_NULL;
    if (cl_id >= NE_FULLINTEG_CL_COUNT)
        return NE_FULLINTEG_ERR_BAD_CL;

    if (tbl->log_fp) {
        fprintf(tbl->log_fp,
                "[FULLINTEG] checklist %s: %s\n",
                ne_fullinteg_cl_name(cl_id),
                done ? "DONE" : "CLEARED");
        fflush(tbl->log_fp);
    }

    tbl->checklist[cl_id].done = done ? 1u : 0u;

    return NE_FULLINTEG_OK;
}

/* =========================================================================
 * ne_fullinteg_checklist_set_notes
 * ===================================================================== */

int ne_fullinteg_checklist_set_notes(NEFullIntegTable *tbl,
                                      uint8_t           cl_id,
                                      const char       *notes)
{
    if (!tbl || !notes)
        return NE_FULLINTEG_ERR_NULL;
    if (cl_id >= NE_FULLINTEG_CL_COUNT)
        return NE_FULLINTEG_ERR_BAD_CL;

    strncpy(tbl->checklist[cl_id].notes, notes, NE_FULLINTEG_NOTES_MAX - 1u);
    tbl->checklist[cl_id].notes[NE_FULLINTEG_NOTES_MAX - 1u] = '\0';

    return NE_FULLINTEG_OK;
}

/* =========================================================================
 * ne_fullinteg_is_complete
 * ===================================================================== */

int ne_fullinteg_is_complete(const NEFullIntegTable *tbl)
{
    uint8_t i;

    if (!tbl)
        return 0;

    for (i = 0; i < NE_FULLINTEG_ITEM_COUNT; i++) {
        if (tbl->items[i].status != NE_FULLINTEG_STATUS_PASS)
            return 0;
    }

    for (i = 0; i < NE_FULLINTEG_CL_COUNT; i++) {
        if (!tbl->checklist[i].done)
            return 0;
    }

    return 1;
}

/* =========================================================================
 * ne_fullinteg_report
 * ===================================================================== */

void ne_fullinteg_report(const NEFullIntegTable *tbl)
{
    uint8_t i;

    if (!tbl || !tbl->log_fp)
        return;

    fprintf(tbl->log_fp,
            "=== WinDOS Full Integration Status Report (Step 9) ===\n");

    fprintf(tbl->log_fp, "\n  Validation items:\n");
    for (i = 0; i < NE_FULLINTEG_ITEM_COUNT; i++) {
        const NEFullIntegItem *it = &tbl->items[i];
        fprintf(tbl->log_fp,
                "    %-18s  %s\n",
                ne_fullinteg_item_name(it->item_id),
                ne_fullinteg_status_name(it->status));
        if (it->notes[0] != '\0')
            fprintf(tbl->log_fp,
                    "      notes: %s\n", it->notes);
    }

    fprintf(tbl->log_fp, "\n  Release checklist:\n");
    for (i = 0; i < NE_FULLINTEG_CL_COUNT; i++) {
        const NEFullIntegCLItem *cl = &tbl->checklist[i];
        fprintf(tbl->log_fp,
                "    %-14s  %s\n",
                ne_fullinteg_cl_name(cl->cl_id),
                cl->done ? "DONE" : "PENDING");
        if (cl->notes[0] != '\0')
            fprintf(tbl->log_fp,
                    "      notes: %s\n", cl->notes);
    }

    fprintf(tbl->log_fp,
            "\n  Overall: %s\n",
            ne_fullinteg_is_complete(tbl) ? "COMPLETE" : "INCOMPLETE");

    fprintf(tbl->log_fp,
            "======================================================\n");
    fflush(tbl->log_fp);
}

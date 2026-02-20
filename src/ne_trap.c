/*
 * ne_trap.c - CPU exception and trap handler management
 *
 * Implements Step 7 of the WinDOS kernel-replacement roadmap.
 *
 * Host-side build: all functions operate on the NETrapTable in-memory
 * dispatch table.  Actual IVT installation is omitted because it requires
 * hardware access only available in a real 16-bit DOS environment.
 *
 * Watcom/DOS 16-bit real-mode target: each required exception vector needs
 * a corresponding __interrupt stub declared in a separate assembly module
 * (or as a Watcom-specific __interrupt C function).  The stub must:
 *   1. Build an NETrapContext from the CPU register snapshot on the stack.
 *   2. Call ne_trap_dispatch(&g_trap_table, vec, &ctx).
 *   3. Perform IRET to return to the interrupted code.
 * Vector registration uses _dos_setvect() / _dos_getvect() (declared in
 * <dos.h>) to write and restore the IVT entry at 0000:vec*4.
 */

#include "ne_trap.h"

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Vectors considered unrecoverable by the built-in default handler
 *
 * The GP fault, stack fault, double fault, and divide error are treated as
 * fatal by default; all other vectors default to NE_TRAP_RECOVER_SKIP.
 * ---------------------------------------------------------------------- */
static int is_fatal_vec(uint8_t vec)
{
    return (vec == NE_TRAP_VEC_DIVIDE_ERROR  ||
            vec == NE_TRAP_VEC_DOUBLE_FAULT  ||
            vec == NE_TRAP_VEC_STACK_FAULT   ||
            vec == NE_TRAP_VEC_GP_FAULT);
}

/* =========================================================================
 * ne_trap_vec_name
 * ===================================================================== */

const char *ne_trap_vec_name(uint8_t vec)
{
    switch (vec) {
    case NE_TRAP_VEC_DIVIDE_ERROR:   return "DIVIDE_ERROR";
    case NE_TRAP_VEC_SINGLE_STEP:    return "SINGLE_STEP";
    case NE_TRAP_VEC_NMI:            return "NMI";
    case NE_TRAP_VEC_BREAKPOINT:     return "BREAKPOINT";
    case NE_TRAP_VEC_OVERFLOW:       return "OVERFLOW";
    case NE_TRAP_VEC_BOUND:          return "BOUND";
    case NE_TRAP_VEC_INVALID_OPCODE: return "INVALID_OPCODE";
    case NE_TRAP_VEC_NO_FPU:         return "NO_FPU";
    case NE_TRAP_VEC_DOUBLE_FAULT:   return "DOUBLE_FAULT";
    case NE_TRAP_VEC_FPU_SEG:        return "FPU_SEG";
    case NE_TRAP_VEC_STACK_FAULT:    return "STACK_FAULT";
    case NE_TRAP_VEC_GP_FAULT:       return "GP_FAULT";
    default:                         return "UNKNOWN";
    }
}

/* =========================================================================
 * ne_trap_log
 * ===================================================================== */

void ne_trap_log(NETrapTable *tbl, uint8_t vec, const NETrapContext *ctx)
{
    if (!tbl || !tbl->log_fp)
        return;

    fprintf(tbl->log_fp,
            "[TRAP] vec=0x%02X (%s)",
            (unsigned)vec,
            ne_trap_vec_name(vec));

    if (ctx) {
        fprintf(tbl->log_fp,
                " err=0x%04X cs:ip=%04X:%04X ss:sp=%04X:%04X"
                " ax=%04X bx=%04X cx=%04X dx=%04X"
                " si=%04X di=%04X bp=%04X"
                " ds=%04X es=%04X flags=%04X",
                (unsigned)ctx->error_code,
                (unsigned)ctx->cs, (unsigned)ctx->ip,
                (unsigned)ctx->ss, (unsigned)ctx->sp,
                (unsigned)ctx->ax, (unsigned)ctx->bx,
                (unsigned)ctx->cx, (unsigned)ctx->dx,
                (unsigned)ctx->si, (unsigned)ctx->di,
                (unsigned)ctx->bp,
                (unsigned)ctx->ds, (unsigned)ctx->es,
                (unsigned)ctx->flags);
    }

    fputc('\n', tbl->log_fp);
    fflush(tbl->log_fp);
}

/* =========================================================================
 * ne_trap_panic
 * ===================================================================== */

void ne_trap_panic(NETrapTable *tbl, const char *msg, const NETrapContext *ctx)
{
    if (tbl && tbl->log_fp) {
        fprintf(tbl->log_fp,
                "[PANIC] %s\n",
                msg ? msg : "(no message)");
        fflush(tbl->log_fp);
    }

    if (ctx && tbl)
        ne_trap_log(tbl, ctx->fault_vec, ctx);

    if (tbl && tbl->panic_fn) {
        tbl->panic_fn(msg, ctx, tbl->panic_user);
        return; /* custom handler may return (e.g., in tests) */
    }

    /*
     * Built-in unrecoverable path.
     *
     * On the Watcom/DOS 16-bit target replace exit(1) with:
     *   __asm { cli };
     *   __asm { hlt };
     * to halt the CPU cleanly without returning to DOS.
     */
    exit(1);
}

/* =========================================================================
 * ne_trap_table_init / ne_trap_table_free
 * ===================================================================== */

int ne_trap_table_init(NETrapTable *tbl)
{
    uint8_t i;

    if (!tbl)
        return NE_TRAP_ERR_NULL;

    memset(tbl, 0, sizeof(*tbl));

    /* Install NULL handlers for all vectors (NULL means use default path). */
    for (i = 0; i < NE_TRAP_VEC_COUNT; i++) {
        tbl->entries[i].fn   = NULL;
        tbl->entries[i].user = NULL;
    }

    tbl->log_fp    = stderr;
    tbl->panic_fn  = NULL;
    tbl->panic_user = NULL;

    return NE_TRAP_OK;
}

void ne_trap_table_free(NETrapTable *tbl)
{
    if (!tbl)
        return;

    /*
     * On the Watcom/DOS target, restore all modified IVT entries here by
     * calling _dos_setvect() with the previously saved original vectors.
     * This ensures that DOS interrupt handling is not left in a broken
     * state if the trap subsystem is shut down mid-session.
     */
    memset(tbl, 0, sizeof(*tbl));
}

/* =========================================================================
 * ne_trap_install / ne_trap_remove
 * ===================================================================== */

int ne_trap_install(NETrapTable  *tbl,
                    uint8_t       vec,
                    NETrapHandler fn,
                    void         *user)
{
    if (!tbl)
        return NE_TRAP_ERR_NULL;
    if (vec >= NE_TRAP_VEC_COUNT)
        return NE_TRAP_ERR_BAD_VEC;

    tbl->entries[vec].fn   = fn;
    tbl->entries[vec].user = user;

    /*
     * Watcom/DOS 16-bit target:
     *   Save the old vector with _dos_getvect(vec), then call
     *   _dos_setvect(vec, stub) where 'stub' is the __interrupt wrapper
     *   that builds an NETrapContext and calls ne_trap_dispatch().
     */
    return NE_TRAP_OK;
}

int ne_trap_remove(NETrapTable *tbl, uint8_t vec)
{
    if (!tbl)
        return NE_TRAP_ERR_NULL;
    if (vec >= NE_TRAP_VEC_COUNT)
        return NE_TRAP_ERR_BAD_VEC;

    tbl->entries[vec].fn   = NULL;
    tbl->entries[vec].user = NULL;

    /*
     * Watcom/DOS 16-bit target:
     *   Restore the original vector using the pointer saved during
     *   ne_trap_install(): _dos_setvect(vec, saved_vec[vec]).
     */
    return NE_TRAP_OK;
}

/* =========================================================================
 * ne_trap_dispatch
 * ===================================================================== */

int ne_trap_dispatch(NETrapTable *tbl, uint8_t vec, const NETrapContext *ctx)
{
    int rc;

    if (!tbl)
        return NE_TRAP_RECOVER_PANIC;

    if (vec >= NE_TRAP_VEC_COUNT) {
        /* Out-of-range vector: log and treat as fatal. */
        if (tbl->log_fp) {
            fprintf(tbl->log_fp,
                    "[TRAP] vec=0x%02X out of range – treating as panic\n",
                    (unsigned)vec);
            fflush(tbl->log_fp);
        }
        ne_trap_panic(tbl, "exception vector out of range", ctx);
        return NE_TRAP_RECOVER_PANIC;
    }

    if (tbl->entries[vec].fn) {
        /* Custom handler installed – call it. */
        rc = tbl->entries[vec].fn(vec, ctx, tbl->entries[vec].user);
    } else {
        /*
         * Default handler: log the fault and choose a recovery code based
         * on whether the vector is in the unrecoverable set.
         */
        ne_trap_log(tbl, vec, ctx);
        rc = is_fatal_vec(vec) ? NE_TRAP_RECOVER_PANIC : NE_TRAP_RECOVER_SKIP;
    }

    if (rc == NE_TRAP_RECOVER_PANIC)
        ne_trap_panic(tbl, ne_trap_vec_name(vec), ctx);

    return rc;
}

/* =========================================================================
 * ne_trap_strerror
 * ===================================================================== */

const char *ne_trap_strerror(int err)
{
    switch (err) {
    case NE_TRAP_OK:          return "success";
    case NE_TRAP_ERR_NULL:    return "NULL pointer argument";
    case NE_TRAP_ERR_BAD_VEC: return "exception vector out of range";
    default:                  return "unknown error";
    }
}

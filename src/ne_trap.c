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

#ifdef __WATCOMC__
#include <dos.h>    /* _dos_setvect, _dos_getvect, __interrupt */
#include <i86.h>

/*
 * Saved original IVT entries so they can be restored when a handler is
 * removed or when the trap table is freed.
 */
static void (__interrupt __far *g_saved_ivt[NE_TRAP_VEC_COUNT])(void);

/*
 * Global pointer to the active trap table, used by the __interrupt stubs
 * because they cannot receive user-defined arguments directly.
 */
static NETrapTable *g_active_trap_table = NULL;

/*
 * Generic __interrupt stub template.  One stub per vector would be ideal
 * but Open Watcom inline assembly cannot generate parameterised
 * __interrupt functions in a loop.  Instead, we define one dispatcher
 * that captures the register state from the interrupt frame and a set
 * of thin per-vector stubs that call through to it.
 *
 * The compiler-generated __interrupt prologue pushes all registers and
 * restores them on IRET, so we only need to read them.
 */
static void ne_trap_isr_common(uint8_t vec,
                                uint16_t r_ax, uint16_t r_bx,
                                uint16_t r_cx, uint16_t r_dx,
                                uint16_t r_si, uint16_t r_di,
                                uint16_t r_bp, uint16_t r_ds,
                                uint16_t r_es, uint16_t r_flags,
                                uint16_t r_cs, uint16_t r_ip,
                                uint16_t r_ss, uint16_t r_sp)
{
    NETrapContext ctx;

    ctx.ax         = r_ax;
    ctx.bx         = r_bx;
    ctx.cx         = r_cx;
    ctx.dx         = r_dx;
    ctx.si         = r_si;
    ctx.di         = r_di;
    ctx.bp         = r_bp;
    ctx.ds         = r_ds;
    ctx.es         = r_es;
    ctx.flags      = r_flags;
    ctx.cs         = r_cs;
    ctx.ip         = r_ip;
    ctx.ss         = r_ss;
    ctx.sp         = r_sp;
    ctx.error_code = 0;
    ctx.fault_vec  = vec;

    if (g_active_trap_table)
        ne_trap_dispatch(g_active_trap_table, vec, &ctx);
}

/*
 * Per-vector __interrupt stubs.
 *
 * Each stub captures the register state from the compiler-generated
 * interrupt frame and forwards to ne_trap_isr_common.  The Watcom
 * __interrupt keyword ensures the correct PUSHAD/IRET prologue/epilogue.
 */
#define DEFINE_ISR_STUB(VEC)                                            \
    static void __interrupt __far ne_trap_isr_##VEC(                    \
        union INTPACK r)                                                \
    {                                                                   \
        ne_trap_isr_common((VEC),                                       \
                           r.w.ax, r.w.bx, r.w.cx, r.w.dx,            \
                           r.w.si, r.w.di, r.w.bp, r.w.ds,            \
                           r.w.es, r.w.flags, r.w.cs, r.w.ip,         \
                           r.w.ss, r.w.sp);                            \
    }

DEFINE_ISR_STUB(0x00)
DEFINE_ISR_STUB(0x01)
DEFINE_ISR_STUB(0x02)
DEFINE_ISR_STUB(0x03)
DEFINE_ISR_STUB(0x04)
DEFINE_ISR_STUB(0x05)
DEFINE_ISR_STUB(0x06)
DEFINE_ISR_STUB(0x07)
DEFINE_ISR_STUB(0x08)
DEFINE_ISR_STUB(0x09)
DEFINE_ISR_STUB(0x0A)
DEFINE_ISR_STUB(0x0B)
DEFINE_ISR_STUB(0x0C)
DEFINE_ISR_STUB(0x0D)
DEFINE_ISR_STUB(0x0E)
DEFINE_ISR_STUB(0x0F)

/*
 * Lookup table mapping vector index → __interrupt stub.
 */
typedef void (__interrupt __far *ISRStubFn)(union INTPACK);

static ISRStubFn g_isr_stubs[NE_TRAP_VEC_COUNT] = {
    ne_trap_isr_0x00, ne_trap_isr_0x01, ne_trap_isr_0x02, ne_trap_isr_0x03,
    ne_trap_isr_0x04, ne_trap_isr_0x05, ne_trap_isr_0x06, ne_trap_isr_0x07,
    ne_trap_isr_0x08, ne_trap_isr_0x09, ne_trap_isr_0x0A, ne_trap_isr_0x0B,
    ne_trap_isr_0x0C, ne_trap_isr_0x0D, ne_trap_isr_0x0E, ne_trap_isr_0x0F,
};

#endif /* __WATCOMC__ */

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
     * On the Watcom/DOS 16-bit target halt the CPU cleanly.
     * On the POSIX host, exit the process.
     */
#ifdef __WATCOMC__
    _asm { cli };
    _asm { hlt };
#else
    exit(1);
#endif
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

#ifdef __WATCOMC__
    g_active_trap_table = tbl;
#endif

    return NE_TRAP_OK;
}

void ne_trap_table_free(NETrapTable *tbl)
{
    if (!tbl)
        return;

#ifdef __WATCOMC__
    /*
     * Restore all modified IVT entries to their original values saved
     * during ne_trap_install() calls.
     */
    {
        uint8_t i;
        for (i = 0; i < NE_TRAP_VEC_COUNT; i++) {
            if (g_saved_ivt[i] != NULL) {
                _dos_setvect(i, g_saved_ivt[i]);
                g_saved_ivt[i] = NULL;
            }
        }
    }
    if (g_active_trap_table == tbl)
        g_active_trap_table = NULL;
#endif

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

#ifdef __WATCOMC__
    /*
     * Install the corresponding __interrupt stub into the real-mode IVT.
     * Save the original vector for later restoration.
     */
    if (fn != NULL) {
        if (g_saved_ivt[vec] == NULL)
            g_saved_ivt[vec] = _dos_getvect(vec);
        _dos_setvect(vec, (void (__interrupt __far *)(void))g_isr_stubs[vec]);
    }
#endif

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

#ifdef __WATCOMC__
    /*
     * Restore the original IVT entry saved during ne_trap_install().
     */
    if (g_saved_ivt[vec] != NULL) {
        _dos_setvect(vec, g_saved_ivt[vec]);
        g_saved_ivt[vec] = NULL;
    }
#endif

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

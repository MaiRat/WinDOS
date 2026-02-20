/*
 * ne_trap.h - CPU exception and trap handler management
 *
 * Implements Step 7 of the WinDOS kernel-replacement roadmap:
 *   - CPU exception/trap vector definitions for kernel operation.
 *   - Low-level handler stubs for each required vector.
 *   - C-level diagnostic handler dispatch with register context.
 *   - Safe recovery paths for recoverable faults.
 *   - Panic/fatal-error handler for unrecoverable conditions.
 *   - Fault address, exception code, and register state logging.
 *
 * Host-side (POSIX/GCC) implementation: NETrapContext holds a minimal
 * register snapshot synthesised by the caller (e.g., from a signal
 * handler or by direct initialisation in tests).  Actual interrupt-vector
 * installation is not performed on the host because it requires direct
 * manipulation of the 8086 Interrupt Vector Table (IVT).
 *
 * Watcom/DOS 16-bit real-mode target: each handler stub must be declared
 * with the Watcom __interrupt keyword so the compiler generates the correct
 * prologue (push all registers) and epilogue (IRET).  The IVT is installed
 * at segment 0, offset vec*4 (each entry is a 4-byte far pointer).
 * Use _dos_setvect() / _dos_getvect() or direct far-pointer writes to
 * 0000:0000 + vec*4 to register and restore the stubs.  Every stub must
 * build an NETrapContext from the CPU snapshot on the stack and then call
 * ne_trap_dispatch() before returning.
 *
 * Reference: Intel 8086/88 Family Programmer's Reference Manual,
 *            Microsoft MS-DOS Programmer's Reference (INT 21h / AH=25h,35h).
 */

#ifndef NE_TRAP_H
#define NE_TRAP_H

#include <stdint.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * CPU exception vector numbers (8086 / DOS real-mode relevant subset)
 *
 * Only vectors that are meaningful in a 16-bit real-mode kernel are listed.
 * Higher-numbered vectors (INT 20h..FFh) are DOS/BIOS software interrupts
 * and are not managed by this module.
 * ---------------------------------------------------------------------- */
#define NE_TRAP_VEC_DIVIDE_ERROR    0x00u  /* divide by zero / quotient overflow  */
#define NE_TRAP_VEC_SINGLE_STEP     0x01u  /* debug single-step trap              */
#define NE_TRAP_VEC_NMI             0x02u  /* non-maskable interrupt              */
#define NE_TRAP_VEC_BREAKPOINT      0x03u  /* INT 3 software breakpoint           */
#define NE_TRAP_VEC_OVERFLOW        0x04u  /* INTO overflow trap                  */
#define NE_TRAP_VEC_BOUND           0x05u  /* BOUND range exceeded                */
#define NE_TRAP_VEC_INVALID_OPCODE  0x06u  /* undefined / invalid opcode          */
#define NE_TRAP_VEC_NO_FPU          0x07u  /* coprocessor not available           */
#define NE_TRAP_VEC_DOUBLE_FAULT    0x08u  /* double fault (nested exception)     */
#define NE_TRAP_VEC_FPU_SEG         0x09u  /* coprocessor segment overrun         */
#define NE_TRAP_VEC_STACK_FAULT     0x0Cu  /* stack-segment fault                 */
#define NE_TRAP_VEC_GP_FAULT        0x0Du  /* general protection fault            */

/* Total number of CPU exception vectors managed by this module. */
#define NE_TRAP_VEC_COUNT           0x10u  /* vectors 0x00–0x0F                   */

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_TRAP_OK              0
#define NE_TRAP_ERR_NULL       -1   /* NULL pointer argument                */
#define NE_TRAP_ERR_BAD_VEC    -2   /* vector number >= NE_TRAP_VEC_COUNT   */

/* -------------------------------------------------------------------------
 * Recovery codes returned by handler functions and ne_trap_dispatch()
 *
 * The scheduler / fault-dispatch loop interprets the recovery code to
 * decide what to do after the handler returns.
 * ---------------------------------------------------------------------- */
#define NE_TRAP_RECOVER_RETRY   0   /* retry the faulting instruction       */
#define NE_TRAP_RECOVER_SKIP    1   /* skip the faulting instruction        */
#define NE_TRAP_RECOVER_PANIC   2   /* unrecoverable – invoke panic handler */

/* -------------------------------------------------------------------------
 * Register context snapshot at the time of a fault
 *
 * Populated by the low-level handler stub before calling ne_trap_dispatch.
 * All fields are 16 bits wide to match 8086 real-mode register widths.
 *
 * On the Watcom/DOS target the __interrupt prologue pushes all registers
 * onto the stack in a known order; the stub reads them back into this
 * struct before forwarding to the C dispatcher.
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t ax;
    uint16_t bx;
    uint16_t cx;
    uint16_t dx;
    uint16_t si;
    uint16_t di;
    uint16_t bp;
    uint16_t sp;
    uint16_t ss;
    uint16_t ip;         /* fault IP saved on stack by CPU before IRET     */
    uint16_t cs;         /* fault CS saved on stack by CPU before IRET     */
    uint16_t flags;      /* fault FLAGS saved on stack by CPU before IRET  */
    uint16_t ds;
    uint16_t es;
    uint16_t error_code; /* CPU-pushed error code for GP/Stack/DF faults   */
    uint16_t fault_vec;  /* exception vector number (mirrors dispatch arg) */
} NETrapContext;

/* -------------------------------------------------------------------------
 * Handler function type
 *
 * Installed per-vector via ne_trap_install().  Called by ne_trap_dispatch()
 * when the corresponding vector fires.
 *
 * Parameters:
 *   vec  – exception vector number (NE_TRAP_VEC_*)
 *   ctx  – pointer to the register snapshot at fault time
 *   user – opaque pointer supplied to ne_trap_install()
 *
 * Returns one of the NE_TRAP_RECOVER_* codes.
 * ---------------------------------------------------------------------- */
typedef int (*NETrapHandler)(uint8_t vec, const NETrapContext *ctx, void *user);

/* -------------------------------------------------------------------------
 * Per-vector handler entry (internal; exposed for unit-test introspection)
 * ---------------------------------------------------------------------- */
typedef struct {
    NETrapHandler fn;    /* handler function, or NULL for default behaviour */
    void         *user;  /* opaque argument passed back to fn               */
} NETrapEntry;

/* -------------------------------------------------------------------------
 * Trap dispatch table
 *
 * Initialise with ne_trap_table_init(); release with ne_trap_table_free().
 *
 * 'log_fp' receives diagnostic output from ne_trap_log() and the default
 * handler.  Set to NULL to suppress all output.  Defaults to stderr.
 *
 * 'panic_fn' is called by ne_trap_panic() for unrecoverable faults.  If
 * NULL the default implementation prints a diagnostic message and calls
 * exit(1) (on the host) or executes a CLI / HLT sequence (on DOS/Watcom).
 *
 * 'panic_user' is the opaque argument forwarded to panic_fn.
 * ---------------------------------------------------------------------- */
typedef struct {
    NETrapEntry entries[NE_TRAP_VEC_COUNT]; /* per-vector handler table     */

    FILE       *log_fp;      /* diagnostic output stream (NULL = suppress)  */

    /*
     * Optional panic callback.  Signature:
     *   void fn(const char *msg, const NETrapContext *ctx, void *user);
     * If NULL, ne_trap_panic() uses a built-in handler.
     */
    void      (*panic_fn)(const char *msg, const NETrapContext *ctx,
                          void *user);
    void       *panic_user;
} NETrapTable;

/* =========================================================================
 * Public API
 * ===================================================================== */

/*
 * ne_trap_table_init - initialise *tbl with default (logging) handlers.
 *
 * Sets log_fp to stderr and installs the built-in default handler for every
 * vector.  Must be called before any other ne_trap_* function.
 *
 * Returns NE_TRAP_OK or NE_TRAP_ERR_NULL.
 */
int ne_trap_table_init(NETrapTable *tbl);

/*
 * ne_trap_table_free - release resources and zero the table.
 *
 * Safe to call on a zeroed or partially-initialised table and on NULL.
 * Does not close log_fp (the caller owns the file handle).
 */
void ne_trap_table_free(NETrapTable *tbl);

/*
 * ne_trap_install - register a handler for exception vector 'vec'.
 *
 * 'vec'  must be < NE_TRAP_VEC_COUNT.
 * 'fn'   is the handler function; NULL restores the built-in default.
 * 'user' is forwarded to fn on every dispatch.
 *
 * On the Watcom/DOS real-mode target this function should also call
 * _dos_setvect() (or write directly to the IVT at 0000:vec*4) to point
 * the hardware interrupt vector at the corresponding __interrupt stub.
 *
 * Returns NE_TRAP_OK, NE_TRAP_ERR_NULL, or NE_TRAP_ERR_BAD_VEC.
 */
int ne_trap_install(NETrapTable *tbl,
                    uint8_t      vec,
                    NETrapHandler fn,
                    void        *user);

/*
 * ne_trap_remove - remove the custom handler for 'vec' and restore default.
 *
 * On the Watcom/DOS target this should restore the original IVT entry
 * saved during ne_trap_install().
 *
 * Returns NE_TRAP_OK, NE_TRAP_ERR_NULL, or NE_TRAP_ERR_BAD_VEC.
 */
int ne_trap_remove(NETrapTable *tbl, uint8_t vec);

/*
 * ne_trap_dispatch - dispatch exception 'vec' through the handler table.
 *
 * Looks up the handler for 'vec'; if one is installed it is called with
 * (vec, ctx, user) and its return value is forwarded to the caller.  If no
 * custom handler is installed, the built-in default handler is used instead,
 * which logs the fault via ne_trap_log() and returns NE_TRAP_RECOVER_PANIC
 * for unrecoverable vectors or NE_TRAP_RECOVER_SKIP for others.
 *
 * If the returned recovery code is NE_TRAP_RECOVER_PANIC this function
 * calls ne_trap_panic() before returning to give the caller the opportunity
 * to perform final cleanup.
 *
 * Returns one of the NE_TRAP_RECOVER_* codes.
 */
int ne_trap_dispatch(NETrapTable *tbl, uint8_t vec, const NETrapContext *ctx);

/*
 * ne_trap_panic - invoke the fatal-error handler.
 *
 * Logs the fault state via ne_trap_log() then calls tbl->panic_fn if
 * configured, or the built-in handler (print + exit(1)) otherwise.
 *
 * 'msg' is a short description of the reason for the panic; may be NULL.
 * 'ctx' is the register context at fault time; may be NULL.
 *
 * This function does not return when using the built-in handler.
 * Custom panic_fn implementations may return (e.g., for testing).
 */
void ne_trap_panic(NETrapTable *tbl, const char *msg, const NETrapContext *ctx);

/*
 * ne_trap_log - write fault diagnostics to tbl->log_fp.
 *
 * Prints the vector name, vector number, error code, and all register
 * fields from *ctx (if non-NULL) to tbl->log_fp.  Does nothing if
 * tbl->log_fp is NULL.
 */
void ne_trap_log(NETrapTable *tbl, uint8_t vec, const NETrapContext *ctx);

/*
 * ne_trap_vec_name - return a human-readable name for exception vector 'vec'.
 *
 * Returns a pointer to a static string; never NULL.
 * Returns "UNKNOWN" for unrecognised vector numbers.
 */
const char *ne_trap_vec_name(uint8_t vec);

/*
 * ne_trap_strerror - return a static string describing error code 'err'.
 */
const char *ne_trap_strerror(int err);

#endif /* NE_TRAP_H */

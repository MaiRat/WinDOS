/*
 * ne_task.h - Task descriptor and cooperative scheduler
 *
 * Implements Step 6 of the WinDOS kernel-replacement roadmap:
 *   - Task descriptor structure (stack, registers, state, priority).
 *   - Task creation with stack allocation and initial context setup.
 *   - Cooperative scheduling loop with yield and switch hooks.
 *   - Context-save and context-restore routines for task switching.
 *   - Memory ownership tracking per task (GMEM handles owned by each task).
 *
 * Host-side (POSIX/GCC) implementation: context switching uses
 * ucontext_t / makecontext / swapcontext from <ucontext.h>.
 *
 * Watcom/DOS 16-bit target: context switching requires replacement with
 * Watcom inline __asm that saves and restores the full 8086 register set
 * (AX, BX, CX, DX, SI, DI, BP, SS, SP, CS, IP, FLAGS, DS, ES).
 * The NETaskContext type and ne_task_context_* functions must be
 * re-implemented using a small platform assembly module.
 *
 * Reference: Microsoft Windows 3.1 kernel task management internals.
 */

#ifndef NE_TASK_H
#define NE_TASK_H

#include <stdint.h>
#include <stdio.h>

#ifdef __WATCOMC__
/*
 * Watcom/DOS 16-bit real-mode target.
 *
 * NETaskContext holds the complete 8086 CPU register snapshot captured by
 * ne_task_context_save (in ne_task_yield) and consumed by
 * ne_task_context_restore (in ne_task_table_run).  Every field is 16 bits
 * wide to match real-mode register widths.
 *
 * Context switching is NOT implemented in this C module for the DOS target;
 * it requires a separate Watcom assembly (.asm) file.  The assembly module
 * must implement two routines that match the following C prototypes:
 *
 *   void ne_task_context_save(NETaskContext *dst);
 *     – Save AX, BX, CX, DX, SI, DI, BP, DS, ES, FLAGS into *dst.
 *     – Save the current SS:SP into dst->ss / dst->sp.
 *     – Save the return address (the instruction after the call) into
 *       dst->cs / dst->ip so that ne_task_context_restore returns to the
 *       correct point in the caller.
 *
 *   void ne_task_context_restore(const NETaskContext *src);
 *     – Load all register fields from *src in reverse order.
 *     – Perform a far return (RETF) or far JMP to src->cs:src->ip, which
 *       resumes the task (or scheduler) at its saved execution point.
 *
 * These routines replace the swapcontext calls used on the POSIX host.
 */
typedef struct {
    uint16_t ax;
    uint16_t bx;
    uint16_t cx;
    uint16_t dx;
    uint16_t si;
    uint16_t di;
    uint16_t bp;
    uint16_t ss;
    uint16_t sp;
    uint16_t cs;
    uint16_t ip;
    uint16_t flags;
    uint16_t ds;
    uint16_t es;
} NETaskContext;

#else /* !__WATCOMC__ – host-side POSIX build */

/*
 * Host-side POSIX build.
 *
 * Uses ucontext_t for proper per-task stack switching with makecontext and
 * swapcontext.  This gives each task an independent stack so that local
 * variables and the call frame survive across yields.
 */
#include <ucontext.h>
typedef ucontext_t NETaskContext;

#endif /* __WATCOMC__ */

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_TASK_OK              0
#define NE_TASK_ERR_NULL       -1   /* NULL pointer argument                */
#define NE_TASK_ERR_ALLOC      -2   /* memory allocation failure            */
#define NE_TASK_ERR_FULL       -3   /* task table at capacity               */
#define NE_TASK_ERR_BAD_HANDLE -4   /* zero or otherwise invalid handle     */
#define NE_TASK_ERR_NOT_FOUND  -5   /* handle not found in table            */
#define NE_TASK_ERR_STATE      -6   /* operation invalid for current state  */

/* -------------------------------------------------------------------------
 * Task state codes
 * ---------------------------------------------------------------------- */
#define NE_TASK_STATE_READY      0  /* created, not yet started             */
#define NE_TASK_STATE_RUNNING    1  /* currently executing                  */
#define NE_TASK_STATE_YIELDED    2  /* suspended mid-execution              */
#define NE_TASK_STATE_TERMINATED 3  /* returned or explicitly destroyed     */

/* -------------------------------------------------------------------------
 * Task priority levels
 * ---------------------------------------------------------------------- */
#define NE_TASK_PRIORITY_LOW     0
#define NE_TASK_PRIORITY_NORMAL  1
#define NE_TASK_PRIORITY_HIGH    2

/* -------------------------------------------------------------------------
 * Configuration constants
 * ---------------------------------------------------------------------- */

/* Default task table capacity. */
#define NE_TASK_TABLE_CAP   16u

/*
 * Default stack size in bytes for a newly created task.
 *
 * On a 16-bit DOS real-mode target the conventional-memory budget is tight;
 * 4 KB per task is a reasonable starting point (the stack of the Windows 3.1
 * kernel itself is typically 4–8 KB).  Increase for tasks that use deep
 * call chains.
 */
#define NE_TASK_DEFAULT_STACK  4096u

/* Maximum number of GMEM handles a single task may own. */
#define NE_TASK_MAX_OWNED_MEM  32u

/* -------------------------------------------------------------------------
 * Task handle type
 *
 * A non-zero uint16_t value uniquely identifies an active task.
 * NE_TASK_HANDLE_INVALID (0) is the null sentinel.
 * ---------------------------------------------------------------------- */
typedef uint16_t NETaskHandle;

#define NE_TASK_HANDLE_INVALID ((NETaskHandle)0)

/* -------------------------------------------------------------------------
 * Task entry-function type
 *
 * Every task entry function must match this signature.
 * 'arg' is the opaque pointer supplied to ne_task_create().
 * ---------------------------------------------------------------------- */
typedef void (*NETaskEntryFn)(void *arg);

/* -------------------------------------------------------------------------
 * Task descriptor
 *
 * Owns the stack buffer; callers must not free it directly.  Released by
 * ne_task_destroy() or ne_task_table_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    NETaskHandle  handle;      /* unique 1-based handle (0 = free slot)    */
    uint8_t       state;       /* NE_TASK_STATE_*                          */
    uint8_t       priority;    /* NE_TASK_PRIORITY_*                       */

    NETaskContext ctx;         /* saved execution context (see above)      */

    uint8_t      *stack_base;  /* heap-allocated stack buffer              */
    uint16_t      stack_size;  /* stack size in bytes                      */

    NETaskEntryFn entry;       /* task entry function                      */
    void         *arg;         /* opaque argument passed to entry          */

    /*
     * Handles of GMEM blocks owned by this task.
     * All listed handles are freed when the task is destroyed or when
     * ne_task_cleanup_mem() is called.
     */
    uint16_t      owned_mem[NE_TASK_MAX_OWNED_MEM];
    uint16_t      owned_mem_count;
} NETaskDescriptor;

/* -------------------------------------------------------------------------
 * Task table and scheduler state
 * ---------------------------------------------------------------------- */
typedef struct {
    NETaskDescriptor *tasks;         /* heap-allocated array [0..capacity-1] */
    uint16_t          capacity;      /* total slots allocated                */
    uint16_t          count;         /* number of active (non-free) slots    */
    uint16_t          next_handle;   /* next handle value to assign          */

    /*
     * Pointer to the currently executing task (NULL outside a scheduler
     * run or when no task is active).
     */
    NETaskDescriptor *current;

    /*
     * Saved scheduler context.  Used by the task to yield back to the
     * scheduler loop (ne_task_table_run).
     */
    NETaskContext     sched_ctx;
} NETaskTable;

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

/*
 * ne_task_table_init - initialise *tbl with 'capacity' pre-allocated slots.
 *
 * 'capacity' must be > 0.  Pass NE_TASK_TABLE_CAP for the default size.
 * Returns NE_TASK_OK on success; call ne_task_table_free() when done.
 */
int ne_task_table_init(NETaskTable *tbl, uint16_t capacity);

/*
 * ne_task_table_free - release all resources owned by *tbl.
 *
 * Destroys every active task (freeing its stack and owned memory) then
 * frees the tasks array.  Safe to call on a zeroed or partially-initialised
 * table and on NULL.
 *
 * Note: This function does NOT call the per-task GMEM free path because it
 * has no knowledge of the global memory table.  Callers that track GMEM
 * ownership should call ne_task_cleanup_mem() on each task before freeing
 * the table, or arrange for ne_task_table_free_with_gmem() instead.
 */
void ne_task_table_free(NETaskTable *tbl);

/*
 * ne_task_create - create a new task and add it to *tbl.
 *
 * Allocates a 'stack_size'-byte stack buffer and sets up the initial
 * execution context so that when the task is scheduled it will call
 * entry(arg).  If 'stack_size' is 0 the default NE_TASK_DEFAULT_STACK is
 * used.
 *
 * 'priority' must be one of the NE_TASK_PRIORITY_* constants.
 *
 * On success *out_handle is set to the new task's handle and NE_TASK_OK is
 * returned.  On failure *out_handle is NE_TASK_HANDLE_INVALID.
 */
int ne_task_create(NETaskTable  *tbl,
                   NETaskEntryFn entry,
                   void         *arg,
                   uint16_t      stack_size,
                   uint8_t       priority,
                   NETaskHandle *out_handle);

/*
 * ne_task_destroy - terminate and remove a task from the table.
 *
 * Frees the task's stack buffer and zeroes the slot.  The task must be in
 * READY, YIELDED, or TERMINATED state; destroying a RUNNING task returns
 * NE_TASK_ERR_STATE.
 *
 * Returns NE_TASK_OK on success or a negative NE_TASK_ERR_* code.
 */
int ne_task_destroy(NETaskTable *tbl, NETaskHandle handle);

/*
 * ne_task_get - retrieve a pointer to the descriptor for 'handle'.
 *
 * Returns a pointer into the table's internal storage or NULL if 'handle'
 * is NE_TASK_HANDLE_INVALID or not active.  The pointer is valid only until
 * the next ne_task_create() or ne_task_destroy() call.
 */
NETaskDescriptor *ne_task_get(NETaskTable *tbl, NETaskHandle handle);

/*
 * ne_task_yield - voluntarily surrender the CPU from within a running task.
 *
 * Saves the current task's execution context and returns control to the
 * scheduler (ne_task_table_run).  The task transitions from RUNNING to
 * YIELDED; the scheduler will resume it in a future iteration.
 *
 * Must only be called from within a task entry function while a scheduler
 * run (ne_task_table_run) is active.  Calling it from outside a scheduler
 * context is undefined behaviour.
 *
 * On the Watcom/DOS target this function must be re-implemented using
 * inline __asm to save SS:SP and all caller-saved registers before
 * performing the far JMP back to the scheduler.
 */
void ne_task_yield(NETaskTable *tbl);

/*
 * ne_task_table_run - execute one full scheduling pass.
 *
 * Iterates over all tasks in priority order (HIGH first) and runs each
 * READY or YIELDED task until it either terminates or yields again.
 * The pass ends when all tasks are TERMINATED or YIELDED (waiting for a
 * future pass).
 *
 * Returns the number of tasks that were run (switched to RUNNING state)
 * during this pass.  Returns 0 when there are no runnable tasks.
 */
int ne_task_table_run(NETaskTable *tbl);

/*
 * ne_task_own_mem - record that 'handle' owns the GMEM block 'gmem_handle'.
 *
 * The GMEM handle is added to the task's owned_mem list so that it can be
 * freed when the task is destroyed.  Duplicates are silently ignored.
 *
 * Returns NE_TASK_OK, NE_TASK_ERR_NOT_FOUND, or NE_TASK_ERR_STATE (table
 * full for this task's ownership list).
 */
int ne_task_own_mem(NETaskTable *tbl,
                    NETaskHandle handle,
                    uint16_t     gmem_handle);

/*
 * ne_task_disown_mem - remove a GMEM handle from a task's ownership list.
 *
 * Called when a GMEM block is explicitly freed via ne_gmem_free() so that
 * the double-free guard in the task teardown path is not triggered.
 *
 * Returns NE_TASK_OK or NE_TASK_ERR_NOT_FOUND (either the task or the
 * gmem_handle entry is not present).
 */
int ne_task_disown_mem(NETaskTable *tbl,
                       NETaskHandle handle,
                       uint16_t     gmem_handle);

/*
 * ne_task_strerror - return a static string describing error code 'err'.
 */
const char *ne_task_strerror(int err);

#endif /* NE_TASK_H */

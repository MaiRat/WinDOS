/*
 * ne_task.c - Task descriptor and cooperative scheduler implementation
 *
 * Implements Step 6 of the WinDOS kernel-replacement roadmap.
 *
 * Host-side context switching uses ucontext_t (POSIX) via makecontext and
 * swapcontext.  Each task gets its own malloc-allocated stack so that local
 * variables survive yields.
 *
 * On the Watcom/DOS 16-bit target the ne_task_context_* helpers and the
 * ne_task_yield / ne_task_table_run body must be replaced with a platform
 * assembly module that saves and restores SS:SP, CS:IP, and the full set of
 * 8086 general-purpose registers.  All other functions in this file are
 * portable C and require no changes for the DOS target.
 *
 * Memory allocation: malloc / free (large-model Watcom: _fmalloc / _ffree,
 * or DOS INT 21h AH=48h / AH=49h on the real target).
 */

#include "ne_task.h"

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal helpers
 * ---------------------------------------------------------------------- */

/*
 * find_task_by_handle - scan the table for a slot whose handle == h.
 * Returns a pointer to the descriptor or NULL.
 */
static NETaskDescriptor *find_task_by_handle(NETaskTable *tbl,
                                              NETaskHandle h)
{
    uint16_t i;

    if (!tbl || !tbl->tasks || h == NE_TASK_HANDLE_INVALID)
        return NULL;

    for (i = 0; i < tbl->capacity; i++) {
        if (tbl->tasks[i].handle == h)
            return &tbl->tasks[i];
    }
    return NULL;
}

/*
 * find_free_slot - return the first slot with handle == 0.
 * Returns NULL when the table is full.
 */
static NETaskDescriptor *find_free_slot(NETaskTable *tbl)
{
    uint16_t i;

    if (!tbl || !tbl->tasks)
        return NULL;

    for (i = 0; i < tbl->capacity; i++) {
        if (tbl->tasks[i].handle == NE_TASK_HANDLE_INVALID)
            return &tbl->tasks[i];
    }
    return NULL;
}

/*
 * release_task_slot - free the stack buffer and zero the descriptor.
 * Does NOT free any owned GMEM blocks (caller's responsibility).
 */
static void release_task_slot(NETaskDescriptor *t)
{
    if (!t)
        return;
    if (t->stack_base) {
        free(t->stack_base);
        t->stack_base = NULL;
    }
    memset(t, 0, sizeof(*t));
}

/* =========================================================================
 * Platform-specific context initialisation
 *
 * ne_task_context_init sets up the initial context for a newly created task
 * so that the first swapcontext / context-restore call begins execution at
 * task_trampoline (which invokes the entry function).
 *
 * On the Watcom/DOS target this block must be replaced with Watcom __asm
 * code that manually constructs the saved-register image on the task's
 * newly allocated stack.
 * ===================================================================== */

#ifndef __WATCOMC__

/*
 * task_trampoline - entry wrapper executed the first time a task runs.
 *
 * Reads the task pointer from tbl->current (set by ne_task_table_run just
 * before swapping context), calls entry(arg), marks the task TERMINATED,
 * and then swaps back to the scheduler context so the scheduler loop can
 * continue.
 */
static void task_trampoline(uint32_t tbl_lo, uint32_t tbl_hi)
{
    /*
     * Reconstruct the NETaskTable pointer from two uint32_t halves.
     * makecontext passes arguments as int-sized values; on 64-bit hosts a
     * pointer is 64 bits, so we split it into two 32-bit halves.
     */
    uintptr_t addr = ((uintptr_t)tbl_hi << 32) | (uintptr_t)tbl_lo;
    NETaskTable      *tbl  = (NETaskTable *)(void *)addr;
    NETaskDescriptor *task = tbl->current;

    task->entry(task->arg);

    task->state = NE_TASK_STATE_TERMINATED;

    /* Return to the scheduler. */
    swapcontext(&task->ctx, &tbl->sched_ctx);
}

/*
 * ne_task_context_init - initialise the ucontext_t for a new task.
 *
 * Sets the stack, links back to the scheduler context, and arranges for
 * task_trampoline to run first.
 */
static int ne_task_context_init(NETaskTable      *tbl,
                                NETaskDescriptor *task)
{
    uintptr_t addr;
    uint32_t  lo, hi;

    if (getcontext(&task->ctx) != 0)
        return NE_TASK_ERR_ALLOC;

    task->ctx.uc_stack.ss_sp   = task->stack_base;
    task->ctx.uc_stack.ss_size = (size_t)task->stack_size;
    task->ctx.uc_link          = NULL; /* explicit swap back; no auto-link */

    /*
     * Split the table pointer into two int-sized arguments for makecontext.
     * This avoids passing a pointer through a variadic int list (which
     * would be UB on LP64 systems where sizeof(void*) > sizeof(int)).
     */
    addr = (uintptr_t)(void *)tbl;
    lo   = (uint32_t)(addr & 0xFFFFFFFFu);
    hi   = (uint32_t)(addr >> 32);

    makecontext(&task->ctx,
                (void (*)(void))task_trampoline,
                2,
                (int)lo,
                (int)hi);

    return NE_TASK_OK;
}

#else /* __WATCOMC__ */

/*
 * Watcom/DOS 16-bit real-mode stub.
 *
 * The NETaskContext is a plain register-snapshot struct (see ne_task.h).
 * A real implementation must:
 *   1. Switch to the task's allocated stack (load SS:SP from task->stack_base
 *      and task->stack_size).
 *   2. Push a sentinel return address that transitions the task to
 *      NE_TASK_STATE_TERMINATED and performs a far JMP to the scheduler.
 *   3. Save the resulting SS:SP into task->ctx.ss / task->ctx.sp.
 *
 * Until that assembly module is written this stub returns an error so that
 * the build is at least link-clean on the DOS target.
 */
static int ne_task_context_init(NETaskTable      *tbl,
                                NETaskDescriptor *task)
{
    (void)tbl;
    (void)task;
    /*
     * TODO: implement with Watcom inline __asm.
     * Full specification is in ne_task.h inside the #ifdef __WATCOMC__
     * block: build the initial register snapshot on the task's allocated
     * stack so that the first ne_task_context_restore call begins execution
     * at task->entry(task->arg) and sets state to NE_TASK_STATE_TERMINATED
     * on return before jumping back to the scheduler context.
     */
    return NE_TASK_ERR_ALLOC;
}

#endif /* __WATCOMC__ */

/* =========================================================================
 * ne_task_table_init / ne_task_table_free
 * ===================================================================== */

int ne_task_table_init(NETaskTable *tbl, uint16_t capacity)
{
    if (!tbl)
        return NE_TASK_ERR_NULL;
    if (capacity == 0)
        return NE_TASK_ERR_NULL;

    memset(tbl, 0, sizeof(*tbl));

    tbl->tasks = (NETaskDescriptor *)calloc(capacity,
                                            sizeof(NETaskDescriptor));
    if (!tbl->tasks)
        return NE_TASK_ERR_ALLOC;

    tbl->capacity    = capacity;
    tbl->count       = 0;
    tbl->next_handle = 1u;
    tbl->current     = NULL;

    return NE_TASK_OK;
}

void ne_task_table_free(NETaskTable *tbl)
{
    uint16_t i;

    if (!tbl)
        return;

    if (tbl->tasks) {
        for (i = 0; i < tbl->capacity; i++) {
            if (tbl->tasks[i].handle != NE_TASK_HANDLE_INVALID)
                release_task_slot(&tbl->tasks[i]);
        }
        free(tbl->tasks);
    }

    memset(tbl, 0, sizeof(*tbl));
}

/* =========================================================================
 * ne_task_create
 * ===================================================================== */

int ne_task_create(NETaskTable  *tbl,
                   NETaskEntryFn entry,
                   void         *arg,
                   uint16_t      stack_size,
                   uint8_t       priority,
                   NETaskHandle *out_handle)
{
    NETaskDescriptor *slot;
    int               rc;

    if (out_handle)
        *out_handle = NE_TASK_HANDLE_INVALID;

    if (!tbl || !entry || !out_handle)
        return NE_TASK_ERR_NULL;

    if (tbl->count >= tbl->capacity)
        return NE_TASK_ERR_FULL;

    slot = find_free_slot(tbl);
    if (!slot)
        return NE_TASK_ERR_FULL;

    if (stack_size == 0)
        stack_size = NE_TASK_DEFAULT_STACK;

    /* Allocate the task stack. */
    slot->stack_base = (uint8_t *)malloc((size_t)stack_size);
    if (!slot->stack_base)
        return NE_TASK_ERR_ALLOC;

    slot->stack_size     = stack_size;
    slot->entry          = entry;
    slot->arg            = arg;
    slot->priority       = priority;
    slot->state          = NE_TASK_STATE_READY;
    slot->owned_mem_count = 0;
    memset(slot->owned_mem, 0, sizeof(slot->owned_mem));

    /* Set up the initial execution context. */
    rc = ne_task_context_init(tbl, slot);
    if (rc != NE_TASK_OK) {
        free(slot->stack_base);
        slot->stack_base = NULL;
        return rc;
    }

    /* Assign handle last (marks slot as occupied). */
    slot->handle = tbl->next_handle++;
    tbl->count++;

    *out_handle = slot->handle;
    return NE_TASK_OK;
}

/* =========================================================================
 * ne_task_destroy
 * ===================================================================== */

int ne_task_destroy(NETaskTable *tbl, NETaskHandle handle)
{
    NETaskDescriptor *t;

    if (!tbl)
        return NE_TASK_ERR_NULL;
    if (handle == NE_TASK_HANDLE_INVALID)
        return NE_TASK_ERR_BAD_HANDLE;

    t = find_task_by_handle(tbl, handle);
    if (!t)
        return NE_TASK_ERR_NOT_FOUND;

    if (t->state == NE_TASK_STATE_RUNNING)
        return NE_TASK_ERR_STATE;

    tbl->count--;
    release_task_slot(t); /* zeroes slot including handle field */

    return NE_TASK_OK;
}

/* =========================================================================
 * ne_task_get
 * ===================================================================== */

NETaskDescriptor *ne_task_get(NETaskTable *tbl, NETaskHandle handle)
{
    return find_task_by_handle(tbl, handle);
}

/* =========================================================================
 * ne_task_yield
 *
 * Must be called from within a running task while ne_task_table_run() is
 * active.  Transitions the task from RUNNING to YIELDED and returns control
 * to the scheduler.
 * ===================================================================== */

void ne_task_yield(NETaskTable *tbl)
{
#ifndef __WATCOMC__
    NETaskDescriptor *task;

    if (!tbl || !tbl->current)
        return;

    task = tbl->current;
    if (task->state != NE_TASK_STATE_RUNNING)
        return;

    task->state = NE_TASK_STATE_YIELDED;

    /*
     * swapcontext saves this task's complete register and stack state into
     * task->ctx and restores the scheduler's context (sched_ctx), causing
     * ne_task_table_run to resume from its own swapcontext call.
     */
    swapcontext(&task->ctx, &tbl->sched_ctx);

    /*
     * When the scheduler calls swapcontext back to this task (to resume it)
     * execution continues here.  Restore RUNNING state.
     */
    task->state = NE_TASK_STATE_RUNNING;

#else
    /*
     * Watcom/DOS 16-bit real-mode target.
     *
     * Replace this stub with inline __asm that:
     *   1. Saves AX, BX, CX, DX, SI, DI, BP, DS, ES, and FLAGS to
     *      tbl->current->ctx.
     *   2. Saves the current SS:SP to tbl->current->ctx.ss / .sp.
     *   3. Saves the return IP (next instruction after the save) to
     *      tbl->current->ctx.ip.
     *   4. Sets tbl->current->state = NE_TASK_STATE_YIELDED.
     *   5. Loads SS:SP from tbl->sched_ctx.ss / .sp.
     *   6. Performs a far return (RETF) or far JMP to the saved CS:IP in
     *      tbl->sched_ctx to resume the scheduler.
     */
    (void)tbl;
#endif
}

/* =========================================================================
 * ne_task_table_run
 *
 * Single scheduling pass over all tasks in priority order.
 * Returns the number of tasks run during this pass.
 * ===================================================================== */

int ne_task_table_run(NETaskTable *tbl)
{
#ifndef __WATCOMC__
    int      run_count = 0;
    uint8_t  pri;
    uint16_t i;

    if (!tbl || !tbl->tasks)
        return 0;

    /*
     * Iterate from HIGH priority down to LOW.  Within each priority band
     * run tasks in slot order (round-robin among equal-priority tasks is
     * deferred to a future enhancement).
     */
    for (pri = NE_TASK_PRIORITY_HIGH; ; pri--) {
        for (i = 0; i < tbl->capacity; i++) {
            NETaskDescriptor *task = &tbl->tasks[i];

            if (task->handle == NE_TASK_HANDLE_INVALID)
                continue;
            if (task->priority != pri)
                continue;
            if (task->state != NE_TASK_STATE_READY &&
                task->state != NE_TASK_STATE_YIELDED)
                continue;

            /* Activate the task. */
            tbl->current   = task;
            task->state    = NE_TASK_STATE_RUNNING;
            run_count++;

            /*
             * swapcontext saves the scheduler's current register state into
             * tbl->sched_ctx and restores task->ctx, transferring control to
             * the task.  Execution returns here when the task yields
             * (swapping back to sched_ctx from inside ne_task_yield) or when
             * the task terminates (task_trampoline swaps back after setting
             * state to TERMINATED).
             */
            swapcontext(&tbl->sched_ctx, &task->ctx);

            /*
             * The task has either yielded or terminated.  Clear current so
             * that ne_task_yield() called outside a run is a no-op.
             */
            tbl->current = NULL;
        }

        if (pri == NE_TASK_PRIORITY_LOW)
            break;
    }

    return run_count;

#else
    /*
     * Watcom/DOS 16-bit real-mode target.
     *
     * Replace with the equivalent assembly-backed context-switch loop.
     * The logic is identical to the POSIX implementation above; only the
     * swapcontext calls need to be replaced with inline __asm that swaps
     * SS:SP and jumps to the saved CS:IP.
     */
    (void)tbl;
    return 0;
#endif
}

/* =========================================================================
 * ne_task_own_mem / ne_task_disown_mem
 * ===================================================================== */

int ne_task_own_mem(NETaskTable *tbl,
                    NETaskHandle handle,
                    uint16_t     gmem_handle)
{
    NETaskDescriptor *t;
    uint16_t          i;

    if (!tbl)
        return NE_TASK_ERR_NULL;
    if (handle == NE_TASK_HANDLE_INVALID)
        return NE_TASK_ERR_BAD_HANDLE;

    t = find_task_by_handle(tbl, handle);
    if (!t)
        return NE_TASK_ERR_NOT_FOUND;

    /* Check for duplicate. */
    for (i = 0; i < t->owned_mem_count; i++) {
        if (t->owned_mem[i] == gmem_handle)
            return NE_TASK_OK; /* already tracked */
    }

    if (t->owned_mem_count >= NE_TASK_MAX_OWNED_MEM)
        return NE_TASK_ERR_STATE; /* ownership table full */

    t->owned_mem[t->owned_mem_count++] = gmem_handle;
    return NE_TASK_OK;
}

int ne_task_disown_mem(NETaskTable *tbl,
                       NETaskHandle handle,
                       uint16_t     gmem_handle)
{
    NETaskDescriptor *t;
    uint16_t          i;

    if (!tbl)
        return NE_TASK_ERR_NULL;
    if (handle == NE_TASK_HANDLE_INVALID)
        return NE_TASK_ERR_BAD_HANDLE;

    t = find_task_by_handle(tbl, handle);
    if (!t)
        return NE_TASK_ERR_NOT_FOUND;

    for (i = 0; i < t->owned_mem_count; i++) {
        if (t->owned_mem[i] == gmem_handle) {
            /* Remove by shifting the tail down one position. */
            t->owned_mem_count--;
            if (i < t->owned_mem_count)
                t->owned_mem[i] = t->owned_mem[t->owned_mem_count];
            t->owned_mem[t->owned_mem_count] = 0;
            return NE_TASK_OK;
        }
    }

    return NE_TASK_ERR_NOT_FOUND;
}

/* =========================================================================
 * ne_task_strerror
 * ===================================================================== */

const char *ne_task_strerror(int err)
{
    switch (err) {
    case NE_TASK_OK:              return "success";
    case NE_TASK_ERR_NULL:        return "NULL pointer argument";
    case NE_TASK_ERR_ALLOC:       return "memory allocation failure";
    case NE_TASK_ERR_FULL:        return "task table at capacity";
    case NE_TASK_ERR_BAD_HANDLE:  return "invalid task handle";
    case NE_TASK_ERR_NOT_FOUND:   return "task handle not found";
    case NE_TASK_ERR_STATE:       return "operation invalid for current task state";
    default:                      return "unknown error";
    }
}

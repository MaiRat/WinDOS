/*
 * ne_kernel.h - Phase 2 KERNEL.EXE API stubs
 *
 * Declares the unified kernel context and the stub functions that
 * correspond to the Windows 3.1 KERNEL.EXE ordinal exports.  Each
 * stub delegates to the lower-level subsystem (ne_mem, ne_task,
 * ne_module, ne_impexp) through a single NEKernelContext that owns
 * pointers to all subsystem tables.
 *
 * File I/O stubs (_lopen, _lclose, _lread, _lwrite, _llseek) wrap
 * standard C <stdio.h> calls on the host side; on a real 16-bit DOS
 * target they should be replaced with INT 21h service calls.
 *
 * The atom table is a simple linear array capped at
 * NE_KERNEL_ATOM_TABLE_CAP entries.  Atom values start at
 * NE_KERNEL_ATOM_BASE (0xC000) following the Windows convention for
 * string atoms.
 *
 * Reference: Microsoft Windows 3.1 SDK – KERNEL.EXE ordinal list.
 */

#ifndef NE_KERNEL_H
#define NE_KERNEL_H

#include "ne_impexp.h"
#include "ne_mem.h"
#include "ne_task.h"
#include "ne_module.h"

#include <setjmp.h>

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_KERNEL_OK              0
#define NE_KERNEL_ERR_NULL       -1   /* NULL pointer argument               */
#define NE_KERNEL_ERR_INIT       -2   /* context not initialised             */
#define NE_KERNEL_ERR_IO         -3   /* file I/O failure                    */
#define NE_KERNEL_ERR_FULL       -4   /* table at capacity                   */
#define NE_KERNEL_ERR_NOT_FOUND  -5   /* requested item not found            */

/* -------------------------------------------------------------------------
 * File I/O constants
 * ---------------------------------------------------------------------- */
#define NE_KERNEL_OF_READ        0x0000u  /* open for reading                */
#define NE_KERNEL_OF_WRITE       0x0001u  /* open for writing                */
#define NE_KERNEL_OF_READWRITE   0x0002u  /* open for reading and writing    */
#define NE_KERNEL_HFILE_ERROR    (-1)     /* invalid file handle sentinel    */
#define NE_KERNEL_FILE_BEGIN     0        /* seek from beginning of file     */
#define NE_KERNEL_FILE_CURRENT   1        /* seek from current position      */
#define NE_KERNEL_FILE_END       2        /* seek from end of file           */

/* -------------------------------------------------------------------------
 * Export classification
 * ---------------------------------------------------------------------- */
#define NE_KERNEL_CLASS_CRITICAL   0  /* must be implemented for boot        */
#define NE_KERNEL_CLASS_SECONDARY  1  /* needed by most applications         */
#define NE_KERNEL_CLASS_OPTIONAL   2  /* rarely used or safely stubbed       */

/* -------------------------------------------------------------------------
 * Atom table constants
 * ---------------------------------------------------------------------- */
#define NE_KERNEL_ATOM_TABLE_CAP  64u    /* max entries in the atom table    */
#define NE_KERNEL_ATOM_NAME_MAX  256u    /* max atom name length incl. NUL   */
#define NE_KERNEL_ATOM_BASE    0xC000u   /* Windows string-atom base value   */
#define NE_KERNEL_ATOM_INVALID     0u    /* sentinel for no atom             */

/* -------------------------------------------------------------------------
 * KERNEL.EXE ordinal constants  (Windows 3.1)
 * ---------------------------------------------------------------------- */
#define NE_KERNEL_ORD_LOCAL_ALLOC          5
#define NE_KERNEL_ORD_LOCAL_FREE           7
#define NE_KERNEL_ORD_LOCAL_LOCK           8
#define NE_KERNEL_ORD_LOCAL_UNLOCK         9
#define NE_KERNEL_ORD_GLOBAL_ALLOC        15
#define NE_KERNEL_ORD_GLOBAL_REALLOC      16
#define NE_KERNEL_ORD_GLOBAL_FREE         17
#define NE_KERNEL_ORD_GLOBAL_LOCK         18
#define NE_KERNEL_ORD_GLOBAL_UNLOCK       19
#define NE_KERNEL_ORD_WAIT_EVENT          30
#define NE_KERNEL_ORD_POST_EVENT          31
#define NE_KERNEL_ORD_GET_CURRENT_TASK    36
#define NE_KERNEL_ORD_GET_MODULE_HANDLE   47
#define NE_KERNEL_ORD_GET_MODULE_FILENAME 49
#define NE_KERNEL_ORD_GET_PROC_ADDRESS    50
#define NE_KERNEL_ORD_FIND_RESOURCE       60
#define NE_KERNEL_ORD_LOAD_RESOURCE       61
#define NE_KERNEL_ORD_LOCK_RESOURCE       62
#define NE_KERNEL_ORD_INIT_TASK           73
#define NE_KERNEL_ORD_LOPEN               81
#define NE_KERNEL_ORD_LCLOSE              82
#define NE_KERNEL_ORD_LREAD               83
#define NE_KERNEL_ORD_LLSEEK              84
#define NE_KERNEL_ORD_LWRITE              85
#define NE_KERNEL_ORD_LOAD_LIBRARY        96
#define NE_KERNEL_ORD_FREE_LIBRARY       106
#define NE_KERNEL_ORD_YIELD              136
#define NE_KERNEL_ORD_GLOBAL_ADD_ATOM    163
#define NE_KERNEL_ORD_GLOBAL_DELETE_ATOM 164
#define NE_KERNEL_ORD_GLOBAL_FIND_ATOM   165
#define NE_KERNEL_ORD_GLOBAL_GET_ATOM_NAME 167
#define NE_KERNEL_ORD_LOAD_STRING        176

/* Phase A – critical KERNEL.EXE ordinals (Windows 3.1) */
#define NE_KERNEL_ORD_FATAL_EXIT           1
#define NE_KERNEL_ORD_GET_VERSION          3
#define NE_KERNEL_ORD_EXIT_WINDOWS         7
#define NE_KERNEL_ORD_GET_TICK_COUNT      13
#define NE_KERNEL_ORD_MAKE_PROC_INSTANCE  51
#define NE_KERNEL_ORD_FREE_PROC_INSTANCE  52
#define NE_KERNEL_ORD_CATCH               55
#define NE_KERNEL_ORD_THROW               56
#define NE_KERNEL_ORD_OPEN_FILE           74
#define NE_KERNEL_ORD_SET_ERROR_MODE     107
#define NE_KERNEL_ORD_OUTPUT_DEBUG_STRING 115
#define NE_KERNEL_ORD_GET_DOS_ENVIRONMENT 131
#define NE_KERNEL_ORD_GET_WIN_FLAGS      132
#define NE_KERNEL_ORD_GET_WINDOWS_DIR    134
#define NE_KERNEL_ORD_GET_SYSTEM_DIR     135
#define NE_KERNEL_ORD_FATAL_APP_EXIT     137
#define NE_KERNEL_ORD_GET_LAST_ERROR     148
#define NE_KERNEL_ORD_GET_NUM_TASKS      152
#define NE_KERNEL_ORD_WIN_EXEC           166
#define NE_KERNEL_ORD_IS_TASK            320

/* Phase B – INI file / profile API ordinals (Windows 3.1) */
#define NE_KERNEL_ORD_GET_PROFILE_INT           57
#define NE_KERNEL_ORD_GET_PROFILE_STRING        58
#define NE_KERNEL_ORD_WRITE_PROFILE_STRING      59
#define NE_KERNEL_ORD_GET_PRIVATE_PROFILE_INT  127
#define NE_KERNEL_ORD_GET_PRIVATE_PROFILE_STRING 128
#define NE_KERNEL_ORD_WRITE_PRIVATE_PROFILE_STRING 129

/* -------------------------------------------------------------------------
 * INI file constants
 * ---------------------------------------------------------------------- */
#define NE_KERNEL_INI_PATH_MAX  260u    /* max INI file path length */
#define NE_KERNEL_INI_LINE_MAX  512u    /* max INI file line length */
#define NE_KERNEL_INI_VALUE_MAX 256u    /* max INI value length     */

/* -------------------------------------------------------------------------
 * GetWinFlags constants
 * ---------------------------------------------------------------------- */
#define NE_WF_PMODE      0x0001u   /* running in protected mode            */
#define NE_WF_CPU286     0x0002u   /* 80286 processor                      */
#define NE_WF_CPU386     0x0004u   /* 80386 processor                      */
#define NE_WF_CPU486     0x0008u   /* 80486 processor                      */
#define NE_WF_STANDARD   0x0010u   /* standard mode                        */
#define NE_WF_ENHANCED   0x0020u   /* 386 enhanced mode                    */
#define NE_WF_80x87      0x0400u   /* math coprocessor present             */

/* -------------------------------------------------------------------------
 * Catch / Throw types – non-local jump support
 *
 * NECatchBuf wraps a standard jmp_buf so that Catch/Throw can use
 * setjmp/longjmp portably regardless of jmp_buf size.
 * ---------------------------------------------------------------------- */
typedef struct {
    jmp_buf env;
} NECatchBuf;

/* -------------------------------------------------------------------------
 * OpenFile constants and structures
 * ---------------------------------------------------------------------- */
#define NE_OF_READ        0x0000u  /* open for reading                     */
#define NE_OF_WRITE       0x0001u  /* open for writing                     */
#define NE_OF_READWRITE   0x0002u  /* open for reading and writing         */
#define NE_OF_EXIST       0x4000u  /* test for existence only              */
#define NE_OF_DELETE      0x0200u  /* delete the file                      */

#define NE_OFS_MAXPATHNAME  128

typedef struct {
    uint8_t  cBytes;                         /* size of this struct         */
    uint8_t  fFixedDisk;                     /* non-zero if on fixed disk   */
    uint16_t nErrCode;                       /* DOS error code              */
    char     szPathName[NE_OFS_MAXPATHNAME]; /* full pathname               */
} NEOfStruct;

/* -------------------------------------------------------------------------
 * Export catalog entry
 *
 * Describes one KERNEL.EXE export for the static catalog returned by
 * ne_kernel_get_export_catalog().
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t ordinal;                      /* KERNEL.EXE ordinal number      */
    char     name[NE_EXPORT_NAME_MAX];     /* API name                       */
    uint8_t  classification;               /* NE_KERNEL_CLASS_*              */
} NEKernelExportInfo;

/* -------------------------------------------------------------------------
 * Kernel context
 *
 * Aggregates pointers to every subsystem table plus the kernel-private
 * atom table.  Initialise with ne_kernel_init(); release with
 * ne_kernel_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    NEGMemTable   *gmem;       /* global memory table (owned externally)    */
    NELMemHeap    *lmem;       /* local memory heap  (owned externally)     */
    NETaskTable   *tasks;      /* task table          (owned externally)    */
    NEModuleTable *modules;    /* module table        (owned externally)    */
    NEExportTable  exports;    /* KERNEL.EXE export table (owned)           */

    /* Atom table */
    struct {
        uint16_t atom;                            /* atom value             */
        char     name[NE_KERNEL_ATOM_NAME_MAX];   /* atom string           */
    } atoms[NE_KERNEL_ATOM_TABLE_CAP];
    uint16_t atom_count;       /* number of atoms currently registered      */
    uint16_t next_atom;        /* next atom value to assign                 */

    /* Phase A fields */
    uint16_t error_mode;       /* current error mode (SetErrorMode)         */
    uint16_t last_error;       /* last error code (GetLastError)            */
    void    *driver;           /* optional NEDrvContext for GetTickCount     */

    int      initialized;      /* non-zero after successful init            */
} NEKernelContext;

/* =========================================================================
 * Public API – initialisation
 * ===================================================================== */

/*
 * ne_kernel_init - initialise the kernel context.
 *
 * Stores the subsystem pointers, zeroes the atom table, and marks the
 * context as initialised.  All pointer arguments must be non-NULL.
 *
 * Returns NE_KERNEL_OK on success or NE_KERNEL_ERR_NULL.
 */
int ne_kernel_init(NEKernelContext *ctx,
                   NEGMemTable     *gmem,
                   NELMemHeap      *lmem,
                   NETaskTable     *tasks,
                   NEModuleTable   *modules);

/*
 * ne_kernel_free - release resources owned by the kernel context.
 *
 * Frees the internal export table and zeroes the context.  Does NOT free
 * the subsystem tables (gmem, lmem, tasks, modules) because they are
 * owned externally.  Safe to call on a zeroed context or NULL.
 */
void ne_kernel_free(NEKernelContext *ctx);

/* =========================================================================
 * Public API – export catalog
 * ===================================================================== */

/*
 * ne_kernel_get_export_catalog - return the static KERNEL.EXE export list.
 *
 * Sets *out to point to the internal catalog array and *out_count to the
 * number of entries.  The returned pointer is valid for the lifetime of
 * the program (static storage).
 *
 * Returns NE_KERNEL_OK or NE_KERNEL_ERR_NULL.
 */
int ne_kernel_get_export_catalog(const NEKernelExportInfo **out,
                                 uint16_t                  *out_count);

/* =========================================================================
 * Public API – export registration
 * ===================================================================== */

/*
 * ne_kernel_register_exports - populate the NEExportTable in ctx->exports.
 *
 * Builds export entries for every KERNEL.EXE ordinal so that other
 * modules can resolve imports against the kernel via the standard
 * ne_export_find_by_ordinal / ne_export_find_by_name helpers.
 *
 * Returns NE_KERNEL_OK or NE_KERNEL_ERR_INIT.
 */
int ne_kernel_register_exports(NEKernelContext *ctx);

/* =========================================================================
 * Public API – file I/O
 * ===================================================================== */

/*
 * ne_kernel_lopen - open a file and return a file handle.
 *
 * 'mode' is one of NE_KERNEL_OF_READ, NE_KERNEL_OF_WRITE, or
 * NE_KERNEL_OF_READWRITE.
 *
 * Returns a non-negative file handle on success or NE_KERNEL_HFILE_ERROR
 * on failure.
 */
int ne_kernel_lopen(NEKernelContext *ctx, const char *path, uint16_t mode);

/*
 * ne_kernel_lclose - close a previously opened file handle.
 *
 * Returns NE_KERNEL_OK or NE_KERNEL_ERR_IO.
 */
int ne_kernel_lclose(NEKernelContext *ctx, int hFile);

/*
 * ne_kernel_lread - read bytes from a file.
 *
 * Reads up to 'count' bytes into 'buf'.  Returns the number of bytes
 * actually read, or NE_KERNEL_HFILE_ERROR on failure.
 */
int ne_kernel_lread(NEKernelContext *ctx, int hFile,
                    void *buf, uint16_t count);

/*
 * ne_kernel_lwrite - write bytes to a file.
 *
 * Writes up to 'count' bytes from 'buf'.  Returns the number of bytes
 * actually written, or NE_KERNEL_HFILE_ERROR on failure.
 */
int ne_kernel_lwrite(NEKernelContext *ctx, int hFile,
                     const void *buf, uint16_t count);

/*
 * ne_kernel_llseek - reposition the file pointer.
 *
 * 'origin' is one of NE_KERNEL_FILE_BEGIN, NE_KERNEL_FILE_CURRENT, or
 * NE_KERNEL_FILE_END.
 *
 * Returns the new file position, or NE_KERNEL_HFILE_ERROR on failure.
 */
long ne_kernel_llseek(NEKernelContext *ctx, int hFile,
                      long offset, int origin);

/* =========================================================================
 * Public API – module management
 * ===================================================================== */

/*
 * ne_kernel_get_module_handle - return the handle for a loaded module.
 *
 * Returns a non-zero handle on success or 0 if not found.
 */
uint16_t ne_kernel_get_module_handle(NEKernelContext *ctx, const char *name);

/*
 * ne_kernel_get_module_filename - copy the filename of a module into 'buf'.
 *
 * Copies at most 'size' bytes (including the NUL terminator).
 * Returns NE_KERNEL_OK or NE_KERNEL_ERR_NOT_FOUND.
 */
int ne_kernel_get_module_filename(NEKernelContext *ctx, uint16_t hModule,
                                  char *buf, int size);

/*
 * ne_kernel_get_proc_address - look up a named export in a module.
 *
 * Returns a packed seg:offset value on success or 0 on failure.
 */
uint32_t ne_kernel_get_proc_address(NEKernelContext *ctx, uint16_t hModule,
                                     const char *name);

/*
 * ne_kernel_load_library - load a module by name and return its handle.
 *
 * Returns a non-zero handle on success or 0 on failure.
 */
uint16_t ne_kernel_load_library(NEKernelContext *ctx, const char *name);

/*
 * ne_kernel_free_library - decrement a module's reference count.
 *
 * The module is unloaded when its count reaches zero.
 */
void ne_kernel_free_library(NEKernelContext *ctx, uint16_t hModule);

/* =========================================================================
 * Public API – global memory
 * ===================================================================== */

/*
 * ne_kernel_global_alloc - allocate a global memory block.
 *
 * 'flags' is a combination of NE_GMEM_* constants.
 * Returns a non-zero handle on success or NE_GMEM_HANDLE_INVALID.
 */
NEGMemHandle ne_kernel_global_alloc(NEKernelContext *ctx,
                                     uint16_t flags, uint32_t size);

/*
 * ne_kernel_global_free - free a global memory block.
 *
 * Returns NE_KERNEL_OK or a negative error code.
 */
int ne_kernel_global_free(NEKernelContext *ctx, NEGMemHandle handle);

/*
 * ne_kernel_global_lock - lock a global block and return a pointer.
 *
 * Returns a pointer to the block data, or NULL on failure.
 */
void *ne_kernel_global_lock(NEKernelContext *ctx, NEGMemHandle handle);

/*
 * ne_kernel_global_unlock - decrement the lock count of a global block.
 *
 * Returns NE_KERNEL_OK or a negative error code.
 */
int ne_kernel_global_unlock(NEKernelContext *ctx, NEGMemHandle handle);

/*
 * ne_kernel_global_realloc - change the size of an existing global block.
 *
 * Returns the (possibly new) handle on success or NE_GMEM_HANDLE_INVALID.
 */
NEGMemHandle ne_kernel_global_realloc(NEKernelContext *ctx,
                                       NEGMemHandle handle,
                                       uint32_t new_size, uint16_t flags);

/* =========================================================================
 * Public API – local memory
 * ===================================================================== */

/*
 * ne_kernel_local_alloc - allocate a local memory block.
 *
 * 'flags' is a combination of NE_LMEM_* constants.
 * Returns a non-zero handle on success or NE_LMEM_HANDLE_INVALID.
 */
NELMemHandle ne_kernel_local_alloc(NEKernelContext *ctx,
                                    uint16_t flags, uint16_t size);

/*
 * ne_kernel_local_free - free a local memory block.
 *
 * Returns NE_KERNEL_OK or a negative error code.
 */
int ne_kernel_local_free(NEKernelContext *ctx, NELMemHandle handle);

/*
 * ne_kernel_local_lock - lock a local block and return a pointer.
 *
 * Returns a pointer to the block data, or NULL on failure.
 */
void *ne_kernel_local_lock(NEKernelContext *ctx, NELMemHandle handle);

/*
 * ne_kernel_local_unlock - decrement the lock count of a local block.
 *
 * Returns NE_KERNEL_OK or a negative error code.
 */
int ne_kernel_local_unlock(NEKernelContext *ctx, NELMemHandle handle);

/* =========================================================================
 * Public API – task / process management
 * ===================================================================== */

/*
 * ne_kernel_get_current_task - return the handle of the running task.
 *
 * Returns a non-zero handle or 0 if no task is active.
 */
uint16_t ne_kernel_get_current_task(NEKernelContext *ctx);

/*
 * ne_kernel_yield - voluntarily surrender the CPU.
 *
 * Transfers control to the cooperative scheduler.
 */
void ne_kernel_yield(NEKernelContext *ctx);

/*
 * ne_kernel_init_task - perform initial task setup for the current task.
 *
 * Returns NE_KERNEL_OK or a negative error code.
 */
int ne_kernel_init_task(NEKernelContext *ctx);

/*
 * ne_kernel_wait_event - block until an event is posted for 'hTask'.
 *
 * Returns NE_KERNEL_OK or a negative error code.
 */
int ne_kernel_wait_event(NEKernelContext *ctx, uint16_t hTask);

/*
 * ne_kernel_post_event - post an event to wake 'hTask'.
 *
 * Returns NE_KERNEL_OK or a negative error code.
 */
int ne_kernel_post_event(NEKernelContext *ctx, uint16_t hTask);

/* =========================================================================
 * Public API – string / resource stubs
 * ===================================================================== */

/*
 * ne_kernel_load_string - load a string resource.
 *
 * Copies at most 'buf_size' bytes (including NUL) into 'buf'.
 * Returns the number of characters copied (excluding NUL) or 0 on failure.
 */
int ne_kernel_load_string(NEKernelContext *ctx, uint16_t hModule,
                           uint16_t uID, char *buf, int buf_size);

/*
 * ne_kernel_find_resource - locate a resource in a module.
 *
 * Returns a non-zero resource info handle on success or 0 on failure.
 */
uint32_t ne_kernel_find_resource(NEKernelContext *ctx, uint16_t hModule,
                                  const char *name, const char *type);

/*
 * ne_kernel_load_resource - load a resource into memory.
 *
 * Returns a non-zero resource data handle on success or 0 on failure.
 */
uint16_t ne_kernel_load_resource(NEKernelContext *ctx, uint16_t hModule,
                                  uint32_t hResInfo);

/*
 * ne_kernel_lock_resource - lock a loaded resource and return a pointer.
 *
 * Returns a pointer to the resource data, or NULL on failure.
 */
void *ne_kernel_lock_resource(NEKernelContext *ctx, uint16_t hResData);

/* =========================================================================
 * Public API – atom table
 * ===================================================================== */

/*
 * ne_kernel_global_add_atom - add a string to the global atom table.
 *
 * If 'name' already exists, returns the existing atom value.  Otherwise
 * assigns a new atom value starting from NE_KERNEL_ATOM_BASE.
 *
 * Returns the atom value on success or NE_KERNEL_ATOM_INVALID on failure.
 */
uint16_t ne_kernel_global_add_atom(NEKernelContext *ctx, const char *name);

/*
 * ne_kernel_global_find_atom - look up an atom by name.
 *
 * Returns the atom value or NE_KERNEL_ATOM_INVALID if not found.
 */
uint16_t ne_kernel_global_find_atom(NEKernelContext *ctx, const char *name);

/*
 * ne_kernel_global_get_atom_name - copy the name of an atom into 'buf'.
 *
 * Copies at most 'size' bytes (including NUL).
 * Returns NE_KERNEL_OK or NE_KERNEL_ERR_NOT_FOUND.
 */
int ne_kernel_global_get_atom_name(NEKernelContext *ctx, uint16_t atom,
                                    char *buf, int size);

/*
 * ne_kernel_global_delete_atom - remove an atom from the table.
 *
 * Returns NE_KERNEL_OK or NE_KERNEL_ERR_NOT_FOUND.
 */
int ne_kernel_global_delete_atom(NEKernelContext *ctx, uint16_t atom);

/* =========================================================================
 * Public API – error string
 * ===================================================================== */

/*
 * ne_kernel_strerror - return a static string describing error code 'err'.
 */
const char *ne_kernel_strerror(int err);

/* =========================================================================
 * Public API – Phase A: Critical KERNEL.EXE APIs
 * ===================================================================== */

/*
 * ne_kernel_set_driver - attach an optional driver context for GetTickCount.
 *
 * 'driver' may be NULL to detach.  Returns NE_KERNEL_OK or
 * NE_KERNEL_ERR_INIT if the kernel context is not initialised.
 */
int ne_kernel_set_driver(NEKernelContext *ctx, void *driver);

/*
 * ne_kernel_get_version - return the Windows version (3.10).
 *
 * Returns 0x0A03: low byte = major (3), high byte = minor (10).
 */
uint16_t ne_kernel_get_version(NEKernelContext *ctx);

/*
 * ne_kernel_get_win_flags - return capability flags for the environment.
 *
 * WinDOS runs in real mode on an 8086, so all protected-mode and
 * coprocessor flags are cleared.  Returns 0.
 */
uint32_t ne_kernel_get_win_flags(NEKernelContext *ctx);

/*
 * ne_kernel_get_windows_directory - copy the Windows directory path into
 * 'buf'.
 *
 * Copies at most 'size' bytes (including NUL).  Returns the string length
 * (excluding NUL) on success, or 0 on failure.
 */
int ne_kernel_get_windows_directory(NEKernelContext *ctx,
                                     char *buf, int size);

/*
 * ne_kernel_get_system_directory - copy the Windows SYSTEM directory
 * path into 'buf'.
 *
 * Copies at most 'size' bytes (including NUL).  Returns the string length
 * (excluding NUL) on success, or 0 on failure.
 */
int ne_kernel_get_system_directory(NEKernelContext *ctx,
                                    char *buf, int size);

/*
 * ne_kernel_get_dos_environment - return a pointer to the DOS environment
 * block.
 *
 * Stub: returns NULL.
 */
const char *ne_kernel_get_dos_environment(NEKernelContext *ctx);

/*
 * ne_kernel_win_exec - launch a child application.
 *
 * Stub: returns 0 (not yet implemented).
 */
uint16_t ne_kernel_win_exec(NEKernelContext *ctx, const char *cmdLine,
                             uint16_t cmdShow);

/*
 * ne_kernel_exit_windows - initiate a clean shutdown.
 *
 * Stub: returns 0.
 */
int ne_kernel_exit_windows(NEKernelContext *ctx, uint32_t dwReserved);

/*
 * ne_kernel_fatal_exit - terminate with error code.
 *
 * Calls exit() with the given code.
 */
void ne_kernel_fatal_exit(NEKernelContext *ctx, int code);

/*
 * ne_kernel_fatal_app_exit - display an error message and terminate.
 *
 * Prints 'msg' to stderr and calls exit(1).
 */
void ne_kernel_fatal_app_exit(NEKernelContext *ctx, uint16_t action,
                               const char *msg);

/*
 * ne_kernel_get_tick_count - return the system tick count in milliseconds.
 *
 * Delegates to ne_drv_get_tick_count() if a driver context is attached.
 * Otherwise returns 0.
 */
uint32_t ne_kernel_get_tick_count(NEKernelContext *ctx);

/*
 * ne_kernel_catch - save the execution context for non-local jump.
 *
 * Wraps setjmp().  Returns 0 when called directly; non-zero when
 * restored via ne_kernel_throw().  Returns -1 if ctx or buf is NULL
 * or ctx is not initialised.  Implemented as a macro because
 * setjmp must be called from the frame that will longjmp back.
 */
#define ne_kernel_catch(ctx, buf) \
    (((ctx) != NULL && (ctx)->initialized && (buf) != NULL) \
        ? setjmp((buf)->env) : -1)

/*
 * ne_kernel_throw - restore a previously saved context.
 *
 * Wraps longjmp().  'retval' is the value returned by the matching
 * ne_kernel_catch().
 */
void ne_kernel_throw(NEKernelContext *ctx, NECatchBuf *buf, int retval);

/*
 * ne_kernel_make_proc_instance - create a callback thunk.
 *
 * In real mode no thunk is needed; the function pointer is returned as-is.
 */
void *ne_kernel_make_proc_instance(NEKernelContext *ctx, void *proc,
                                    uint16_t hInstance);

/*
 * ne_kernel_free_proc_instance - free a callback thunk.
 *
 * No-op in real mode.
 */
void ne_kernel_free_proc_instance(NEKernelContext *ctx, void *proc);

/*
 * ne_kernel_open_file - open, test, or delete a file with OFSTRUCT semantics.
 *
 * 'style' is a combination of NE_OF_* flags.  The OFSTRUCT is filled in
 * with the result.  Returns a non-negative file handle on success, or
 * NE_KERNEL_HFILE_ERROR on failure.
 */
int ne_kernel_open_file(NEKernelContext *ctx, const char *path,
                         NEOfStruct *ofs, uint16_t style);

/*
 * ne_kernel_output_debug_string - emit a debug message.
 *
 * Prints 'msg' to stderr.
 */
void ne_kernel_output_debug_string(NEKernelContext *ctx, const char *msg);

/*
 * ne_kernel_set_error_mode - set the error handling mode.
 *
 * Returns the previous error mode.
 */
uint16_t ne_kernel_set_error_mode(NEKernelContext *ctx, uint16_t mode);

/*
 * ne_kernel_get_last_error - return the last error code.
 */
uint16_t ne_kernel_get_last_error(NEKernelContext *ctx);

/*
 * ne_kernel_is_task - test whether a handle refers to a valid task.
 *
 * Returns non-zero if 'hTask' is in the task table, 0 otherwise.
 */
int ne_kernel_is_task(NEKernelContext *ctx, uint16_t hTask);

/*
 * ne_kernel_get_num_tasks - return the number of active tasks.
 */
uint16_t ne_kernel_get_num_tasks(NEKernelContext *ctx);

/* =========================================================================
 * Public API – Phase B: INI File and Profile APIs
 * ===================================================================== */

/*
 * ne_kernel_get_profile_string - read a string from WIN.INI.
 *
 * Reads value for 'key' under '[section]'.  If not found, copies
 * 'def' into 'buf'.  Returns the number of characters copied
 * (excluding NUL).
 */
int ne_kernel_get_profile_string(NEKernelContext *ctx,
                                  const char *section,
                                  const char *key,
                                  const char *def,
                                  char *buf, int buf_size);

/*
 * ne_kernel_get_profile_int - read an integer from WIN.INI.
 *
 * Returns the integer value for 'key' under '[section]', or 'def'
 * if not found or not a valid integer.
 */
uint16_t ne_kernel_get_profile_int(NEKernelContext *ctx,
                                    const char *section,
                                    const char *key,
                                    int def);

/*
 * ne_kernel_write_profile_string - write a string to WIN.INI.
 *
 * Sets 'key' = 'value' under '[section]'.  If 'value' is NULL,
 * deletes the key.  If 'key' is NULL, deletes the entire section.
 *
 * Returns non-zero on success, 0 on failure.
 */
int ne_kernel_write_profile_string(NEKernelContext *ctx,
                                    const char *section,
                                    const char *key,
                                    const char *value);

/*
 * ne_kernel_get_private_profile_string - read a string from a
 * specified INI file.
 *
 * Same semantics as ne_kernel_get_profile_string but operates on
 * the file specified by 'filename'.
 */
int ne_kernel_get_private_profile_string(NEKernelContext *ctx,
                                          const char *section,
                                          const char *key,
                                          const char *def,
                                          char *buf, int buf_size,
                                          const char *filename);

/*
 * ne_kernel_get_private_profile_int - read an integer from a
 * specified INI file.
 *
 * Returns the integer value for 'key' under '[section]', or 'def'
 * if not found.
 */
uint16_t ne_kernel_get_private_profile_int(NEKernelContext *ctx,
                                            const char *section,
                                            const char *key,
                                            int def,
                                            const char *filename);

/*
 * ne_kernel_write_private_profile_string - write a string to a
 * specified INI file.
 *
 * Same semantics as ne_kernel_write_profile_string but operates on
 * the file specified by 'filename'.
 */
int ne_kernel_write_private_profile_string(NEKernelContext *ctx,
                                            const char *section,
                                            const char *key,
                                            const char *value,
                                            const char *filename);

#endif /* NE_KERNEL_H */

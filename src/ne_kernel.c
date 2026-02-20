/*
 * ne_kernel.c - Phase 2 KERNEL.EXE API stubs implementation
 *
 * Provides stub or minimal implementations of the most critical
 * KERNEL.EXE exports so that Windows 3.1 applications can link and
 * start.  Each function delegates to the lower-level subsystem tables
 * through the NEKernelContext.
 *
 * File I/O: POSIX open/close/read/write/lseek on the host side;
 * Watcom <io.h> on the DOS 16-bit target (same POSIX-like API).
 */

#include "ne_kernel.h"
#include "ne_driver.h"
#include "ne_dosalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

#ifdef __WATCOMC__
#include <io.h>
#include <fcntl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

/* -------------------------------------------------------------------------
 * Static export catalog
 *
 * Lists every KERNEL.EXE ordinal implemented (or stubbed) in Phase 2
 * together with its classification.
 * ---------------------------------------------------------------------- */

static const NEKernelExportInfo g_catalog[] = {
    /* File I/O – critical */
    { NE_KERNEL_ORD_LOPEN,              "_lopen",              NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_LCLOSE,             "_lclose",             NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_LREAD,              "_lread",              NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_LLSEEK,             "_llseek",             NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_LWRITE,             "_lwrite",             NE_KERNEL_CLASS_CRITICAL  },

    /* Module management – critical */
    { NE_KERNEL_ORD_GET_MODULE_HANDLE,  "GetModuleHandle",     NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_GET_MODULE_FILENAME,"GetModuleFileName",   NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_GET_PROC_ADDRESS,   "GetProcAddress",      NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_LOAD_LIBRARY,       "LoadLibrary",         NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_FREE_LIBRARY,       "FreeLibrary",         NE_KERNEL_CLASS_CRITICAL  },

    /* Global memory – critical */
    { NE_KERNEL_ORD_GLOBAL_ALLOC,       "GlobalAlloc",         NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_GLOBAL_REALLOC,     "GlobalReAlloc",       NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_GLOBAL_FREE,        "GlobalFree",          NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_GLOBAL_LOCK,        "GlobalLock",          NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_GLOBAL_UNLOCK,      "GlobalUnlock",        NE_KERNEL_CLASS_CRITICAL  },

    /* Local memory – critical */
    { NE_KERNEL_ORD_LOCAL_ALLOC,        "LocalAlloc",          NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_LOCAL_FREE,         "LocalFree",           NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_LOCAL_LOCK,         "LocalLock",           NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_LOCAL_UNLOCK,       "LocalUnlock",         NE_KERNEL_CLASS_CRITICAL  },

    /* Task / process – critical */
    { NE_KERNEL_ORD_GET_CURRENT_TASK,   "GetCurrentTask",      NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_YIELD,              "Yield",               NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_INIT_TASK,          "InitTask",            NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_WAIT_EVENT,         "WaitEvent",           NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_POST_EVENT,         "PostEvent",           NE_KERNEL_CLASS_CRITICAL  },

    /* String / resource – secondary (stubs) */
    { NE_KERNEL_ORD_LOAD_STRING,        "LoadString",          NE_KERNEL_CLASS_SECONDARY },
    { NE_KERNEL_ORD_FIND_RESOURCE,      "FindResource",        NE_KERNEL_CLASS_SECONDARY },
    { NE_KERNEL_ORD_LOAD_RESOURCE,      "LoadResource",        NE_KERNEL_CLASS_SECONDARY },
    { NE_KERNEL_ORD_LOCK_RESOURCE,      "LockResource",        NE_KERNEL_CLASS_SECONDARY },

    /* Atom – secondary */
    { NE_KERNEL_ORD_GLOBAL_ADD_ATOM,    "GlobalAddAtom",       NE_KERNEL_CLASS_SECONDARY },
    { NE_KERNEL_ORD_GLOBAL_DELETE_ATOM, "GlobalDeleteAtom",    NE_KERNEL_CLASS_SECONDARY },
    { NE_KERNEL_ORD_GLOBAL_FIND_ATOM,   "GlobalFindAtom",      NE_KERNEL_CLASS_SECONDARY },
    { NE_KERNEL_ORD_GLOBAL_GET_ATOM_NAME,"GlobalGetAtomName",  NE_KERNEL_CLASS_SECONDARY },

    /* Phase A – critical KERNEL.EXE APIs */
    { NE_KERNEL_ORD_GET_VERSION,         "GetVersion",          NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_GET_WIN_FLAGS,       "GetWinFlags",         NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_GET_WINDOWS_DIR,     "GetWindowsDirectory", NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_GET_SYSTEM_DIR,      "GetSystemDirectory",  NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_GET_DOS_ENVIRONMENT, "GetDOSEnvironment",   NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_WIN_EXEC,            "WinExec",             NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_EXIT_WINDOWS,        "ExitWindows",         NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_FATAL_EXIT,          "FatalExit",           NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_FATAL_APP_EXIT,      "FatalAppExit",        NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_GET_TICK_COUNT,      "GetTickCount",        NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_CATCH,               "Catch",               NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_THROW,               "Throw",               NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_MAKE_PROC_INSTANCE,  "MakeProcInstance",    NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_FREE_PROC_INSTANCE,  "FreeProcInstance",    NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_OPEN_FILE,           "OpenFile",            NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_OUTPUT_DEBUG_STRING,  "OutputDebugString",  NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_SET_ERROR_MODE,      "SetErrorMode",        NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_GET_LAST_ERROR,      "GetLastError",        NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_IS_TASK,             "IsTask",              NE_KERNEL_CLASS_CRITICAL  },
    { NE_KERNEL_ORD_GET_NUM_TASKS,       "GetNumTasks",         NE_KERNEL_CLASS_CRITICAL  },
};

#define CATALOG_COUNT \
    ((uint16_t)(sizeof(g_catalog) / sizeof(g_catalog[0])))

/* =========================================================================
 * ne_kernel_init / ne_kernel_free
 * ===================================================================== */

int ne_kernel_init(NEKernelContext *ctx,
                   NEGMemTable     *gmem,
                   NELMemHeap      *lmem,
                   NETaskTable     *tasks,
                   NEModuleTable   *modules)
{
    if (!ctx || !gmem || !lmem || !tasks || !modules)
        return NE_KERNEL_ERR_NULL;

    memset(ctx, 0, sizeof(*ctx));

    ctx->gmem      = gmem;
    ctx->lmem      = lmem;
    ctx->tasks     = tasks;
    ctx->modules   = modules;
    ctx->next_atom = NE_KERNEL_ATOM_BASE;

    ctx->initialized = 1;
    return NE_KERNEL_OK;
}

void ne_kernel_free(NEKernelContext *ctx)
{
    if (!ctx)
        return;

    ne_export_free(&ctx->exports);
    memset(ctx, 0, sizeof(*ctx));
}

/* =========================================================================
 * ne_kernel_get_export_catalog
 * ===================================================================== */

int ne_kernel_get_export_catalog(const NEKernelExportInfo **out,
                                 uint16_t                  *out_count)
{
    if (!out || !out_count)
        return NE_KERNEL_ERR_NULL;

    *out       = g_catalog;
    *out_count = CATALOG_COUNT;
    return NE_KERNEL_OK;
}

/* =========================================================================
 * ne_kernel_register_exports
 * ===================================================================== */

int ne_kernel_register_exports(NEKernelContext *ctx)
{
    uint16_t i;

    if (!ctx || !ctx->initialized)
        return NE_KERNEL_ERR_INIT;

    /* Free any previous export table */
    ne_export_free(&ctx->exports);

    ctx->exports.entries = (NEExportEntry *)NE_CALLOC(CATALOG_COUNT,
                                                       sizeof(NEExportEntry));
    if (!ctx->exports.entries)
        return NE_KERNEL_ERR_NULL;

    for (i = 0; i < CATALOG_COUNT; i++) {
        ctx->exports.entries[i].ordinal = g_catalog[i].ordinal;
        ctx->exports.entries[i].segment = 0u;
        ctx->exports.entries[i].offset  = g_catalog[i].ordinal;
        strncpy(ctx->exports.entries[i].name, g_catalog[i].name,
                NE_EXPORT_NAME_MAX - 1u);
        ctx->exports.entries[i].name[NE_EXPORT_NAME_MAX - 1u] = '\0';
    }

    ctx->exports.count = CATALOG_COUNT;
    return NE_KERNEL_OK;
}

/* =========================================================================
 * File I/O
 * ===================================================================== */

int ne_kernel_lopen(NEKernelContext *ctx, const char *path, uint16_t mode)
{
    int oflags;

    if (!ctx || !ctx->initialized || !path)
        return NE_KERNEL_HFILE_ERROR;

    switch (mode & 0x0003u) {
    case NE_KERNEL_OF_READ:      oflags = O_RDONLY; break;
    case NE_KERNEL_OF_WRITE:     oflags = O_WRONLY; break;
    case NE_KERNEL_OF_READWRITE: oflags = O_RDWR;   break;
    default: return NE_KERNEL_HFILE_ERROR;
    }

#ifdef __WATCOMC__
    oflags |= O_BINARY;
#endif

    return open(path, oflags);
}

int ne_kernel_lclose(NEKernelContext *ctx, int hFile)
{
    if (!ctx || !ctx->initialized)
        return NE_KERNEL_ERR_IO;

    if (hFile < 0)
        return NE_KERNEL_ERR_IO;

    return (close(hFile) == 0) ? NE_KERNEL_OK : NE_KERNEL_ERR_IO;
}

int ne_kernel_lread(NEKernelContext *ctx, int hFile,
                    void *buf, uint16_t count)
{
    int n;

    if (!ctx || !ctx->initialized || !buf)
        return NE_KERNEL_HFILE_ERROR;
    if (hFile < 0)
        return NE_KERNEL_HFILE_ERROR;

    n = (int)read(hFile, buf, (size_t)count);
    return (n >= 0) ? n : NE_KERNEL_HFILE_ERROR;
}

int ne_kernel_lwrite(NEKernelContext *ctx, int hFile,
                     const void *buf, uint16_t count)
{
    int n;

    if (!ctx || !ctx->initialized || !buf)
        return NE_KERNEL_HFILE_ERROR;
    if (hFile < 0)
        return NE_KERNEL_HFILE_ERROR;

    n = (int)write(hFile, buf, (size_t)count);
    return (n >= 0) ? n : NE_KERNEL_HFILE_ERROR;
}

long ne_kernel_llseek(NEKernelContext *ctx, int hFile,
                      long offset, int origin)
{
    long pos;
    int  whence;

    if (!ctx || !ctx->initialized)
        return (long)NE_KERNEL_HFILE_ERROR;
    if (hFile < 0)
        return (long)NE_KERNEL_HFILE_ERROR;

    switch (origin) {
    case NE_KERNEL_FILE_BEGIN:    whence = SEEK_SET; break;
    case NE_KERNEL_FILE_CURRENT: whence = SEEK_CUR; break;
    case NE_KERNEL_FILE_END:     whence = SEEK_END; break;
    default: return (long)NE_KERNEL_HFILE_ERROR;
    }

    pos = (long)lseek(hFile, (off_t)offset, whence);
    return pos;
}

/* =========================================================================
 * Module management
 * ===================================================================== */

uint16_t ne_kernel_get_module_handle(NEKernelContext *ctx, const char *name)
{
    if (!ctx || !ctx->initialized || !ctx->modules || !name)
        return 0;

    return ne_mod_find(ctx->modules, name);
}

int ne_kernel_get_module_filename(NEKernelContext *ctx, uint16_t hModule,
                                  char *buf, int size)
{
    NEModuleEntry *entry;
    int            len;

    if (!ctx || !ctx->initialized || !ctx->modules || !buf || size <= 0)
        return NE_KERNEL_ERR_NULL;

    entry = ne_mod_get(ctx->modules, hModule);
    if (!entry)
        return NE_KERNEL_ERR_NOT_FOUND;

    len = (int)strlen(entry->name);
    if (len >= size)
        len = size - 1;
    memcpy(buf, entry->name, (size_t)len);
    buf[len] = '\0';
    return NE_KERNEL_OK;
}

uint32_t ne_kernel_get_proc_address(NEKernelContext *ctx, uint16_t hModule,
                                     const char *name)
{
    const NEExportEntry *entry;

    if (!ctx || !ctx->initialized || !name)
        return 0;

    (void)hModule; /* look up in the KERNEL export table */

    entry = ne_export_find_by_name(&ctx->exports, name);
    if (entry)
        return ((uint32_t)entry->segment << 16) | entry->offset;

    return 0;
}

uint16_t ne_kernel_load_library(NEKernelContext *ctx, const char *name)
{
    NEModuleHandle h;

    if (!ctx || !ctx->initialized || !ctx->modules || !name)
        return 0;

    h = ne_mod_find(ctx->modules, name);
    if (h != NE_MOD_HANDLE_INVALID)
        ne_mod_addref(ctx->modules, h);

    return h;
}

void ne_kernel_free_library(NEKernelContext *ctx, uint16_t hModule)
{
    if (!ctx || !ctx->initialized || !ctx->modules)
        return;
    if (hModule == NE_MOD_HANDLE_INVALID)
        return;

    ne_mod_unload(ctx->modules, hModule);
}

/* =========================================================================
 * Global memory
 * ===================================================================== */

NEGMemHandle ne_kernel_global_alloc(NEKernelContext *ctx,
                                     uint16_t flags, uint32_t size)
{
    uint16_t owner = 0;

    if (!ctx || !ctx->initialized || !ctx->gmem)
        return NE_GMEM_HANDLE_INVALID;

    if (ctx->tasks && ctx->tasks->current)
        owner = ctx->tasks->current->handle;

    return ne_gmem_alloc(ctx->gmem, flags, size, owner);
}

int ne_kernel_global_free(NEKernelContext *ctx, NEGMemHandle handle)
{
    if (!ctx || !ctx->initialized || !ctx->gmem)
        return NE_KERNEL_ERR_NULL;

    return ne_gmem_free(ctx->gmem, handle);
}

void *ne_kernel_global_lock(NEKernelContext *ctx, NEGMemHandle handle)
{
    if (!ctx || !ctx->initialized || !ctx->gmem)
        return NULL;

    return ne_gmem_lock(ctx->gmem, handle);
}

int ne_kernel_global_unlock(NEKernelContext *ctx, NEGMemHandle handle)
{
    if (!ctx || !ctx->initialized || !ctx->gmem)
        return NE_KERNEL_ERR_NULL;

    return ne_gmem_unlock(ctx->gmem, handle);
}

NEGMemHandle ne_kernel_global_realloc(NEKernelContext *ctx,
                                       NEGMemHandle handle,
                                       uint32_t new_size, uint16_t flags)
{
    NEGMemBlock  *blk;
    NEGMemHandle  new_handle;
    void         *old_data;
    void         *new_data;
    uint32_t      copy_size;

    if (!ctx || !ctx->initialized || !ctx->gmem)
        return NE_GMEM_HANDLE_INVALID;

    blk = ne_gmem_find_block(ctx->gmem, handle);
    if (!blk)
        return NE_GMEM_HANDLE_INVALID;

    new_handle = ne_gmem_alloc(ctx->gmem, flags, new_size, blk->owner_task);
    if (new_handle == NE_GMEM_HANDLE_INVALID)
        return NE_GMEM_HANDLE_INVALID;

    old_data = ne_gmem_lock(ctx->gmem, handle);
    new_data = ne_gmem_lock(ctx->gmem, new_handle);

    if (old_data && new_data) {
        copy_size = (blk->size < new_size) ? blk->size : new_size;
        memcpy(new_data, old_data, (size_t)copy_size);
    }

    ne_gmem_unlock(ctx->gmem, handle);
    ne_gmem_unlock(ctx->gmem, new_handle);
    ne_gmem_free(ctx->gmem, handle);

    return new_handle;
}

/* =========================================================================
 * Local memory
 * ===================================================================== */

NELMemHandle ne_kernel_local_alloc(NEKernelContext *ctx,
                                    uint16_t flags, uint16_t size)
{
    if (!ctx || !ctx->initialized || !ctx->lmem)
        return NE_LMEM_HANDLE_INVALID;

    return ne_lmem_alloc(ctx->lmem, flags, size);
}

int ne_kernel_local_free(NEKernelContext *ctx, NELMemHandle handle)
{
    if (!ctx || !ctx->initialized || !ctx->lmem)
        return NE_KERNEL_ERR_NULL;

    return ne_lmem_free(ctx->lmem, handle);
}

void *ne_kernel_local_lock(NEKernelContext *ctx, NELMemHandle handle)
{
    if (!ctx || !ctx->initialized || !ctx->lmem)
        return NULL;

    return ne_lmem_lock(ctx->lmem, handle);
}

int ne_kernel_local_unlock(NEKernelContext *ctx, NELMemHandle handle)
{
    if (!ctx || !ctx->initialized || !ctx->lmem)
        return NE_KERNEL_ERR_NULL;

    return ne_lmem_unlock(ctx->lmem, handle);
}

/* =========================================================================
 * Task / process management
 * ===================================================================== */

uint16_t ne_kernel_get_current_task(NEKernelContext *ctx)
{
    if (!ctx || !ctx->initialized || !ctx->tasks)
        return 0;

    if (ctx->tasks->current)
        return ctx->tasks->current->handle;

    return 0;
}

void ne_kernel_yield(NEKernelContext *ctx)
{
    if (!ctx || !ctx->initialized || !ctx->tasks)
        return;

    ne_task_yield(ctx->tasks);
}

int ne_kernel_init_task(NEKernelContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return NE_KERNEL_ERR_INIT;

    /* Stub: no additional initialisation required at this phase. */
    return NE_KERNEL_OK;
}

int ne_kernel_wait_event(NEKernelContext *ctx, uint16_t hTask)
{
    if (!ctx || !ctx->initialized || !ctx->tasks)
        return NE_KERNEL_ERR_NULL;

    (void)hTask;

    /* Stub: event is considered immediately available. */
    return NE_KERNEL_OK;
}

int ne_kernel_post_event(NEKernelContext *ctx, uint16_t hTask)
{
    if (!ctx || !ctx->initialized || !ctx->tasks)
        return NE_KERNEL_ERR_NULL;

    (void)hTask;

    /* Stub: event is considered posted successfully. */
    return NE_KERNEL_OK;
}

/* =========================================================================
 * String / resource stubs
 * ===================================================================== */

int ne_kernel_load_string(NEKernelContext *ctx, uint16_t hModule,
                           uint16_t uID, char *buf, int buf_size)
{
    (void)hModule;
    (void)uID;

    if (!ctx || !ctx->initialized || !buf || buf_size <= 0)
        return 0;

    /* Stub: no string resource tables are loaded yet. */
    buf[0] = '\0';
    return 0;
}

uint32_t ne_kernel_find_resource(NEKernelContext *ctx, uint16_t hModule,
                                  const char *name, const char *type)
{
    (void)hModule;
    (void)name;
    (void)type;

    if (!ctx || !ctx->initialized)
        return 0;

    /* Stub: resource management is deferred to Phase 5. */
    return 0;
}

uint16_t ne_kernel_load_resource(NEKernelContext *ctx, uint16_t hModule,
                                  uint32_t hResInfo)
{
    (void)hModule;
    (void)hResInfo;

    if (!ctx || !ctx->initialized)
        return 0;

    /* Stub: resource management is deferred to Phase 5. */
    return 0;
}

void *ne_kernel_lock_resource(NEKernelContext *ctx, uint16_t hResData)
{
    (void)hResData;

    if (!ctx || !ctx->initialized)
        return NULL;

    /* Stub: resource management is deferred to Phase 5. */
    return NULL;
}

/* =========================================================================
 * Atom table
 * ===================================================================== */

uint16_t ne_kernel_global_add_atom(NEKernelContext *ctx, const char *name)
{
    uint16_t existing;
    uint16_t i;

    if (!ctx || !ctx->initialized || !name || name[0] == '\0')
        return NE_KERNEL_ATOM_INVALID;

    /* Return existing atom if already registered */
    existing = ne_kernel_global_find_atom(ctx, name);
    if (existing != NE_KERNEL_ATOM_INVALID)
        return existing;

    if (ctx->atom_count >= NE_KERNEL_ATOM_TABLE_CAP)
        return NE_KERNEL_ATOM_INVALID;

    i = ctx->atom_count;
    ctx->atoms[i].atom = ctx->next_atom++;

    strncpy(ctx->atoms[i].name, name, NE_KERNEL_ATOM_NAME_MAX - 1u);
    ctx->atoms[i].name[NE_KERNEL_ATOM_NAME_MAX - 1u] = '\0';

    ctx->atom_count++;
    return ctx->atoms[i].atom;
}

uint16_t ne_kernel_global_find_atom(NEKernelContext *ctx, const char *name)
{
    uint16_t i;

    if (!ctx || !ctx->initialized || !name || name[0] == '\0')
        return NE_KERNEL_ATOM_INVALID;

    for (i = 0; i < ctx->atom_count; i++) {
        if (strncmp(ctx->atoms[i].name, name,
                    NE_KERNEL_ATOM_NAME_MAX) == 0)
            return ctx->atoms[i].atom;
    }

    return NE_KERNEL_ATOM_INVALID;
}

int ne_kernel_global_get_atom_name(NEKernelContext *ctx, uint16_t atom,
                                    char *buf, int size)
{
    uint16_t i;
    int      len;

    if (!ctx || !ctx->initialized || !buf || size <= 0)
        return NE_KERNEL_ERR_NULL;

    if (atom == NE_KERNEL_ATOM_INVALID)
        return NE_KERNEL_ERR_NOT_FOUND;

    for (i = 0; i < ctx->atom_count; i++) {
        if (ctx->atoms[i].atom == atom) {
            len = (int)strlen(ctx->atoms[i].name);
            if (len >= size)
                len = size - 1;
            memcpy(buf, ctx->atoms[i].name, (size_t)len);
            buf[len] = '\0';
            return NE_KERNEL_OK;
        }
    }

    return NE_KERNEL_ERR_NOT_FOUND;
}

int ne_kernel_global_delete_atom(NEKernelContext *ctx, uint16_t atom)
{
    uint16_t i;

    if (!ctx || !ctx->initialized)
        return NE_KERNEL_ERR_NULL;

    if (atom == NE_KERNEL_ATOM_INVALID)
        return NE_KERNEL_ERR_NOT_FOUND;

    for (i = 0; i < ctx->atom_count; i++) {
        if (ctx->atoms[i].atom == atom) {
            /* Shift remaining entries down */
            if (i + 1u < ctx->atom_count) {
                memmove(&ctx->atoms[i], &ctx->atoms[i + 1u],
                        (size_t)(ctx->atom_count - i - 1u)
                        * sizeof(ctx->atoms[0]));
            }
            ctx->atom_count--;
            memset(&ctx->atoms[ctx->atom_count], 0,
                   sizeof(ctx->atoms[0]));
            return NE_KERNEL_OK;
        }
    }

    return NE_KERNEL_ERR_NOT_FOUND;
}

/* =========================================================================
 * ne_kernel_strerror
 * ===================================================================== */

const char *ne_kernel_strerror(int err)
{
    switch (err) {
    case NE_KERNEL_OK:             return "success";
    case NE_KERNEL_ERR_NULL:       return "NULL pointer argument";
    case NE_KERNEL_ERR_INIT:       return "kernel context not initialised";
    case NE_KERNEL_ERR_IO:         return "file I/O failure";
    case NE_KERNEL_ERR_FULL:       return "table at capacity";
    case NE_KERNEL_ERR_NOT_FOUND:  return "item not found";
    default:                       return "unknown error";
    }
}

/* =========================================================================
 * Phase A: Critical KERNEL.EXE APIs
 * ===================================================================== */

int ne_kernel_set_driver(NEKernelContext *ctx, void *driver)
{
    if (!ctx || !ctx->initialized)
        return NE_KERNEL_ERR_INIT;

    ctx->driver = driver;
    return NE_KERNEL_OK;
}

uint16_t ne_kernel_get_version(NEKernelContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;

    /* Windows 3.10: low byte = major (3), high byte = minor (10) */
    return 0x0A03u;
}

uint32_t ne_kernel_get_win_flags(NEKernelContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;

    /* WinDOS runs in real mode on 8086 – no protected-mode flags */
    return 0;
}

int ne_kernel_get_windows_directory(NEKernelContext *ctx,
                                     char *buf, int size)
{
    const char *dir = "C:\\WINDOWS";
    int len;

    if (!ctx || !ctx->initialized || !buf || size <= 0)
        return 0;

    len = (int)strlen(dir);
    if (len >= size)
        len = size - 1;
    memcpy(buf, dir, (size_t)len);
    buf[len] = '\0';
    return len;
}

int ne_kernel_get_system_directory(NEKernelContext *ctx,
                                    char *buf, int size)
{
    const char *dir = "C:\\WINDOWS\\SYSTEM";
    int len;

    if (!ctx || !ctx->initialized || !buf || size <= 0)
        return 0;

    len = (int)strlen(dir);
    if (len >= size)
        len = size - 1;
    memcpy(buf, dir, (size_t)len);
    buf[len] = '\0';
    return len;
}

const char *ne_kernel_get_dos_environment(NEKernelContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return NULL;

    /* Stub: no DOS environment block */
    return NULL;
}

uint16_t ne_kernel_win_exec(NEKernelContext *ctx, const char *cmdLine,
                             uint16_t cmdShow)
{
    (void)cmdLine;
    (void)cmdShow;

    if (!ctx || !ctx->initialized)
        return 0;

    /* Stub: child process launching is not yet implemented */
    return 0;
}

int ne_kernel_exit_windows(NEKernelContext *ctx, uint32_t dwReserved)
{
    (void)dwReserved;

    if (!ctx || !ctx->initialized)
        return 0;

    /* Stub: clean shutdown not yet implemented */
    return 0;
}

void ne_kernel_fatal_exit(NEKernelContext *ctx, int code)
{
    (void)ctx;
    exit(code);
}

void ne_kernel_fatal_app_exit(NEKernelContext *ctx, uint16_t action,
                               const char *msg)
{
    (void)ctx;
    (void)action;

    if (msg)
        fprintf(stderr, "FatalAppExit: %s\n", msg);
    exit(1);
}

uint32_t ne_kernel_get_tick_count(NEKernelContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;

    if (ctx->driver)
        return ne_drv_get_tick_count((const NEDrvContext *)ctx->driver);

    return 0;
}

void ne_kernel_throw(NEKernelContext *ctx, NECatchBuf *buf, int retval)
{
    (void)ctx;
    longjmp(buf->env, retval);
}

void *ne_kernel_make_proc_instance(NEKernelContext *ctx, void *proc,
                                    uint16_t hInstance)
{
    (void)hInstance;

    if (!ctx || !ctx->initialized)
        return NULL;

    /* Real mode: no thunk needed, return the pointer as-is */
    return proc;
}

void ne_kernel_free_proc_instance(NEKernelContext *ctx, void *proc)
{
    (void)ctx;
    (void)proc;
    /* No-op in real mode */
}

int ne_kernel_open_file(NEKernelContext *ctx, const char *path,
                         NEOfStruct *ofs, uint16_t style)
{
    int hFile;

    if (!ctx || !ctx->initialized || !path || !ofs)
        return NE_KERNEL_HFILE_ERROR;

    memset(ofs, 0, sizeof(*ofs));
    ofs->cBytes = (uint8_t)sizeof(*ofs);

    /* Copy path into ofstruct */
    strncpy(ofs->szPathName, path, NE_OFS_MAXPATHNAME - 1u);
    ofs->szPathName[NE_OFS_MAXPATHNAME - 1u] = '\0';

    /* Handle OF_DELETE */
    if (style & NE_OF_DELETE) {
        if (remove(path) == 0) {
            ofs->nErrCode = 0;
            return 0;
        }
        ofs->nErrCode = 2; /* file not found */
        return NE_KERNEL_HFILE_ERROR;
    }

    /* Handle OF_EXIST: just test if file can be opened */
    if (style & NE_OF_EXIST) {
        hFile = ne_kernel_lopen(ctx, path, NE_KERNEL_OF_READ);
        if (hFile != NE_KERNEL_HFILE_ERROR) {
            ne_kernel_lclose(ctx, hFile);
            ofs->nErrCode = 0;
            return 0;
        }
        ofs->nErrCode = 2;
        return NE_KERNEL_HFILE_ERROR;
    }

    /* Normal open */
    hFile = ne_kernel_lopen(ctx, path, style & 0x0003u);
    if (hFile == NE_KERNEL_HFILE_ERROR) {
        ofs->nErrCode = 2;
        return NE_KERNEL_HFILE_ERROR;
    }

    ofs->nErrCode = 0;
    return hFile;
}

void ne_kernel_output_debug_string(NEKernelContext *ctx, const char *msg)
{
    (void)ctx;

    if (msg)
        fprintf(stderr, "[DEBUG] %s\n", msg);
}

uint16_t ne_kernel_set_error_mode(NEKernelContext *ctx, uint16_t mode)
{
    uint16_t prev;

    if (!ctx || !ctx->initialized)
        return 0;

    prev = ctx->error_mode;
    ctx->error_mode = mode;
    return prev;
}

uint16_t ne_kernel_get_last_error(NEKernelContext *ctx)
{
    if (!ctx || !ctx->initialized)
        return 0;

    return ctx->last_error;
}

int ne_kernel_is_task(NEKernelContext *ctx, uint16_t hTask)
{
    if (!ctx || !ctx->initialized || !ctx->tasks)
        return 0;

    if (ne_task_get(ctx->tasks, hTask) != NULL)
        return 1;

    return 0;
}

uint16_t ne_kernel_get_num_tasks(NEKernelContext *ctx)
{
    if (!ctx || !ctx->initialized || !ctx->tasks)
        return 0;

    return ctx->tasks->count;
}

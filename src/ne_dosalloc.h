/*
 * ne_dosalloc.h - Portable memory allocation macros
 *
 * On the Watcom/DOS 16-bit target these macros expand to DOS INT 21h
 * conventional-memory allocation (AH=48h / AH=49h / AH=4Ah).
 * On the POSIX host they expand to the standard C library calls.
 *
 * Phase 1 of the WinDOS roadmap: replace all host malloc/free calls with
 * these macros so the codebase builds and runs on the DOS target.
 */

#ifndef NE_DOSALLOC_H
#define NE_DOSALLOC_H

#include <stdlib.h>
#include <string.h>

#ifdef __WATCOMC__

#include <dos.h>
#include <i86.h>

/*
 * DOS INT 21h memory allocation helpers.
 *
 * AH=48h – Allocate memory block: BX = paragraphs, returns segment in AX.
 * AH=49h – Free memory block:     ES = segment to free.
 *
 * All allocations return far pointers (segment:0000) pointing to the
 * start of the allocated conventional memory block.
 */

/* Convert byte count to paragraph count (16-byte units), rounding up. */
static uint16_t ne_dos_bytes_to_paras(uint32_t bytes)
{
    return (uint16_t)((bytes + 15u) >> 4);
}

/*
 * ne_dos_alloc - allocate conventional memory via INT 21h / AH=48h.
 * Returns a far pointer to the block or NULL on failure.
 */
static void __far *ne_dos_alloc(uint32_t size)
{
    union REGS   r;
    struct SREGS sr;

    if (size == 0)
        return NULL;

    r.h.ah = 0x48;
    r.x.bx = ne_dos_bytes_to_paras(size);
    intdosx(&r, &r, &sr);

    if (r.x.cflag)
        return NULL;

    return MK_FP(r.x.ax, 0);
}

/*
 * ne_dos_free - release conventional memory via INT 21h / AH=49h.
 */
static void ne_dos_free(void __far *ptr)
{
    union REGS   r;
    struct SREGS sr;

    if (ptr == NULL)
        return;

    segread(&sr);
    sr.es  = FP_SEG(ptr);
    r.h.ah = 0x49;
    intdosx(&r, &r, &sr);
}

/*
 * ne_dos_calloc - allocate and zero-fill via ne_dos_alloc + _fmemset.
 */
static void __far *ne_dos_calloc(uint32_t count, uint32_t size)
{
    uint32_t     total = count * size;
    void __far  *p     = ne_dos_alloc(total);

    if (p)
        _fmemset(p, 0, (size_t)total);
    return p;
}

#define NE_MALLOC(sz)      ne_dos_alloc((uint32_t)(sz))
#define NE_CALLOC(n, sz)   ne_dos_calloc((uint32_t)(n), (uint32_t)(sz))
#define NE_FREE(p)         ne_dos_free(p)

#else /* POSIX host */

#define NE_MALLOC(sz)      malloc((size_t)(sz))
#define NE_CALLOC(n, sz)   calloc((size_t)(n), (size_t)(sz))
#define NE_FREE(p)         free(p)

#endif /* __WATCOMC__ */

#endif /* NE_DOSALLOC_H */

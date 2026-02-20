/*
 * ne_dpmi.h - Protected-mode (DPMI) support for WinDOS
 *
 * Implements Phase H of the KERNEL_ASSESSMENT.md roadmap:
 *   - Minimal DPMI server (INT 31h) for 16-bit protected mode
 *   - Selector allocation and management (AllocSelector, FreeSelector,
 *     ChangeSelector)
 *   - Extended memory access via DPMI
 *   - Descriptor table management
 *
 * Host-side (POSIX/GCC) implementation: all state is maintained in the
 * NEDpmiContext structure.  Actual INT 31h installation and descriptor
 * table manipulation are not performed on the host; they require a real
 * 16-bit DOS environment with DPMI host services.
 *
 * Watcom/DOS 16-bit target: the DPMI server installs an INT 31h handler
 * that dispatches DPMI function calls.  Selector operations manipulate
 * the Local Descriptor Table (LDT) via DPMI host calls or direct LDT
 * access.  Extended memory is managed through DPMI memory allocation
 * functions (AX=0501h/0502h).
 *
 * Reference: DPMI Specification Version 0.9 / 1.0,
 *            Intel 80286/386 Protected Mode Programming.
 */

#ifndef NE_DPMI_H
#define NE_DPMI_H

#include <stdint.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Error codes
 * ---------------------------------------------------------------------- */
#define NE_DPMI_OK              0
#define NE_DPMI_ERR_NULL       -1   /* NULL pointer argument                */
#define NE_DPMI_ERR_FULL       -2   /* selector/descriptor table full       */
#define NE_DPMI_ERR_BAD_SEL    -3   /* invalid selector value               */
#define NE_DPMI_ERR_NO_MEM     -4   /* extended memory allocation failed    */
#define NE_DPMI_ERR_NOT_INIT   -5   /* context not initialised              */
#define NE_DPMI_ERR_BAD_FUNC   -6   /* unsupported DPMI function            */

/* -------------------------------------------------------------------------
 * Configuration constants
 * ---------------------------------------------------------------------- */
#define NE_DPMI_MAX_SELECTORS  256u  /* max number of allocated selectors   */
#define NE_DPMI_MAX_EXTBLOCKS   64u  /* max extended memory blocks          */
#define NE_DPMI_MAX_DESCRIPTORS 256u /* max descriptor table entries        */

/* DPMI version reported by this implementation */
#define NE_DPMI_VERSION_MAJOR    0
#define NE_DPMI_VERSION_MINOR    9

/* -------------------------------------------------------------------------
 * DPMI INT 31h function codes (subset)
 *
 * Only the functions required for Windows 3.1 Standard-mode operation
 * are listed here.
 * ---------------------------------------------------------------------- */
#define NE_DPMI_FN_ALLOC_LDT          0x0000u  /* Allocate LDT descriptors */
#define NE_DPMI_FN_FREE_LDT           0x0001u  /* Free LDT descriptor      */
#define NE_DPMI_FN_SEG_TO_DESC        0x0002u  /* Segment to descriptor    */
#define NE_DPMI_FN_GET_SEL_INC        0x0003u  /* Get selector increment   */
#define NE_DPMI_FN_GET_DESC           0x000Bu  /* Get descriptor           */
#define NE_DPMI_FN_SET_DESC           0x000Cu  /* Set descriptor           */
#define NE_DPMI_FN_ALLOC_MEM          0x0501u  /* Allocate memory block    */
#define NE_DPMI_FN_FREE_MEM           0x0502u  /* Free memory block        */
#define NE_DPMI_FN_RESIZE_MEM         0x0503u  /* Resize memory block      */
#define NE_DPMI_FN_GET_VERSION        0x0400u  /* Get DPMI version         */

/* -------------------------------------------------------------------------
 * Selector constants
 *
 * Selectors are 16-bit values used in protected mode to index into the
 * descriptor table.  Bits 0–1 are the RPL (Requested Privilege Level),
 * bit 2 is the TI (Table Indicator: 0 = GDT, 1 = LDT), and bits 3–15
 * are the descriptor index.
 * ---------------------------------------------------------------------- */
#define NE_DPMI_SEL_INVALID       0u       /* invalid/null selector        */
#define NE_DPMI_SEL_RPL_MASK      0x0003u  /* RPL field mask               */
#define NE_DPMI_SEL_TI_LDT       0x0004u  /* LDT table indicator          */
#define NE_DPMI_SEL_INDEX_SHIFT   3u       /* shift for descriptor index   */
#define NE_DPMI_SEL_INCREMENT     8u       /* selector increment value     */

/* -------------------------------------------------------------------------
 * Descriptor access rights
 * ---------------------------------------------------------------------- */
#define NE_DPMI_DESC_PRESENT       0x80u  /* segment present               */
#define NE_DPMI_DESC_DPL_MASK      0x60u  /* descriptor privilege level    */
#define NE_DPMI_DESC_DPL_SHIFT     5u
#define NE_DPMI_DESC_CODE_SEG      0x18u  /* code segment type             */
#define NE_DPMI_DESC_DATA_SEG      0x10u  /* data segment type             */
#define NE_DPMI_DESC_READABLE      0x02u  /* readable (code) / writable    */
#define NE_DPMI_DESC_WRITABLE      0x02u  /* writable (data)               */
#define NE_DPMI_DESC_CONFORMING    0x04u  /* conforming code segment       */
#define NE_DPMI_DESC_EXPAND_DOWN   0x04u  /* expand-down data segment      */
#define NE_DPMI_DESC_ACCESSED      0x01u  /* segment has been accessed     */

/* Default access byte for a writable data segment at DPL 3 */
#define NE_DPMI_DESC_DATA_RW  (NE_DPMI_DESC_PRESENT | \
                                (3u << NE_DPMI_DESC_DPL_SHIFT) | \
                                NE_DPMI_DESC_DATA_SEG | \
                                NE_DPMI_DESC_WRITABLE)

/* Default access byte for a readable code segment at DPL 3 */
#define NE_DPMI_DESC_CODE_RX  (NE_DPMI_DESC_PRESENT | \
                                (3u << NE_DPMI_DESC_DPL_SHIFT) | \
                                NE_DPMI_DESC_CODE_SEG | \
                                NE_DPMI_DESC_READABLE)

/* -------------------------------------------------------------------------
 * Descriptor structure
 *
 * 8-byte descriptor matching the Intel protected-mode segment descriptor
 * format.  On the host the fields are stored directly; on a real 286/386
 * they would be packed into the 8-byte GDT/LDT entry format.
 * ---------------------------------------------------------------------- */
typedef struct {
    uint32_t base;        /* segment base address (linear)                  */
    uint32_t limit;       /* segment limit in bytes                         */
    uint8_t  access;      /* access rights byte (P, DPL, type)             */
    uint8_t  flags;       /* granularity / D/B / AVL flags (386+)          */
    uint8_t  in_use;      /* non-zero if this descriptor is allocated       */
} NEDpmiDescriptor;

/* -------------------------------------------------------------------------
 * Selector table entry
 *
 * Tracks an allocated selector and its associated descriptor index.
 * ---------------------------------------------------------------------- */
typedef struct {
    uint16_t selector;         /* the selector value                        */
    uint16_t desc_index;       /* index into the descriptor table           */
    uint8_t  in_use;           /* non-zero if this entry is allocated       */
} NEDpmiSelectorEntry;

/* -------------------------------------------------------------------------
 * Extended memory block
 *
 * Tracks a DPMI-allocated extended memory block.
 * ---------------------------------------------------------------------- */
typedef struct {
    uint32_t handle;           /* DPMI memory block handle                  */
    uint32_t base;             /* linear base address                       */
    uint32_t size;             /* size in bytes                             */
    uint8_t  in_use;           /* non-zero if allocated                     */
} NEDpmiExtBlock;

/* -------------------------------------------------------------------------
 * DPMI context
 *
 * Aggregates all DPMI state.  Initialise with ne_dpmi_init(); release
 * with ne_dpmi_free().
 * ---------------------------------------------------------------------- */
typedef struct {
    NEDpmiDescriptor    descriptors[NE_DPMI_MAX_DESCRIPTORS];
    NEDpmiSelectorEntry selectors[NE_DPMI_MAX_SELECTORS];
    NEDpmiExtBlock      ext_blocks[NE_DPMI_MAX_EXTBLOCKS];

    uint16_t sel_count;        /* number of allocated selectors             */
    uint16_t desc_count;       /* number of allocated descriptors           */
    uint16_t ext_count;        /* number of allocated ext memory blocks     */
    uint32_t next_ext_handle;  /* next handle value for ext memory          */
    uint32_t next_base;        /* next linear base address for simulation   */

    int      server_active;    /* non-zero if DPMI server is active         */
    int      initialized;      /* non-zero after successful init            */
} NEDpmiContext;

/* =========================================================================
 * Public API – initialisation
 * ===================================================================== */

/*
 * ne_dpmi_init - initialise the DPMI context.
 *
 * Zeroes all tables and marks the context as initialised.
 *
 * Returns NE_DPMI_OK or NE_DPMI_ERR_NULL.
 */
int ne_dpmi_init(NEDpmiContext *ctx);

/*
 * ne_dpmi_free - release resources and zero the context.
 *
 * Safe to call on a zeroed context or NULL.
 */
void ne_dpmi_free(NEDpmiContext *ctx);

/* =========================================================================
 * Public API – DPMI server (INT 31h)
 * ===================================================================== */

/*
 * ne_dpmi_server_start - activate the DPMI server.
 *
 * On the Watcom/DOS target this installs the INT 31h handler.
 * On the host it marks the server as active.
 *
 * Returns NE_DPMI_OK or NE_DPMI_ERR_NULL / NE_DPMI_ERR_NOT_INIT.
 */
int ne_dpmi_server_start(NEDpmiContext *ctx);

/*
 * ne_dpmi_server_stop - deactivate the DPMI server.
 *
 * On the Watcom/DOS target this restores the original INT 31h handler.
 *
 * Returns NE_DPMI_OK or NE_DPMI_ERR_NULL / NE_DPMI_ERR_NOT_INIT.
 */
int ne_dpmi_server_stop(NEDpmiContext *ctx);

/*
 * ne_dpmi_server_is_active - query whether the DPMI server is running.
 *
 * Returns non-zero if the server is active, 0 otherwise.
 */
int ne_dpmi_server_is_active(const NEDpmiContext *ctx);

/*
 * ne_dpmi_dispatch - dispatch a DPMI INT 31h function call.
 *
 * 'func' is the DPMI function number (AX register value).
 * 'bx', 'cx', 'dx', 'si', 'di' are the register parameters.
 * 'out_ax', 'out_bx', 'out_cx', 'out_dx', 'out_si', 'out_di' receive
 * the return register values (any may be NULL to ignore).
 *
 * Returns NE_DPMI_OK on success (carry flag clear) or a negative error
 * code on failure (carry flag set).
 */
int ne_dpmi_dispatch(NEDpmiContext *ctx,
                     uint16_t func,
                     uint16_t bx, uint16_t cx,
                     uint16_t dx, uint16_t si, uint16_t di,
                     uint16_t *out_ax, uint16_t *out_bx,
                     uint16_t *out_cx, uint16_t *out_dx,
                     uint16_t *out_si, uint16_t *out_di);

/*
 * ne_dpmi_get_version - return the DPMI version.
 *
 * Sets *major and *minor to the DPMI version numbers.
 *
 * Returns NE_DPMI_OK or NE_DPMI_ERR_NULL.
 */
int ne_dpmi_get_version(const NEDpmiContext *ctx,
                        uint8_t *major, uint8_t *minor);

/* =========================================================================
 * Public API – selector management
 * ===================================================================== */

/*
 * ne_dpmi_alloc_selector - allocate a new LDT selector.
 *
 * 'src_sel' is a source selector to copy; use 0 to create a fresh
 * selector with default data-segment access rights.
 *
 * Returns the new selector value, or NE_DPMI_SEL_INVALID on failure.
 */
uint16_t ne_dpmi_alloc_selector(NEDpmiContext *ctx, uint16_t src_sel);

/*
 * ne_dpmi_free_selector - free a previously allocated selector.
 *
 * Returns NE_DPMI_OK, NE_DPMI_ERR_NULL, or NE_DPMI_ERR_BAD_SEL.
 */
int ne_dpmi_free_selector(NEDpmiContext *ctx, uint16_t selector);

/*
 * ne_dpmi_change_selector - change a selector between code and data types.
 *
 * Copies the descriptor from 'src_sel' to 'dst_sel' and toggles the
 * code/data type bit.  If 'src_sel' is a code segment, 'dst_sel'
 * becomes data; if 'src_sel' is data, 'dst_sel' becomes code.
 *
 * Returns NE_DPMI_OK, NE_DPMI_ERR_NULL, or NE_DPMI_ERR_BAD_SEL.
 */
int ne_dpmi_change_selector(NEDpmiContext *ctx,
                            uint16_t dst_sel, uint16_t src_sel);

/*
 * ne_dpmi_get_selector_count - return the number of allocated selectors.
 */
uint16_t ne_dpmi_get_selector_count(const NEDpmiContext *ctx);

/* =========================================================================
 * Public API – descriptor table management
 * ===================================================================== */

/*
 * ne_dpmi_get_descriptor - retrieve the descriptor for a selector.
 *
 * Copies the descriptor contents into *desc.
 *
 * Returns NE_DPMI_OK, NE_DPMI_ERR_NULL, or NE_DPMI_ERR_BAD_SEL.
 */
int ne_dpmi_get_descriptor(const NEDpmiContext *ctx, uint16_t selector,
                           NEDpmiDescriptor *desc);

/*
 * ne_dpmi_set_descriptor - set the descriptor for a selector.
 *
 * Copies *desc into the descriptor table entry for 'selector'.
 *
 * Returns NE_DPMI_OK, NE_DPMI_ERR_NULL, or NE_DPMI_ERR_BAD_SEL.
 */
int ne_dpmi_set_descriptor(NEDpmiContext *ctx, uint16_t selector,
                           const NEDpmiDescriptor *desc);

/*
 * ne_dpmi_set_segment_base - set the base address of a selector.
 *
 * Returns NE_DPMI_OK, NE_DPMI_ERR_NULL, or NE_DPMI_ERR_BAD_SEL.
 */
int ne_dpmi_set_segment_base(NEDpmiContext *ctx, uint16_t selector,
                             uint32_t base);

/*
 * ne_dpmi_get_segment_base - get the base address of a selector.
 *
 * Returns NE_DPMI_OK, NE_DPMI_ERR_NULL, or NE_DPMI_ERR_BAD_SEL.
 */
int ne_dpmi_get_segment_base(const NEDpmiContext *ctx, uint16_t selector,
                             uint32_t *base);

/*
 * ne_dpmi_set_segment_limit - set the limit of a selector.
 *
 * Returns NE_DPMI_OK, NE_DPMI_ERR_NULL, or NE_DPMI_ERR_BAD_SEL.
 */
int ne_dpmi_set_segment_limit(NEDpmiContext *ctx, uint16_t selector,
                              uint32_t limit);

/* =========================================================================
 * Public API – extended memory access via DPMI
 * ===================================================================== */

/*
 * ne_dpmi_alloc_ext_memory - allocate an extended memory block.
 *
 * 'size' is the block size in bytes.  On success, *handle receives the
 * block handle and *base receives the linear base address.
 *
 * Returns NE_DPMI_OK, NE_DPMI_ERR_NULL, NE_DPMI_ERR_NO_MEM, or
 * NE_DPMI_ERR_FULL.
 */
int ne_dpmi_alloc_ext_memory(NEDpmiContext *ctx, uint32_t size,
                             uint32_t *handle, uint32_t *base);

/*
 * ne_dpmi_free_ext_memory - free an extended memory block.
 *
 * Returns NE_DPMI_OK, NE_DPMI_ERR_NULL, or NE_DPMI_ERR_BAD_SEL
 * (handle not found).
 */
int ne_dpmi_free_ext_memory(NEDpmiContext *ctx, uint32_t handle);

/*
 * ne_dpmi_resize_ext_memory - resize an extended memory block.
 *
 * 'new_size' is the new size in bytes.  On success, *new_base receives
 * the (possibly changed) linear base address.
 *
 * Returns NE_DPMI_OK, NE_DPMI_ERR_NULL, or NE_DPMI_ERR_BAD_SEL
 * (handle not found).
 */
int ne_dpmi_resize_ext_memory(NEDpmiContext *ctx, uint32_t handle,
                              uint32_t new_size, uint32_t *new_base);

/*
 * ne_dpmi_get_ext_memory_count - return the number of allocated blocks.
 */
uint16_t ne_dpmi_get_ext_memory_count(const NEDpmiContext *ctx);

/* =========================================================================
 * Public API – error string
 * ===================================================================== */

/*
 * ne_dpmi_strerror - return a static string describing error code 'err'.
 */
const char *ne_dpmi_strerror(int err);

#endif /* NE_DPMI_H */

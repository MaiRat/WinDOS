# NE-Executable and DOS Stub Loader Design

## Overview

The original Windows 3.1 `krnl386.exe` is a **New Executable (NE)**
format binary with a special self-loading DOS stub.  This document
describes the NE executable format, the role of the DOS stub loader,
and how the WinDOS replacement `krnl386.exe` is built using the Open
Watcom toolchain.

---

## 1 – NE Executable Format

### 1.1 File Layout

An NE-format executable consists of two parts:

```
┌──────────────────────────────┐  offset 0
│  MZ (DOS) Stub Header        │  Standard DOS MZ executable header
│  + DOS stub code              │  Runs when executed from plain DOS
├──────────────────────────────┤  offset from MZ header e_lfanew
│  NE Header                    │  'NE' signature + module metadata
│  ├── Segment Table            │  Code/data segment descriptors
│  ├── Resource Table           │  Resources (strings, dialogs, etc.)
│  ├── Resident Name Table      │  Exported names (always in memory)
│  ├── Module Reference Table   │  Imported module names
│  ├── Imported Names Table     │  Imported procedure names
│  ├── Entry Table              │  Exported entry points by ordinal
│  └── Non-Resident Name Table  │  Exported names (loaded on demand)
├──────────────────────────────┤
│  Segment Data                 │  Code and data segment contents
│  ├── Segment 1 (CODE)         │
│  ├── Segment 2 (DATA)         │
│  └── ...                      │
└──────────────────────────────┘
```

### 1.2 Key NE Header Fields

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 2 | `ne_magic` | Signature bytes `'N'`, `'E'` (0x454E) |
| 0x02 | 1 | `ne_ver` | Linker version |
| 0x03 | 1 | `ne_rev` | Linker revision |
| 0x04 | 2 | `ne_enttab` | Offset to entry table |
| 0x06 | 2 | `ne_cbenttab` | Size of entry table in bytes |
| 0x0C | 2 | `ne_flags` | Module flags (SINGLEDATA, MULTIPLEDATA, etc.) |
| 0x0E | 2 | `ne_autodata` | Auto data segment number |
| 0x14 | 2 | `ne_csip` | Initial CS:IP (entry point) |
| 0x16 | 2 | `ne_sssp` | Initial SS:SP (stack) |
| 0x1C | 2 | `ne_cseg` | Number of segments |
| 0x1E | 2 | `ne_cmod` | Number of module references |
| 0x22 | 2 | `ne_segtab` | Offset to segment table |
| 0x24 | 2 | `ne_rsrctab` | Offset to resource table |
| 0x26 | 2 | `ne_restab` | Offset to resident name table |
| 0x28 | 2 | `ne_modtab` | Offset to module reference table |
| 0x2A | 2 | `ne_imptab` | Offset to imported names table |

### 1.3 Segment Table Entries

Each entry in the segment table is 8 bytes:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 2 | `ns_sector` | File sector of segment data |
| 0x02 | 2 | `ns_cbseg` | Size of segment in file |
| 0x04 | 2 | `ns_flags` | Segment flags (CODE/DATA, MOVEABLE, PRELOAD, etc.) |
| 0x06 | 2 | `ns_minalloc` | Minimum allocation size in memory |

### 1.4 Segment Flags

| Bit | Name | Meaning |
|-----|------|---------|
| 0 | `NSTYPE` | 0 = CODE, 1 = DATA |
| 4 | `NSMOVE` | Segment is moveable |
| 6 | `NSPRELOAD` | Segment is preloaded at module load time |
| 8 | `NSRELOC` | Segment has relocation records |
| 12 | `NSDISCARD` | Segment is discardable |

---

## 2 – DOS Stub Loader

### 2.1 Role of the Stub

Every NE executable begins with an MZ (DOS) executable header and a
small DOS program — the "stub".  The stub serves two purposes:

1. **Plain DOS execution**: If the NE file is run from a plain DOS
   prompt without Windows, the stub prints a message like
   `"This program requires Microsoft Windows."` and exits.

2. **Self-loading kernel**: For `krnl386.exe` specifically, the stub
   acts as a **self-loading bootstrap**.  Instead of simply printing an
   error, the krnl386 stub:
   - Sets up initial memory (conventional + extended via XMS/DPMI)
   - Reads the NE header from the file
   - Loads and relocates the kernel's code and data segments
   - Transfers control to the NE entry point

### 2.2 Stub Structure

The DOS stub is a standard MZ-format program.  It is typically written
in 16-bit x86 assembly (e.g., using `wasm`, the Open Watcom assembler)
and linked as the stub for the NE binary.

```
┌────────────────────────────────────────┐
│ MZ Header (28+ bytes)                  │
│   e_magic  = 'MZ'                      │
│   e_lfanew = offset to NE header       │
├────────────────────────────────────────┤
│ DOS Stub Code                          │
│   1. Check if Windows is running       │
│   2. If not: print error, exit (4Ch)   │
│   3. If self-loading (krnl386):        │
│      a. Open self (NE file)            │
│      b. Seek to NE header (e_lfanew)   │
│      c. Parse NE header + segment table│
│      d. Allocate memory for segments   │
│      e. Load segments from file        │
│      f. Apply relocations              │
│      g. JMP FAR to NE CS:IP entry      │
└────────────────────────────────────────┘
```

### 2.3 MZ Header → NE Header Link

The MZ header field at offset 0x3C (`e_lfanew`) contains the file
offset to the NE header.  The Windows loader (or the self-loading stub)
uses this value to locate the NE portion of the file:

```
Offset 0x3C in MZ header  ──►  NE header offset
```

---

## 3 – Open Watcom Toolchain Configuration

### 3.1 Compiler (`wcc`)

```
wcc -ml -za99 -wx -d2 -i=src <source.c>
```

| Flag | Purpose |
|------|---------|
| `-ml` | Large memory model — far code and data pointers, matching the original krnl386.exe segment layout |
| `-za99` | Enable C99 language extensions |
| `-wx` | All warnings enabled |
| `-d2` | Full symbolic debug information |
| `-i=src` | Include search path for headers |

### 3.2 Linker (`wlink`)

```
wlink format windows option quiet name build/krnl386.exe file {obj1.obj obj2.obj ...}
```

| Directive | Purpose |
|-----------|---------|
| `format windows` | Produce an NE-format executable (16-bit Windows) |
| `option quiet` | Suppress informational linker output |
| `name <path>` | Output file name |
| `file {objs}` | Object files to link |

#### NE Stub Control

Watcom's linker embeds a default DOS stub for NE targets.  To use a
custom self-loading stub:

```
wlink format windows option stub=stub.exe name krnl386.exe file {objs}
```

Where `stub.exe` is a pre-built MZ executable containing the
self-loading bootstrap code.

#### Segment Ordering

Use `SEGMENT` directives to control segment placement:

```
wlink format windows &
    segment CODE preload &
    segment DATA preload &
    segment _BSS  &
    name krnl386.exe file {objs}
```

#### Export Definitions

KERNEL ordinals can be specified inline or via a `.def` file:

```
; krnl386.def
LIBRARY KERNEL
EXPORTS
    GetVersion        @3
    GetModuleHandle   @47
    GetProcAddress    @50
    GlobalAlloc       @15
    GlobalFree        @17
    ...
```

Then link with:

```
wlink format windows option quiet @krnl386.def name krnl386.exe file {objs}
```

### 3.3 Makefile Target

The `krnl386` target in the Makefile links all in-scope kernel modules:

```makefile
krnl386: $(KRNL386_BIN)

$(KRNL386_BIN): $(KRNL386_OBJS) | $(BUILD_DIR)
	$(LD) format windows option quiet name $@ file {$(KRNL386_OBJS)}
```

---

## 4 – Relocation Records

Each segment with the `NSRELOC` flag set has a relocation table
appended after its raw data in the file.  Relocation records are 8
bytes each:

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0x00 | 1 | `nr_stype` | Source type (byte, segment, far pointer, offset) |
| 0x01 | 1 | `nr_flags` | Target type (internal ref, imported ordinal, imported name, OS fixup) |
| 0x02 | 2 | `nr_soff` | Source offset within the segment |
| 0x04 | 2 | `nr_mod` | Target module index (for imports) or segment number (internal) |
| 0x06 | 2 | `nr_proc` | Target ordinal or offset |

The WinDOS modules `ne_reloc.c` and `ne_loader.c` implement full
relocation processing for all four target types.

---

## 5 – krnl386.exe Entry Point and Boot Sequence

### 5.1 Boot Flow

```
DOS boot
  └─► AUTOEXEC.BAT or manual invocation
       └─► krnl386.exe MZ stub runs
            ├─► Stub loads NE kernel segments into memory
            ├─► Stub applies relocations
            └─► Stub transfers control to NE CS:IP
                 └─► ne_kernel_init()
                      ├─► ne_parser initialised
                      ├─► ne_loader initialised
                      ├─► ne_module table created
                      ├─► ne_impexp tables populated
                      ├─► ne_task scheduler started
                      ├─► ne_mem heaps initialised
                      ├─► ne_trap vectors installed
                      ├─► ne_driver subsystems started
                      └─► System DLLs loaded (USER.EXE, GDI.EXE, ...)
```

### 5.2 Self-Loading vs. Standard NE

| Feature | Standard NE | krnl386.exe (self-loading) |
|---------|-------------|---------------------------|
| Loaded by | Windows loader | Own DOS stub |
| Stub purpose | Print error message | Bootstrap the kernel |
| Entry point | Called by Windows | Called by own stub |
| Segment loading | Windows loader reads segments | Stub reads segments |
| Relocations | Windows loader applies | Stub applies |

---

## 6 – Future Work

- **Custom stub implementation**: Write a dedicated `wasm` assembly
  stub that performs the full self-loading sequence (memory setup, NE
  parsing, segment loading, relocation, and entry-point transfer).
- **Export `.def` file**: Create `krnl386.def` with the full ordinal
  listing for all implemented KERNEL exports.
- **Segment layout optimization**: Profile and tune segment ordering
  for optimal load performance.
- **CI integration**: Add automated builds that produce the NE binary
  and run basic validation (file format checks, ordinal verification).

---

## References

- [Microsoft NE Executable Format (MSDN Archive)](https://web.archive.org/web/2023/https://docs.microsoft.com/en-us/windows/win32/debug/pe-format)
- [Open Watcom Linker Guide – NE Format](https://github.com/open-watcom/open-watcom-v2/wiki)
- [NE (New Executable) format specification](https://wiki.osdev.org/NE)
- [Windows 3.1 SDK – Module Definition File Reference](https://web.archive.org/web/2023/https://docs.microsoft.com)

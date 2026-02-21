## WinDOS Kernel Replacement Roadmap (Windows 3.1)

### Project Scope

> **Current focus: replacing `krnl386.exe` only.**
>
> The original `krnl386.exe` is a unique NE-executable with a
> self-loading stub.  This project aims to produce a drop-in replacement
> built with the Open Watcom toolchain, which can generate
> NE-executables.
>
> GDI, USER, and driver components are **not** being replaced or removed
> at this time.  All existing code for those subsystems is preserved in
> `archive/future/` for potential future work.

### Directory Layout

```
WinDOS/
├── src/              # Core kernel source modules (in scope)
├── tests/            # Unit tests for in-scope modules
├── archive/
│   └── future/       # Preserved USER.EXE / GDI.EXE code (out of scope)
│       ├── src/      # ne_user.c/.h, ne_gdi.c/.h
│       └── tests/    # test_ne_user.c, test_ne_gdi.c
├── Makefile          # Build system (krnl386 + test targets)
├── NE_STUB.md        # NE-executable and DOS stub design document
├── DEVELOPER.md      # Architecture and contributor guide
├── INSTALL.md        # Installation and boot guide
├── ROADMAP.md        # Extended roadmap and phase tracking
└── KERNEL_ASSESSMENT.md  # Gap analysis and missing-step catalog
```

### Module Classification

| Scope | Modules | Notes |
|-------|---------|-------|
| **In scope** (krnl386 replacement) | `ne_parser`, `ne_loader`, `ne_reloc`, `ne_module`, `ne_impexp`, `ne_task`, `ne_mem`, `ne_trap`, `ne_kernel`, `ne_segmgr`, `ne_resource`, `ne_dpmi`, `ne_integrate`, `ne_fullinteg`, `ne_compat`, `ne_release` | Core kernel loading, runtime, and API surface (in `src/`) |
| **In scope** (kernel dependency) | `ne_driver` | Device driver module used by `ne_kernel` for `GetTickCount` and driver services (in `src/`) |
| **Preserved** (future phases) | `ne_user`, `ne_gdi` | USER.EXE and GDI.EXE code — moved to `archive/future/` for later replacement work |

### Overview
This roadmap breaks the work into small milestones to implement a replacement for `krnl386.exe` in WinDOS, starting from executable loading and ending with full subsystem integration.

### Assumptions
- A working DOS 16-bit environment is already available.
- The Open Watcom compiler toolchain (v2.0+) is available; it can build 16-bit NE-executables.
- Target runtime is DOS 5.0+ with enough conventional/extended memory for Windows 3.1 modules.
- Standard low-level tooling (assembler/linker/debugger) is available for bring-up diagnostics.

### Open Watcom NE-Executable Toolchain Requirements

Building a replacement `krnl386.exe` as an NE-executable requires:

- **Open Watcom C/C++ v2.0+** — `wcc` compiler targeting 16-bit real-mode DOS.
- **Watcom Linker (`wlink`)** — with `format windows` or `system windows` directive to produce NE-format output.
- **Large memory model (`-ml`)** — for far code and data segments matching the original krnl386.exe layout.
- **C99 extensions (`-za99`)** — used throughout the codebase.
- **NE self-loading stub** — the linker must embed an MZ stub that loads the NE image; Watcom provides this by default for NE targets.
- **Segment ordering control** — `wlink` `SEGMENT` directives to match the original segment layout (code, data, resident/discardable).
- **Export definitions** — `wlink` `EXPORT` directives or a `.def` file listing all KERNEL ordinals to produce the correct export table.

### Building krnl386.exe

The Makefile includes a dedicated `krnl386` target that links all
in-scope kernel modules into a single NE-format executable:

```bash
# Build the krnl386.exe NE-executable (requires Open Watcom):
make krnl386

# Build and run all kernel tests (host/CI):
make host-test
```

The `krnl386` target uses `wlink format windows` to produce the NE
binary.  See [NE_STUB.md](NE_STUB.md) for details on the DOS stub
loader and NE header requirements.

### Step-by-step plan
1. **NE-file parser**
   - [x] Read and document the NE executable format specification (header layout, table structures).
   - [x] Implement parsing of the NE file header (magic bytes, linker version, offsets).
   - [x] Implement parsing of the segment table (segment descriptors, flags, sizes).
   - [x] Implement parsing of the resource table, imported names table, and entry table.
   - [x] Validate magic values (`NE` signature) and reject invalid files with clear errors.
   - [x] Validate segment count, entry-point offsets, and required table offsets.
   - [x] Write unit tests using representative NE binary samples (e.g. stock Windows 3.1 DLLs).
   - [x] Expose a clean API for querying parsed metadata (segments, exports, entry point).
   - [x] Deliverable: a standalone parser module that can print or expose parsed metadata.

2. **NE-file loader**
   - [x] Allocate DOS conventional/extended memory regions for each NE segment.
   - [x] Load code and data segments from file into allocated memory according to segment descriptors.
   - [x] Respect segment alignment requirements and honor segment flags (read/write/execute).
   - [x] Handle the case where available memory is insufficient and report diagnostics.
   - [x] Add loader diagnostics for segment placement addresses and load failures.
   - [x] Verify entry-point offset is within bounds after loading.
   - [x] Write integration tests confirming correct segment placement for known NE files.
   - [x] Deliverable: executable image mapped in memory with basic entry readiness.

3. **Relocation management**
   - [x] Parse the relocation records for each loaded segment.
   - [x] Apply internal relocations (intra-module segment/offset fixups).
   - [x] Apply imported-reference relocations (cross-module symbol fixups).
   - [x] Handle OS-fixup record types required by Windows 3.1 runtime.
   - [x] Report and fail gracefully on unresolvable relocation targets.
   - [x] Write verification tests for pointer fixups and segment selector fixups.
   - [x] Write verification tests for imported reference resolution against a dummy module.
   - [x] Deliverable: correctly relocated module image.

4. **Module table handling**
   - [x] Design and implement the global module table data structure.
   - [x] Assign and track unique module handles for each loaded NE module.
   - [x] Implement reference counting for module load/unload lifecycle.
   - [x] Record inter-module dependencies to enforce correct unload ordering.
   - [x] Implement duplicate-load detection and return existing handle on re-load.
   - [x] Implement module unload path including reference count decrement and memory release.
   - [x] Write tests for load, duplicate-load, and unload bookkeeping correctness.
   - [x] Deliverable: stable module lifecycle management.

5. **Import/export resolution**
   - [x] Build per-module export tables indexed by ordinal number.
   - [x] Build per-module export tables indexed by name for name-based lookups.
   - [x] Implement ordinal-based import resolution against loaded module export tables.
   - [x] Implement name-based import resolution against loaded module export tables.
   - [x] Register temporary API stubs for imports whose target module is not yet loaded.
   - [x] Maintain a shared stub-tracking table (module/API name, owner step, behavior, replacement milestone, removal status).
   - [x] Replace stubs with real addresses as target modules are loaded.
   - [x] Write tests covering ordinal resolution, name resolution, and stub fallback paths.
   - [x] Deliverable: inter-module calls resolving through a central linker/runtime path.

6. **Task and memory management**
   - [x] Define the task descriptor structure (stack, registers, state, priority).
   - [x] Implement task creation with stack allocation and initial context setup.
   - [x] Implement a cooperative scheduling loop and yield/switch hooks.
   - [x] Implement context-save and context-restore routines for task switching.
   - [x] Implement Windows 3.1-compatible global memory allocation (GMEM) primitives.
   - [x] Implement local memory allocation (LMEM) primitives per task heap.
   - [x] Track memory ownership per task and enforce cleanup on task termination.
   - [x] Validate task startup path (entry called, stack correct) and teardown path (resources freed).
   - [x] Write tests for task create/switch/destroy and memory alloc/free correctness.
   - [x] Deliverable: minimal multitasking runtime with deterministic memory behavior.

7. **Exception and trap handling**
   - [x] Identify all CPU exception/trap vectors needed for kernel operation (GP fault, stack fault, etc.).
   - [x] Install low-level interrupt/trap handler stubs for each required vector.
   - [x] Route each exception to a C-level diagnostic handler with register context.
   - [x] Implement safe recovery paths for recoverable faults (e.g. page not present stubs).
   - [x] Define and implement the panic/fatal-error handler for unrecoverable conditions.
   - [x] Add logging of fault address, exception code, and register state to diagnostic output.
   - [x] Write tests that deliberately trigger handled faults and verify correct handler dispatch.
   - [x] Deliverable: predictable fault handling and improved debugging visibility.

8. **Integration steps**
   - [x] Identify the minimal set of kernel services needed by the Windows 3.1 GUI layer.
   - [x] Integrate kernel services with the display/GUI layer incrementally, one subsystem at a time.
   - [x] Integrate kernel services with device drivers (keyboard, timer, display).
   - [x] Integrate with system DLLs (KERNEL.EXE, USER.EXE, GDI.EXE) interfaces.
   - [x] Write compatibility tests in the DOS environment for each integration stage before proceeding.
   - [x] Gate promotion to the next stage on all prior-stage compatibility tests passing.
   - [x] Track regressions per subsystem and document fallback/bypass paths during migration.
   - [x] Document integration status, known gaps, and workarounds per subsystem.
   - [x] Deliverable: staged compatibility across core Windows 3.1 subsystems.

9. **Full integration**
   - [x] Perform end-to-end boot sequence validation with the custom kernel replacing the original.
   - [x] Validate full runtime stability across all integrated subsystems under normal workloads.
   - [x] Execute regression suite covering all prior steps and confirm no regressions.
   - [x] Document the complete test procedure for reproducible verification.
   - [x] Document all known limitations, unsupported configurations, and deferred work.
   - [x] Document supported configurations and minimum hardware/emulator requirements.
   - [x] Produce a release checklist covering build steps, test steps, and sign-off criteria.
   - [x] Verify reproducible builds produce bit-identical output across clean environments.
   - [x] Deliverable: fully replaceable kernel path for WinDOS with documented constraints.

### Remaining work

For a detailed assessment of gaps, missing functionalities, and the phased plan to achieve full Windows 3.1 kernel replacement status, see **[ROADMAP.md](ROADMAP.md)**.

A full kernel replacement status review has been performed.  See
**[KERNEL_ASSESSMENT.md](KERNEL_ASSESSMENT.md)** for the gap analysis
and final roadmap of missing steps.

### Tracking and execution notes
- Create dedicated sub-issues per step for:
  - Implementation tasks
  - Test coverage tasks
  - Documentation updates
- Use a consistent issue naming format: `Step <N>: <component> - <goal>`.
- Example naming: `Step 1: NE-parser - Validate magic values`.
- Define done criteria per sub-issue (code merged, tests passing, docs updated).
- Track temporary API stubs in a shared table with: module/API name, owner step, behavior, replacement milestone, and removal status.
- Use milestone reviews after each step before proceeding.
- Publish regular progress updates with blockers, risks, and next actions.

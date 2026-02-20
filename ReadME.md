## WinDOS Kernel Replacement Roadmap (Windows 3.1)

### Overview
This roadmap breaks the work into small milestones to implement a full Windows 3.1 kernel replacement in WinDOS, starting from executable loading and ending with full subsystem integration.

### Assumptions
- A working DOS 16-bit environment is already available.
- A WATCOM-compatible compiler toolchain is available.
- Target runtime is DOS 5.0+ with enough conventional/extended memory for Windows 3.1 modules.
- Standard low-level tooling (assembler/linker/debugger) is available for bring-up diagnostics.

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

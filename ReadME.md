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
   - [ ] Read and document the NE executable format specification (header layout, table structures).
   - [ ] Implement parsing of the NE file header (magic bytes, linker version, offsets).
   - [ ] Implement parsing of the segment table (segment descriptors, flags, sizes).
   - [ ] Implement parsing of the resource table, imported names table, and entry table.
   - [ ] Validate magic values (`NE` signature) and reject invalid files with clear errors.
   - [ ] Validate segment count, entry-point offsets, and required table offsets.
   - [ ] Write unit tests using representative NE binary samples (e.g. stock Windows 3.1 DLLs).
   - [ ] Expose a clean API for querying parsed metadata (segments, exports, entry point).
   - [ ] Deliverable: a standalone parser module that can print or expose parsed metadata.

2. **NE-file loader**
   - [ ] Allocate DOS conventional/extended memory regions for each NE segment.
   - [ ] Load code and data segments from file into allocated memory according to segment descriptors.
   - [ ] Respect segment alignment requirements and honor segment flags (read/write/execute).
   - [ ] Handle the case where available memory is insufficient and report diagnostics.
   - [ ] Add loader diagnostics for segment placement addresses and load failures.
   - [ ] Verify entry-point offset is within bounds after loading.
   - [ ] Write integration tests confirming correct segment placement for known NE files.
   - [ ] Deliverable: executable image mapped in memory with basic entry readiness.

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
   - [ ] Build per-module export tables indexed by ordinal number.
   - [ ] Build per-module export tables indexed by name for name-based lookups.
   - [ ] Implement ordinal-based import resolution against loaded module export tables.
   - [ ] Implement name-based import resolution against loaded module export tables.
   - [ ] Register temporary API stubs for imports whose target module is not yet loaded.
   - [ ] Maintain a shared stub-tracking table (module/API name, owner step, behavior, replacement milestone, removal status).
   - [ ] Replace stubs with real addresses as target modules are loaded.
   - [ ] Write tests covering ordinal resolution, name resolution, and stub fallback paths.
   - [ ] Deliverable: inter-module calls resolving through a central linker/runtime path.

6. **Task and memory management**
   - [ ] Define the task descriptor structure (stack, registers, state, priority).
   - [ ] Implement task creation with stack allocation and initial context setup.
   - [ ] Implement a cooperative scheduling loop and yield/switch hooks.
   - [ ] Implement context-save and context-restore routines for task switching.
   - [ ] Implement Windows 3.1-compatible global memory allocation (GMEM) primitives.
   - [ ] Implement local memory allocation (LMEM) primitives per task heap.
   - [ ] Track memory ownership per task and enforce cleanup on task termination.
   - [ ] Validate task startup path (entry called, stack correct) and teardown path (resources freed).
   - [ ] Write tests for task create/switch/destroy and memory alloc/free correctness.
   - [ ] Deliverable: minimal multitasking runtime with deterministic memory behavior.

7. **Exception and trap handling**
   - [ ] Identify all CPU exception/trap vectors needed for kernel operation (GP fault, stack fault, etc.).
   - [ ] Install low-level interrupt/trap handler stubs for each required vector.
   - [ ] Route each exception to a C-level diagnostic handler with register context.
   - [ ] Implement safe recovery paths for recoverable faults (e.g. page not present stubs).
   - [ ] Define and implement the panic/fatal-error handler for unrecoverable conditions.
   - [ ] Add logging of fault address, exception code, and register state to diagnostic output.
   - [ ] Write tests that deliberately trigger handled faults and verify correct handler dispatch.
   - [ ] Deliverable: predictable fault handling and improved debugging visibility.

8. **Integration steps**
   - [ ] Identify the minimal set of kernel services needed by the Windows 3.1 GUI layer.
   - [ ] Integrate kernel services with the display/GUI layer incrementally, one subsystem at a time.
   - [ ] Integrate kernel services with device drivers (keyboard, timer, display).
   - [ ] Integrate with system DLLs (KERNEL.EXE, USER.EXE, GDI.EXE) interfaces.
   - [ ] Write compatibility tests in the DOS environment for each integration stage before proceeding.
   - [ ] Gate promotion to the next stage on all prior-stage compatibility tests passing.
   - [ ] Track regressions per subsystem and document fallback/bypass paths during migration.
   - [ ] Document integration status, known gaps, and workarounds per subsystem.
   - [ ] Deliverable: staged compatibility across core Windows 3.1 subsystems.

9. **Full integration**
   - [ ] Perform end-to-end boot sequence validation with the custom kernel replacing the original.
   - [ ] Validate full runtime stability across all integrated subsystems under normal workloads.
   - [ ] Execute regression suite covering all prior steps and confirm no regressions.
   - [ ] Document the complete test procedure for reproducible verification.
   - [ ] Document all known limitations, unsupported configurations, and deferred work.
   - [ ] Document supported configurations and minimum hardware/emulator requirements.
   - [ ] Produce a release checklist covering build steps, test steps, and sign-off criteria.
   - [ ] Verify reproducible builds produce bit-identical output across clean environments.
   - [ ] Deliverable: fully replaceable kernel path for WinDOS with documented constraints.

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

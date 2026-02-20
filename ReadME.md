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
   - Parse Windows 3.1 NE headers and table offsets.
   - Validate magic values, segment counts, and entry points.
   - Add parser tests using representative NE samples.
   - Deliverable: a standalone parser module that can print or expose parsed metadata.

2. **NE-file loader**
   - Load NE segments into DOS memory according to segment descriptors.
   - Respect alignment requirements and segment flags.
   - Add loader diagnostics for segment placement and load failures.
   - Deliverable: executable image mapped in memory with basic entry readiness.

3. **Relocation management**
   - Parse and apply relocation records for loaded segments.
   - Support internal and imported reference relocation paths.
   - Add verification tests for pointer/segment fixups.
   - Deliverable: correctly relocated module image.

4. **Module table handling**
   - Implement a global module table for loaded NE modules.
   - Track module handles, reference counts, and dependencies.
   - Add load/unload bookkeeping and duplicate-load protection.
   - Deliverable: stable module lifecycle management.

5. **Import/export resolution**
   - Resolve imported symbols against loaded module exports.
   - Build export lookup structures (name + ordinal support).
   - Provide temporary API stubs for unresolved imports to unblock bring-up; track each stub in a TODO map with owning step and expected replacement milestone.
   - Deliverable: inter-module calls resolving through a central linker/runtime path.

6. **Task and memory management**
   - Introduce basic task descriptors, scheduling hooks, and context-switch flow.
   - Implement Windows 3.1-compatible memory primitives and allocation regions.
   - Validate task startup/teardown paths and memory ownership.
   - Deliverable: minimal multitasking runtime with deterministic memory behavior.

7. **Exception and trap handling**
   - Install exception/trap handlers required for kernel runtime stability.
   - Route faults to diagnostic handlers with safe recovery where possible.
   - Define panic/fatal-error behavior for unrecoverable conditions.
   - Deliverable: predictable fault handling and improved debugging visibility.

8. **Integration steps**
   - Integrate kernel services incrementally with GUI, drivers, and system DLL interactions.
   - Gate each integration stage behind compatibility tests in DOS environment.
   - Track regressions by subsystem and keep fallback paths during migration.
   - Deliverable: staged compatibility across core Windows 3.1 subsystems.

9. **Full integration**
   - Complete end-to-end boot and runtime validation with the custom kernel.
   - Document test procedures, known limitations, and supported configurations.
   - Produce a release checklist for reproducible build + verification.
   - Deliverable: fully replaceable kernel path for WinDOS with documented constraints.

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

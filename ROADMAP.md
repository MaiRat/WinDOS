# WinDOS – Remaining Roadmap for Windows 3.1 Kernel Replacement

## Current Status Assessment

All nine steps defined in the original plan (ReadME.md) are implemented with corresponding source modules and unit tests:

| Step | Module | Status |
|------|--------|--------|
| 1. NE-file parser | `ne_parser.c` | Complete |
| 2. NE-file loader | `ne_loader.c` | Complete |
| 3. Relocation management | `ne_reloc.c` | Complete |
| 4. Module table handling | `ne_module.c` | Complete |
| 5. Import/export resolution | `ne_impexp.c` | Complete |
| 6. Task and memory management | `ne_task.c`, `ne_mem.c` | Complete (host); DOS assembly deferred |
| 7. Exception and trap handling | `ne_trap.c` | Complete (host); DOS IVT stubs deferred |
| 8. Integration steps | `ne_integrate.c` | Complete |
| 9. Full integration | `ne_fullinteg.c` | Complete |

### Identified Gaps

The original roadmap covers the **NE executable loading and runtime infrastructure**. A full Windows 3.1 kernel replacement requires additional subsystems beyond NE file handling. The gaps fall into two categories:

1. **Platform gaps** – Host/POSIX stubs that need real 16-bit DOS implementations.
2. **Functional gaps** – Windows 3.1 kernel services not yet addressed.

---

## Phase 1 – DOS 16-Bit Target Bring-Up

Complete the transition from host/POSIX stubs to real 16-bit DOS code.

- [x] Implement 16-bit real-mode context switching in `ne_task.c` using Watcom inline assembly (replaces the current `ucontext` POSIX path).
- [x] Implement DOS interrupt vector table (IVT) installation for CPU exception handlers in `ne_trap.c`.
- [x] Replace host `malloc`/`free` calls in the loader and memory manager with DOS INT 21h memory allocation (AH=48h/49h/4Ah).
- [x] Validate the build under the Open Watcom 16-bit DOS target and resolve any compiler/linker issues.
- [x] Run the existing test suite on a DOS environment (or emulator such as DOSBox) and fix platform-specific failures.

## Phase 2 – Windows 3.1 API Stubs (KERNEL)

Provide stub or minimal implementations of the most critical KERNEL.EXE exports so that Windows 3.1 applications can link and start.

- [x] Enumerate all KERNEL.EXE exports (ordinal list) and classify each as critical, secondary, or optional.
- [x] Implement critical file I/O APIs: `_lopen`, `_lclose`, `_lread`, `_lwrite`, `_llseek`.
- [x] Implement module APIs: `GetModuleHandle`, `GetModuleFileName`, `GetProcAddress`, `LoadLibrary`, `FreeLibrary`.
- [x] Implement memory APIs: `GlobalAlloc`, `GlobalFree`, `GlobalLock`, `GlobalUnlock`, `GlobalReAlloc`, `LocalAlloc`, `LocalFree`, `LocalLock`, `LocalUnlock`.
- [x] Implement task/process APIs: `GetCurrentTask`, `Yield`, `InitTask`, `WaitEvent`, `PostEvent`.
- [x] Implement string/resource APIs: `LoadString`, `LoadResource`, `FindResource`, `LockResource`.
- [x] Implement atom APIs: `GlobalAddAtom`, `GlobalFindAtom`, `GlobalGetAtomName`, `GlobalDeleteAtom`.
- [x] Register all implemented exports in the import/export resolution table so NE modules can resolve them at load time.
- [x] Write tests verifying each stub returns expected values or behaves correctly for basic call sequences.

## Phase 3 – USER and GDI Subsystem Interfaces

Provide the minimal interfaces required by KERNEL for the USER.EXE and GDI.EXE subsystems.

- [x] Define the interface boundary between KERNEL and USER (message queue, window class registration, input dispatch).
- [x] Implement `GetMessage`, `PeekMessage`, `TranslateMessage`, `DispatchMessage` message loop primitives.
- [x] Implement `RegisterClass`, `CreateWindow`, `DestroyWindow`, `ShowWindow`, `UpdateWindow`.
- [x] Implement `SendMessage`, `PostMessage`, `DefWindowProc`.
- [x] Define the interface boundary between KERNEL and GDI (device context, drawing primitives).
- [x] Implement `GetDC`, `ReleaseDC`, `BeginPaint`, `EndPaint`.
- [x] Implement basic drawing stubs: `TextOut`, `MoveTo`, `LineTo`, `Rectangle`, `SetPixel`.
- [x] Write integration tests that exercise the message loop and basic window lifecycle.

## Phase 4 – Device Driver Integration

Enable keyboard, timer, and display drivers under DOS.

- [ ] Implement keyboard driver: hook INT 09h, translate scan codes to Windows virtual key codes, feed events into the message queue.
- [ ] Implement timer driver: hook INT 08h (or INT 1Ch), provide `GetTickCount`, `SetTimer`, `KillTimer`.
- [ ] Implement display driver: interface with VGA/EGA text or graphics mode for basic output.
- [ ] Implement mouse driver: hook INT 33h for mouse position and button state, feed events into the message queue.
- [ ] Verify driver coexistence (keyboard + timer + display) with the cooperative scheduler under DOS.

## Phase 5 – Dynamic Segment and Resource Management

Implement segment discarding, reloading, and full resource management required by larger Windows 3.1 applications.

- [ ] Implement discardable segment eviction and demand-reload from file.
- [ ] Implement movable segment compaction and handle table updates.
- [ ] Implement full resource enumeration (`EnumResourceTypes`, `EnumResourceNames`).
- [ ] Implement accelerator table loading and translation.
- [ ] Implement dialog template loading and `DialogBox` / `CreateDialog`.
- [ ] Implement menu resource loading and `LoadMenu`, `TrackPopupMenu`.
- [ ] Write tests for segment discard/reload and resource enumeration.

## Phase 6 – Compatibility Testing and Hardening

Validate the replacement kernel against real Windows 3.1 binaries and applications.

- [ ] Load and validate KERNEL.EXE, USER.EXE, and GDI.EXE from a stock Windows 3.1 installation.
- [ ] Verify module loading, relocation, and inter-module import resolution for the stock system DLLs.
- [ ] Test with at least one stock Windows 3.1 application (e.g., Notepad, Calculator, Write).
- [ ] Profile memory usage and identify leaks or excessive consumption under sustained workloads.
- [ ] Stress-test the cooperative scheduler with multiple concurrent tasks.
- [ ] Document all known limitations, unsupported APIs, and deferred functionality.
- [ ] Produce a compatibility matrix (application × subsystem × status).

## Phase 7 – Release Readiness

Final validation, documentation, and release packaging.

- [ ] Perform full end-to-end boot sequence on target DOS hardware or emulator.
- [ ] Execute the complete regression test suite and confirm zero regressions.
- [ ] Write a user-facing installation guide (DOS setup, file placement, boot procedure).
- [ ] Write a developer guide covering architecture, build instructions, and contribution workflow.
- [ ] Verify reproducible builds produce bit-identical output across clean environments.
- [ ] Tag a versioned release with changelog and known-issues list.

---

## Summary

| Phase | Description | Depends On |
|-------|-------------|------------|
| 1 | DOS 16-bit target bring-up | Steps 1–9 (done) |
| 2 | KERNEL.EXE API stubs | Phase 1 |
| 3 | USER/GDI subsystem interfaces | Phase 2 |
| 4 | Device driver integration | Phase 1 |
| 5 | Dynamic segment & resource management | Phase 2 |
| 6 | Compatibility testing and hardening | Phases 2–5 |
| 7 | Release readiness | Phase 6 |

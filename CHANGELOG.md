# Changelog

All notable changes to WinDOS are documented in this file.

## [1.0.0] - 2026-02-20

### Summary

First release of the WinDOS Windows 3.1 kernel replacement.  All nine
original implementation steps and all seven roadmap phases are complete.

### Added

- **NE executable parser** (`ne_parser`): full parsing of NE file
  headers, segment tables, resource tables, imported names, and entry
  tables with magic-value validation.

- **NE file loader** (`ne_loader`): segment allocation, loading from
  file, alignment handling, and entry-point validation.

- **Relocation management** (`ne_reloc`): internal and imported-reference
  relocations, OS-fixup records, and graceful failure on unresolvable
  targets.

- **Module table** (`ne_module`): global module table with unique handles,
  reference counting, dependency tracking, duplicate-load detection, and
  ordered unload.

- **Import/export resolution** (`ne_impexp`): ordinal-based and name-based
  resolution, stub tracking, and deferred replacement.

- **Task management** (`ne_task`): task descriptors, cooperative
  scheduling, context save/restore, and task lifecycle management.

- **Memory management** (`ne_mem`): GMEM and LMEM primitives, per-task
  heap tracking, and cleanup on termination.

- **Exception handling** (`ne_trap`): CPU exception vector installation,
  C-level diagnostic handlers, safe recovery paths, and panic handler.

- **Integration layer** (`ne_integrate`): staged subsystem integration
  with per-stage compatibility gating.

- **Full integration** (`ne_fullinteg`): end-to-end boot validation,
  runtime stability checks, and release checklist.

- **KERNEL.EXE API stubs** (`ne_kernel`): critical file I/O, module,
  memory, task, string/resource, and atom APIs.

- **USER.EXE subsystem** (`ne_user`): message loop primitives, window
  management, and message dispatch.

- **GDI.EXE subsystem** (`ne_gdi`): device context management, paint
  cycle, and basic drawing stubs.

- **Device drivers** (`ne_driver`): keyboard (INT 09h), timer (INT 08h),
  display (VGA), and mouse (INT 33h) drivers.

- **Segment manager** (`ne_segmgr`): discardable segment eviction,
  demand-reload, movable segment compaction, and handle table updates.

- **Resource manager** (`ne_resource`): resource enumeration, accelerator
  tables, dialog templates, and menu resources.

- **Compatibility testing** (`ne_compat`): system DLL validation, memory
  profiling, scheduler stress testing, known-limitation tracking, and
  compatibility matrix.

- **Release readiness** (`ne_release`): boot sequence validation,
  regression suite tracking, reproducible build verification, release
  metadata, and known-issues list.

- **Documentation**: installation guide (`INSTALL.md`), developer guide
  (`DEVELOPER.md`), and this changelog.

- **Dual-target build**: Watcom/DOS 16-bit and host/POSIX builds with
  comprehensive test suites for both targets.

### Known Issues

- OLE2 APIs are not implemented; applications using OLE2 will not
  function correctly.
- Printing subsystem (PRINTER.DRV) is not implemented.
- Network DDE and NetBIOS APIs are not supported.
- Protected-mode (DPMI) operation is not supported; WinDOS runs in
  real mode only.
- Multimedia APIs (MMSYSTEM.DLL) are not implemented.
- Help system (WINHELP.EXE) integration is not available.
- TrueType font rendering is not implemented; bitmap fonts only.
- Clipboard operations across applications may not work reliably.
- Some applications may require specific Windows 3.1 INI settings
  not handled by WinDOS.

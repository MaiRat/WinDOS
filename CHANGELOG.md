# Changelog

All notable changes to WinDOS are documented in this file.

## [Unreleased] – Phase G: KERNEL Resource Stub Wiring

### Added

- **Phase G KERNEL Resource Wiring** (`ne_kernel`): Connected the
  existing `ne_resource` module to the KERNEL.EXE API stubs so that
  resource lookup, loading, and locking delegate to the resource table
  instead of returning stub values:
  - `LoadString` (`ne_kernel_load_string`) now looks up RT_STRING bundles
    in the attached resource table, parses Pascal-style length-prefixed
    strings, and copies the result into the caller's buffer
  - `FindResource` (`ne_kernel_find_resource`) now searches the resource
    table by type and name, supporting both ordinal (MAKEINTRESOURCE)
    and string-name lookups
  - `LoadResource` (`ne_kernel_load_resource`) now resolves a resource
    info handle to a data handle via the resource table
  - `LockResource` (`ne_kernel_lock_resource`) now returns a pointer to
    the raw resource data for a given data handle
  - `ne_kernel_set_resource_table` – new API to attach or detach an
    `NEResTable` to/from the kernel context

- **New `res` field** in `NEKernelContext` for optional `NEResTable`
  pointer (owned externally).

- **4 new unit tests** for Phase G resource wiring (LoadString with
  string table data, FindResource/LoadResource/LockResource round-trip,
  set_resource_table attach/detach, FindResource by string name).

### Changed

- Updated Makefile to link `ne_resource.c` in the kernel test build.

## [Unreleased] – Phase F: Driver Completion

### Added

- **Phase F Driver Completion** (`ne_driver`): Full 101/102-key keyboard
  support, graphics-mode display driver, printer driver, and enhanced
  mouse driver:
  - Expanded keyboard scan-code table: 27 new VK mappings covering OEM
    punctuation keys (minus, equals, brackets, semicolon, apostrophe,
    grave, backslash, comma, period, slash), keypad operators (multiply,
    add, subtract), navigation keys (Home, End, Page Up, Page Down,
    Insert), lock keys (Caps Lock, Num Lock, Scroll Lock), Numpad 5,
    and F11/F12
  - Graphics-mode display driver with VGA 640×480×16 and 320×200×256
    video modes: `ne_drv_disp_set_mode`, `ne_drv_disp_get_mode`,
    `ne_drv_disp_gfx_set_pixel`, `ne_drv_disp_gfx_get_pixel`,
    `ne_drv_disp_gfx_fill_rect`, `ne_drv_disp_gfx_clear` with
    dynamically allocated framebuffer
  - Printer driver interface (PRINTER.DRV): `ne_drv_printer_install`,
    `ne_drv_printer_uninstall`, `ne_drv_printer_start_doc`,
    `ne_drv_printer_end_doc`, `ne_drv_printer_start_page`,
    `ne_drv_printer_end_page`, `ne_drv_printer_abort_doc`,
    `ne_drv_printer_get_job_count` with up to 4 simultaneous print jobs
  - Mouse cursor rendering: 16×16 cursor bitmap with configurable
    hotspot, `ne_drv_mouse_show_cursor`, `ne_drv_mouse_set_cursor_bitmap`,
    `ne_drv_mouse_get_cursor_visible`
  - Mouse event coalescing: `ne_drv_mouse_coalesce_moves` removes
    duplicate intermediate WM_MOUSEMOVE events from the queue

- **New data structures**: `NEDrvPrintJob`, `NEDrvPrinter` for printer
  subsystem; extended `NEDrvDisplay` with video mode and framebuffer
  fields; extended `NEDrvMouse` with cursor bitmap and hotspot.

- **32 new unit tests** for all Phase F features (67 driver tests total).

## [Unreleased] – Phase E: GDI.EXE Rendering

### Added

- **Phase E GDI.EXE Rendering** (`ne_gdi`): Full rendering backend
  replacing the Phase 3 drawing stubs with real framebuffer-based
  implementations:
  - VGA graphics mode rendering backend with 640×480 8-bit framebuffer
  - Built-in 8×8 bitmap font covering printable ASCII (chars 32–126)
  - `TextOut` with bitmap font rendering to framebuffer
  - `SetPixel` / `GetPixel` with real framebuffer access and bounds
    checking
  - Bresenham line algorithm for `LineTo`
  - `Rectangle` with pen outline and brush fill
  - `Ellipse` with midpoint ellipse algorithm
  - `Polygon` / `Polyline` for multi-point shape drawing
  - `CreatePen` / `CreateBrush` / `CreateFont` – GDI object creation
  - `SelectObject` / `DeleteObject` – GDI object management
  - `SetTextColor` / `SetBkColor` / `SetBkMode` – color and background
    mode attributes
  - `GetTextMetrics` / `GetTextExtent` – text measurement
  - `BitBlt` / `StretchBlt` / `PatBlt` – raster operations (SRCCOPY,
    SRCPAINT, SRCAND, SRCINVERT, BLACKNESS, WHITENESS, PATCOPY)
  - `CreateCompatibleDC` – off-screen device context with own framebuffer
  - `CreateCompatibleBitmap` / `CreateBitmap` / `CreateDIBitmap` – bitmap
    and DIB object support

- **New data structures**: `NEGdiPen`, `NEGdiBrush`, `NEGdiFont`,
  `NEGdiBitmap`, `NEGdiObject`, `NEGdiTextMetrics`, `COLORREF` type
  with `RGB` macro.

- **Extended device context**: selected pen/brush/font handles, text
  color, background color, background mode, per-DC framebuffer for
  compatible DCs.

- **52 new unit tests** for all Phase E APIs (74 GDI tests total).

## [Unreleased] – Phase D: USER.EXE Expansion

### Added

- **Phase D USER.EXE APIs** (`ne_user`): 33 new USER.EXE API
  implementations covering dialog-based applications and richer window
  management:
  - `MessageBox` – modal message box with MB_OK/MB_OKCANCEL/MB_YESNO/
    MB_YESNOCANCEL styles
  - `DialogBox` / `CreateDialog` / `EndDialog` – dialog creation and
    lifecycle management with template support
  - `SetCapture` / `ReleaseCapture` – mouse capture tracking
  - `GetClientRect` / `GetWindowRect` – window rectangle queries
  - `MoveWindow` / `SetWindowPos` – window position and size management
  - `SetWindowText` / `GetWindowText` – window title/text management
  - `EnableWindow` / `IsWindowEnabled` / `IsWindowVisible` – window
    state queries and control
  - `SetFocus` / `GetFocus` – input focus management with WM_SETFOCUS /
    WM_KILLFOCUS notifications
  - `InvalidateRect` / `ValidateRect` – paint invalidation
  - `ScrollWindow` – scroll window contents (stub with repaint)
  - `SetTimer` / `KillTimer` – timer wiring from `ne_driver` into USER
    stubs
  - `OpenClipboard` / `CloseClipboard` / `SetClipboardData` /
    `GetClipboardData` – clipboard data management
  - `CreateCaret` / `SetCaretPos` / `ShowCaret` / `HideCaret` /
    `DestroyCaret` – caret lifecycle
  - `GetKeyState` / `GetAsyncKeyState` – input state queries
  - `CreateMenu` / `SetMenu` / `AppendMenu` / `GetMenu` /
    `DestroyMenu` – menu creation and management

- **New data structures**: `NEUserRect`, `NEUserMenuItem`, `NEUserMenu`,
  `NEUserClipboard`, `NEUserCaret` for Phase D subsystem state.

- **Extended window descriptor**: position (x, y), size (width, height),
  window text, enabled state, invalidation tracking, and menu handle.

- **New message identifiers**: `WM_SETFOCUS`, `WM_KILLFOCUS`,
  `WM_ENABLE`, `WM_SETTEXT`, `WM_GETTEXT`, `WM_GETTEXTLENGTH`,
  `WM_TIMER`, `WM_COMMAND`.

- **24 new unit tests** for all Phase D APIs (46 USER tests total).

## [Unreleased] – Phase B: INI File and Profile APIs

### Added

- **Phase B INI File APIs** (`ne_kernel`): 6 new profile/INI file API
  implementations covering the Windows 3.1 INI configuration APIs:
  - `GetProfileString` / `GetProfileInt` – read from WIN.INI
  - `WriteProfileString` – write to WIN.INI
  - `GetPrivateProfileString` / `GetPrivateProfileInt` – read from
    application-specific INI files
  - `WritePrivateProfileString` – write to application-specific INI files
  - Case-insensitive section and key lookup
  - Full WIN.INI path handling (`C:\WINDOWS\WIN.INI`)
  - Support for key deletion (value=NULL) and section deletion (key=NULL)

- **14 new unit tests** for all Phase B APIs.

## [Unreleased] – Phase A: Critical KERNEL.EXE API Expansion

### Added

- **Phase A KERNEL.EXE APIs** (`ne_kernel`): 20 new critical API stubs
  covering the APIs required by nearly every Windows 3.1 application:
  - `GetVersion` / `GetWinFlags` – OS version (3.10) and capability flags
  - `GetWindowsDirectory` / `GetSystemDirectory` – directory path queries
  - `GetDOSEnvironment` – DOS environment pointer (stub)
  - `WinExec` – child application launching (stub)
  - `ExitWindows` – clean shutdown (stub)
  - `FatalExit` / `FatalAppExit` – error termination
  - `GetTickCount` – tick counter delegating to `ne_driver`
  - `Catch` / `Throw` – non-local jumps via setjmp/longjmp
  - `MakeProcInstance` / `FreeProcInstance` – callback thunks (passthrough
    in real mode)
  - `OpenFile` – file operations with ofstruct semantics (OF_READ,
    OF_WRITE, OF_READWRITE, OF_EXIST, OF_DELETE)
  - `OutputDebugString` – debug output to stderr
  - `SetErrorMode` / `GetLastError` – error mode management
  - `IsTask` / `GetNumTasks` – task table queries

- **Driver integration**: `ne_kernel_set_driver()` to attach a driver
  context for `GetTickCount` delegation.

- **22 new unit tests** for all Phase A APIs (60 kernel tests total).

### Changed

- Fixed `GlobalGetAtomName` ordinal from 166 to 167 to match the
  correct Windows 3.1 KERNEL.EXE ordinal assignment.
- Updated Makefile to link `ne_driver.c` in the kernel test build.

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

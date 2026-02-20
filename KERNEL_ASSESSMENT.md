# WinDOS Kernel Replacement Assessment

## Purpose

This document evaluates whether the current WinDOS implementation can
serve as a full replacement for the Windows 3.1 Kernel, covering all
required functions, compatibility, and stability aspects.  It catalogs
identified gaps and provides a final actionable roadmap.

Tracking issue: **Recheck Kernel Replacement Status and Final Roadmap
Steps**.

---

## 1 – Current Implementation Summary

All nine original implementation steps (ReadME.md) and all seven
extended roadmap phases (ROADMAP.md) are marked complete.  The v1.0.0
release ships the following subsystems:

| Module            | Scope                                          |
|-------------------|------------------------------------------------|
| `ne_parser`       | NE executable format parsing and validation    |
| `ne_loader`       | Segment allocation and loading from file       |
| `ne_reloc`        | Internal and cross-module relocations          |
| `ne_module`       | Module table, reference counting, dependencies |
| `ne_impexp`       | Ordinal and name-based import/export resolution|
| `ne_task`         | Task descriptors, cooperative scheduling       |
| `ne_mem`          | GMEM and LMEM allocation, per-task tracking    |
| `ne_trap`         | CPU exception vectors and trap handlers        |
| `ne_kernel`       | KERNEL.EXE API stubs (23 ordinals)             |
| `ne_user`         | USER.EXE message loop, window management       |
| `ne_gdi`          | GDI.EXE device contexts, drawing stubs         |
| `ne_driver`       | Keyboard, timer, display, mouse drivers        |
| `ne_segmgr`       | Discardable segment eviction and reload        |
| `ne_resource`     | Resource enumeration, dialogs, menus, accels   |
| `ne_compat`       | Compatibility testing and hardening framework  |
| `ne_release`      | Release readiness tracking                     |

Test suite: 17 test modules, all passing (host-test target).

---

## 2 – Gap Analysis

### 2.1 KERNEL.EXE API Coverage

Windows 3.1 KERNEL.EXE exports approximately 500 ordinal entries.
WinDOS currently implements 23 ordinals.

**Implemented ordinals:**

| Category          | APIs                                                       |
|-------------------|------------------------------------------------------------|
| File I/O          | `_lopen`, `_lclose`, `_lread`, `_lwrite`, `_llseek`       |
| Module management | `GetModuleHandle`, `GetModuleFileName`, `GetProcAddress`,  |
|                   | `LoadLibrary`, `FreeLibrary`                               |
| Global memory     | `GlobalAlloc`, `GlobalReAlloc`, `GlobalFree`,              |
|                   | `GlobalLock`, `GlobalUnlock`                               |
| Local memory      | `LocalAlloc`, `LocalFree`, `LocalLock`, `LocalUnlock`      |
| Task / process    | `GetCurrentTask`, `Yield`, `InitTask`,                     |
|                   | `WaitEvent`, `PostEvent`                                   |
| Atoms             | `GlobalAddAtom`, `GlobalDeleteAtom`,                       |
|                   | `GlobalFindAtom`, `GlobalGetAtomName`                      |
| String / resource | `LoadString`, `FindResource`, `LoadResource`,              |
|                   | `LockResource` (all stubs returning zero/NULL)             |

**Missing critical KERNEL.EXE APIs (non-exhaustive):**

- `GetVersion` / `GetWinFlags`
- `GetWindowsDirectory` / `GetSystemDirectory`
- `GlobalSize` / `GlobalFlags` / `GlobalHandle`
- `LocalSize` / `LocalReAlloc` / `LocalFlags` / `LocalHandle`
- `LockSegment` / `UnlockSegment`
- `GetDOSEnvironment`
- `Catch` / `Throw`
- `FatalExit` / `FatalAppExit`
- `WinExec`
- `ExitWindows`
- `GetPrivateProfileString` / `GetProfileString` (INI file APIs)
- `WritePrivateProfileString` / `WriteProfileString`
- `GetTickCount` (implemented in `ne_driver`, not in KERNEL stubs)
- `GetFreeSpace` / `GetFreeSystemResources`
- `GlobalCompact` / `LocalCompact`
- `SetErrorMode` / `GetLastError`
- `MakeProcInstance` / `FreeProcInstance`
- `OpenFile`
- `OutputDebugString`
- `IsTask`
- `GetNumTasks`
- Selector manipulation (`AllocSelector`, `FreeSelector`, `ChangeSelector`)
- Toolhelp APIs

### 2.2 USER.EXE Subsystem

**Implemented:**
- Window class registration (`RegisterClass`)
- Window lifecycle (`CreateWindow`, `DestroyWindow`, `ShowWindow`,
  `UpdateWindow`)
- Message loop (`GetMessage`, `PeekMessage`, `TranslateMessage`,
  `DispatchMessage`)
- Message dispatch (`SendMessage`, `PostMessage`, `DefWindowProc`)

**Missing / stub-only:**
- `MessageBox`
- `DialogBox` / `CreateDialog` / `EndDialog` (stub in `ne_resource`)
- `SetCapture` / `ReleaseCapture`
- `GetClientRect` / `GetWindowRect`
- `MoveWindow` / `SetWindowPos`
- `SetWindowText` / `GetWindowText`
- `EnableWindow` / `IsWindowEnabled` / `IsWindowVisible`
- `SetFocus` / `GetFocus`
- `SetTimer` / `KillTimer` (implemented in `ne_driver`, not USER stubs)
- `InvalidateRect` / `ValidateRect`
- `ScrollWindow`
- `GetDC` / `ReleaseDC` (implemented in `ne_gdi`, not USER stubs)
- Menu APIs (`CreateMenu`, `SetMenu`, `GetMenu`, `AppendMenu`, etc.)
- Clipboard APIs (`OpenClipboard`, `CloseClipboard`, `SetClipboardData`,
  `GetClipboardData`)
- Caret APIs (`CreateCaret`, `SetCaretPos`, `ShowCaret`)
- Input APIs (`GetKeyState`, `GetAsyncKeyState`)

### 2.3 GDI.EXE Subsystem

**Implemented:**
- Device context management (`GetDC`, `ReleaseDC`, `BeginPaint`,
  `EndPaint`)

**Stub-only (no visible output):**
- `TextOut`, `MoveTo`, `LineTo`, `Rectangle`, `SetPixel`

**Missing:**
- `BitBlt` / `StretchBlt` / `PatBlt` (raster operations)
- `CreatePen` / `CreateBrush` / `CreateFont` (GDI object creation)
- `SelectObject` / `DeleteObject` (GDI object management)
- `SetTextColor` / `SetBkColor` / `SetBkMode`
- `GetTextMetrics` / `GetTextExtent`
- `CreateCompatibleDC` / `CreateCompatibleBitmap`
- `Ellipse` / `Polygon` / `Polyline`
- `CreateBitmap` / `CreateDIBitmap`
- TrueType font rendering (bitmap fonts only)

### 2.4 Device Drivers

| Driver     | Status                                                    |
|------------|-----------------------------------------------------------|
| Keyboard   | Partial – ~40 scan codes mapped; missing brackets,        |
|            | punctuation, keypad, function keys beyond common set      |
| Timer      | Basic – tick counting and timer callbacks implemented     |
| Display    | Minimal – VGA text mode stub; no graphics-mode rendering  |
| Mouse      | Partial – position and button tracking via INT 33h        |
| Printer    | **Not implemented** (PRINTER.DRV)                         |
| Sound      | **Not implemented**                                       |
| Network    | **Not implemented**                                       |

### 2.5 Protected Mode (DPMI)

WinDOS operates in real mode only.  Windows 3.1 Standard and Enhanced
modes use DPMI for protected-mode operation.  This is the largest
architectural gap:

- No DPMI server or client hooks
- No extended memory access beyond 640 KB conventional memory
- No virtual memory or page fault handling
- No selector-based memory protection
- No V86 mode support

Real-mode-only operation limits the maximum addressable memory and
prevents running applications that require protected-mode features.

### 2.6 Resource Management

- `LoadString` – stub returning 0; no string resource table loading
- `FindResource` / `LoadResource` / `LockResource` – stubs returning
  zero/NULL; resource loading deferred to `ne_resource` module
- `ne_resource` provides enumeration and dialog/menu/accelerator
  templates but the KERNEL stubs do not yet delegate to it

### 2.7 Subsystem Integration

- OLE2 / COM – not implemented
- DDE / Network DDE – not implemented
- Multimedia (MMSYSTEM.DLL) – not implemented
- Help system (WINHELP.EXE) – not implemented
- INI file management – `GetProfileString` / `WriteProfileString` family
  not implemented
- Clipboard cross-application support – unreliable

### 2.8 Platform and Build

- 16-bit DOS context switching: assembly routines are defined but noted
  as requiring a separate Watcom assembly module for real 8086 register
  save/restore
- DOS INT 21h file I/O: `ne_kernel` file APIs use POSIX wrappers on the
  host; Watcom target uses `open`/`close`/`read`/`write` which may need
  direct INT 21h calls for full DOS compatibility
- Fixed-capacity tables: atom table (64), window classes (32), window
  instances (32), DCs (32), module table (fixed) may be too small for
  complex applications

---

## 3 – Assessment

**WinDOS is not yet a full replacement for the Windows 3.1 Kernel.**

The NE executable loading pipeline (parsing, loading, relocation, module
management, import/export resolution) is complete and well-tested.  The
cooperative multitasking runtime and memory management subsystems provide
a functional foundation.

However, the API surface coverage is approximately 5% of KERNEL.EXE
exports, and USER.EXE and GDI.EXE subsystems are at early-stub level.
Protected-mode operation is entirely absent, which limits the usable
application set to real-mode-compatible programs.

Applications that perform only basic file I/O, memory allocation, and
window message processing may run.  Applications that use INI files,
dialogs, clipboard, GDI drawing, printing, OLE2, multimedia, or
protected-mode features will not function correctly.

---

## 4 – Final Roadmap: Missing Steps

The items below are grouped into phases ordered by priority and
dependency.  Each item is actionable and self-contained.

### Phase A – Critical KERNEL.EXE API Expansion

These APIs are required by nearly every Windows 3.1 application.

- [x] Implement `GetVersion` / `GetWinFlags` to report OS version and
      capability flags
- [x] Implement `GetWindowsDirectory` / `GetSystemDirectory`
- [x] Implement `GetDOSEnvironment`
- [x] Implement `WinExec` for launching child applications
- [x] Implement `ExitWindows` for clean shutdown
- [x] Implement `FatalExit` / `FatalAppExit` for error termination
- [x] Implement `GetTickCount` in KERNEL stubs (delegate to `ne_driver`)
- [x] Implement `Catch` / `Throw` for non-local jumps
- [x] Implement `MakeProcInstance` / `FreeProcInstance` for callback
      thunks
- [x] Implement `OpenFile` with full ofstruct semantics
- [x] Implement `OutputDebugString` for debug output
- [x] Implement `SetErrorMode` / `GetLastError`
- [x] Implement `IsTask` / `GetNumTasks`

### Phase B – INI File and Profile APIs

Required by most Windows 3.1 applications for configuration.

- [x] Implement `GetProfileString` / `GetProfileInt`
- [x] Implement `WriteProfileString`
- [x] Implement `GetPrivateProfileString` / `GetPrivateProfileInt`
- [x] Implement `WritePrivateProfileString`
- [x] Handle WIN.INI and application-specific INI file paths

### Phase C – Extended Memory APIs

Required for correct memory management reporting and compaction.

- [x] Implement `GlobalSize` / `GlobalFlags` / `GlobalHandle`
- [x] Implement `LocalSize` / `LocalReAlloc` / `LocalFlags` /
      `LocalHandle`
- [x] Implement `GlobalCompact` / `LocalCompact`
- [x] Implement `GetFreeSpace` / `GetFreeSystemResources`
- [x] Implement `LockSegment` / `UnlockSegment`

### Phase D – USER.EXE Expansion

Required for dialog-based applications and richer window management.

- [ ] Implement `MessageBox`
- [ ] Implement `DialogBox` / `CreateDialog` / `EndDialog` with full
      dialog template support
- [ ] Implement `SetCapture` / `ReleaseCapture`
- [ ] Implement `GetClientRect` / `GetWindowRect`
- [ ] Implement `MoveWindow` / `SetWindowPos`
- [ ] Implement `SetWindowText` / `GetWindowText`
- [ ] Implement `EnableWindow` / `IsWindowEnabled` / `IsWindowVisible`
- [ ] Implement `SetFocus` / `GetFocus`
- [ ] Implement `InvalidateRect` / `ValidateRect`
- [ ] Implement `ScrollWindow`
- [ ] Wire `SetTimer` / `KillTimer` from `ne_driver` into USER stubs
- [ ] Implement clipboard APIs (`OpenClipboard`, `CloseClipboard`,
      `SetClipboardData`, `GetClipboardData`)
- [ ] Implement caret APIs (`CreateCaret`, `SetCaretPos`, `ShowCaret`)
- [ ] Implement input state APIs (`GetKeyState`, `GetAsyncKeyState`)
- [ ] Implement menu creation and management APIs (`CreateMenu`,
      `SetMenu`, `AppendMenu`, `GetMenu`)

### Phase E – GDI.EXE Rendering

Required for any visible graphical output.

- [ ] Implement actual rendering backend for VGA graphics mode
- [ ] Implement `TextOut` with bitmap font rendering
- [ ] Implement line and shape drawing (`MoveTo`, `LineTo`, `Rectangle`,
      `Ellipse`, `Polygon`, `Polyline`)
- [ ] Implement `SetPixel` / `GetPixel` with real framebuffer access
- [ ] Implement GDI object creation (`CreatePen`, `CreateBrush`,
      `CreateFont`)
- [ ] Implement `SelectObject` / `DeleteObject`
- [ ] Implement `SetTextColor` / `SetBkColor` / `SetBkMode`
- [ ] Implement `GetTextMetrics` / `GetTextExtent`
- [ ] Implement `BitBlt` / `StretchBlt` / `PatBlt`
- [ ] Implement `CreateCompatibleDC` / `CreateCompatibleBitmap`
- [ ] Implement bitmap and DIB support (`CreateBitmap`,
      `CreateDIBitmap`)

### Phase F – Driver Completion

- [ ] Expand keyboard scan-code table to cover full 101/102-key layout
- [ ] Implement graphics-mode display driver (VGA 640×480×16,
      320×200×256)
- [ ] Implement printer driver interface (PRINTER.DRV)
- [ ] Complete mouse driver with cursor rendering and event coalescing

### Phase G – KERNEL Resource Stub Wiring

Connect the existing `ne_resource` module to the KERNEL API stubs.

- [ ] Wire `ne_kernel_load_string` to `ne_resource` string table lookup
- [ ] Wire `ne_kernel_find_resource` / `ne_kernel_load_resource` /
      `ne_kernel_lock_resource` to `ne_resource` module
- [ ] Verify resource loading with stock Windows 3.1 system DLLs

### Phase H – Protected-Mode (DPMI) Support

Required for Windows 3.1 Standard and Enhanced mode applications.

- [ ] Implement a minimal DPMI server (INT 31h) for 16-bit protected
      mode
- [ ] Implement selector allocation and management (`AllocSelector`,
      `FreeSelector`, `ChangeSelector`)
- [ ] Implement extended memory access via DPMI
- [ ] Implement descriptor table management
- [ ] Test with Standard-mode Windows 3.1 applications

### Phase I – Additional Subsystems

Lower priority; required for full application compatibility.

- [ ] Implement OLE2 / COM subsystem basics
- [ ] Implement DDE support
- [ ] Implement multimedia APIs (MMSYSTEM.DLL – waveOut, midiOut,
      timer)
- [ ] Implement help system integration (WINHELP.EXE)
- [ ] Implement TrueType font rendering
- [ ] Implement sound driver (PC speaker / Sound Blaster basics)
- [ ] Implement network APIs (NetBIOS, WinSock basics)

### Phase J – Hardening and Validation

- [ ] Expand compatibility matrix with additional stock applications
      (Program Manager, File Manager, Paint, Terminal)
- [ ] Stress-test memory management under sustained multi-application
      workloads
- [ ] Increase fixed-capacity table limits (atom table, window classes,
      DCs) based on real-world usage profiling
- [ ] Perform full regression testing after each phase above
- [ ] Update CHANGELOG.md and known-issues list after each phase

---

## 5 – Priority Summary

| Phase | Description                        | Priority | Effort   |
|-------|------------------------------------|----------|----------|
| A     | Critical KERNEL.EXE API expansion  | High     | Medium   |
| B     | INI file and profile APIs          | High     | Low      |
| C     | Extended memory APIs               | High     | Low      |
| D     | USER.EXE expansion                 | High     | High     |
| E     | GDI.EXE rendering                  | High     | High     |
| F     | Driver completion                  | Medium   | Medium   |
| G     | KERNEL resource stub wiring        | Medium   | Low      |
| H     | Protected-mode (DPMI) support      | Medium   | Very High|
| I     | Additional subsystems (OLE2, etc.) | Low      | Very High|
| J     | Hardening and validation           | Medium   | Medium   |

---

## 6 – Conclusion

WinDOS provides a solid NE executable loading and runtime foundation
but is not yet a full Windows 3.1 kernel replacement.  Phases A through
E above address the most impactful gaps and would bring the project to a
state where common Windows 3.1 applications (Notepad, Calculator, Write)
can run with visible output.  Phase H (DPMI) is the largest single work
item and is required for Standard/Enhanced mode application support.

This assessment and the roadmap above should be used to plan subsequent
development milestones.

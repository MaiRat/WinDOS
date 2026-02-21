# Archived Subsystem Code

This directory contains source and test files for subsystems that are
**out of scope** for the current `krnl386.exe` replacement effort.  The
code is preserved here for potential future development.

## Contents

| File | Description |
|------|-------------|
| `src/ne_user.c` / `ne_user.h` | USER.EXE subsystem – window management, message loop, dialog/clipboard/caret/menu APIs |
| `src/ne_gdi.c` / `ne_gdi.h` | GDI.EXE subsystem – device contexts, drawing primitives, GDI objects, rendering backend |
| `tests/test_ne_user.c` | Unit tests for the USER.EXE subsystem |
| `tests/test_ne_gdi.c` | Unit tests for the GDI.EXE subsystem |

## Why These Files Were Moved

The project's current focus is building a drop-in replacement for
`krnl386.exe` as an NE-executable with a DOS stub.  USER.EXE and
GDI.EXE are separate Windows 3.1 system modules and are not part of the
kernel replacement.

## Re-enabling These Modules

To restore these modules for active development:

1. Move the source files back to `src/`:
   ```
   mv archive/future/src/ne_user.* src/
   mv archive/future/src/ne_gdi.* src/
   ```
2. Move the test files back to `tests/`:
   ```
   mv archive/future/tests/test_ne_user.c tests/
   mv archive/future/tests/test_ne_gdi.c tests/
   ```
3. Re-add the build rules to the `Makefile` (see git history for the
   original rules).
4. Run `make host-test` to verify everything compiles and passes.

## Note on ne_driver

The `ne_driver` module (device drivers) remains in `src/` because the
kernel module (`ne_kernel`) depends on it for `GetTickCount` delegation
and other driver services.  If driver code is eventually split into its
own replacement module, the kernel dependency should be refactored first.

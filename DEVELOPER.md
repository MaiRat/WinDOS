# WinDOS Developer Guide

## Overview

WinDOS is a replacement kernel for Windows 3.1 targeting 16-bit
real-mode DOS.  This guide covers the project architecture, build
system, coding conventions, testing approach, and contribution workflow.

## Architecture

### Module Structure

The kernel replacement is organised into discrete C modules, each
implementing one subsystem of the Windows 3.1 kernel:

```
src/
├── ne_parser.c / .h      # NE executable format parser
├── ne_loader.c / .h      # NE file loader (segment allocation)
├── ne_reloc.c / .h       # Relocation management
├── ne_module.c / .h      # Module table and lifecycle
├── ne_impexp.c / .h      # Import/export resolution
├── ne_task.c / .h        # Task (process) management
├── ne_mem.c / .h         # Memory management (GMEM/LMEM)
├── ne_trap.c / .h        # Exception and trap handling
├── ne_integrate.c / .h   # Subsystem integration layer
├── ne_fullinteg.c / .h   # Full integration validation
├── ne_kernel.c / .h      # KERNEL.EXE API stubs
├── ne_user.c / .h        # USER.EXE subsystem interface
├── ne_gdi.c / .h         # GDI.EXE subsystem interface
├── ne_driver.c / .h      # Device drivers (keyboard, timer, display, mouse)
├── ne_segmgr.c / .h      # Segment discard/reload manager
├── ne_resource.c / .h    # Resource management (dialogs, menus, accelerators)
├── ne_compat.c / .h      # Compatibility testing and hardening
├── ne_release.c / .h     # Release readiness validation
└── ne_dosalloc.h         # Portable memory allocation macros
```

### Dual-Target Build

Every module compiles under two targets:

1. **Watcom / DOS 16-bit** (`wcc -ml -za99`): The production target.
   Uses DOS INT 21h for memory allocation, INT 09h/08h/33h for device
   drivers, and real-mode context switching.

2. **Host / POSIX** (`cc -std=c99`): The development and CI target.
   Uses standard C library functions (`malloc`, `free`, `ucontext`)
   as stand-ins for the DOS-specific paths.

The `ne_dosalloc.h` header provides `NE_MALLOC`, `NE_CALLOC`, and
`NE_FREE` macros that expand to the appropriate implementation based
on the `__WATCOMC__` preprocessor symbol.

### Data Flow

```
 NE file on disk
      │
      ▼
 ne_parser ──► ne_loader ──► ne_reloc
                  │              │
                  ▼              ▼
             ne_module ◄── ne_impexp
                  │
                  ▼
             ne_kernel ──► ne_user / ne_gdi
                  │
                  ▼
             ne_task ──► ne_mem ──► ne_trap
                  │
                  ▼
             ne_driver (keyboard, timer, display, mouse)
```

## Build System

### Makefile Targets

| Target       | Description                                        |
|-------------|----------------------------------------------------|
| `all`        | Build all object files and test binaries (Watcom)  |
| `test`       | Build and run all tests (Watcom / DOS)             |
| `host-test`  | Build and run all tests with host C compiler       |
| `clean`      | Remove all build artefacts                         |
| `host-clean` | Remove host-built test binaries only               |

### Building

```bash
# Full Watcom build:
make all

# Host (CI) tests:
make host-test

# Clean everything:
make clean
```

### Compiler Flags

**Watcom (`wcc`):**
- `-ml` : Large memory model (far code and data)
- `-za99` : C99 language extensions
- `-wx` : All warnings enabled
- `-d2` : Full debug symbols
- `-i=` : Include path

**Host (`cc`/`gcc`):**
- `-std=c99` : C99 standard
- `-Wall -Wextra` : All warnings
- `-I$(CURDIR)/src` : Include path

## Testing

### Test Organisation

Each source module has a corresponding test file:

```
tests/
├── test_ne_parser.c
├── test_ne_loader.c
├── test_ne_reloc.c
├── ...
└── test_ne_release.c
```

### Test Framework

Tests use a minimal in-tree test framework based on macros:

```c
TEST_BEGIN("description of test case");
ASSERT_EQ(actual, expected);
ASSERT_NOT_NULL(pointer);
ASSERT_STR_EQ(str_a, str_b);
TEST_PASS();
```

Global counters (`g_tests_run`, `g_tests_passed`, `g_tests_failed`)
track results.  Each test file's `main()` prints a summary and returns
non-zero on failure.

### Running Tests

```bash
# Run all host tests (recommended for development):
make host-test

# Run a single test module:
cc -std=c99 -Wall -Wextra -Isrc src/ne_parser.c tests/test_ne_parser.c \
   -o build/host_test_parser && build/host_test_parser
```

### Writing Tests

1. Create `tests/test_ne_<module>.c`.
2. Include the module header: `#include "../src/ne_<module>.h"`.
3. Copy the test framework macros from an existing test file.
4. Write test functions using `TEST_BEGIN` / `ASSERT_*` / `TEST_PASS`.
5. Call all test functions from `main()`.
6. Add the test to the `Makefile` (both Watcom and `host-test` targets).

## Coding Conventions

### Style

- **Language**: C99 (`-std=c99` / `-za99`).
- **Indentation**: 4 spaces, no tabs.
- **Braces**: K&R style (opening brace on same line for functions).
- **Naming**: `snake_case` for functions and variables; `UPPER_CASE`
  for macros and constants; `NEPascalCase` for type names.
- **Comments**: Block comments (`/* ... */`) for file and function
  headers; inline comments for non-obvious logic.
- **Line length**: 80 characters preferred, 100 maximum.

### Header Guards

```c
#ifndef NE_MODULE_H
#define NE_MODULE_H
/* ... */
#endif /* NE_MODULE_H */
```

### Error Handling

- Functions return `int` error codes (`0` = success, negative = error).
- NULL pointer arguments are checked at the top of every public function.
- Error code constants are defined per module: `NE_<MODULE>_ERR_*`.

### Memory Allocation

- Always use `NE_MALLOC` / `NE_CALLOC` / `NE_FREE` from `ne_dosalloc.h`.
- Never call `malloc` / `free` directly.
- Check allocation return values; return an error code on failure.

## Contribution Workflow

### Getting Started

1. Fork the repository on GitHub.
2. Clone your fork:
   ```bash
   git clone https://github.com/<your-username>/WinDOS.git
   cd WinDOS
   ```
3. Create a feature branch:
   ```bash
   git checkout -b feature/my-change
   ```

### Making Changes

1. Make your changes in the appropriate `src/` and `tests/` files.
2. Build and run the host tests:
   ```bash
   make host-test
   ```
3. Ensure all tests pass with zero failures.
4. If you add a new module, update the `Makefile` with both Watcom and
   host-test build rules.

### Commit Messages

- Use imperative mood: "Add parser for NE resource table"
- Keep the first line under 72 characters.
- Reference issue numbers where applicable: "Fix #42: handle empty
  segment table".

### Submitting a Pull Request

1. Push your branch to your fork:
   ```bash
   git push origin feature/my-change
   ```
2. Open a Pull Request against the `main` branch.
3. Describe the change, reference any related issues, and confirm that
   `make host-test` passes.
4. Address review feedback and update the PR.

### Code Review Checklist

- [ ] All existing tests still pass.
- [ ] New functionality has corresponding tests.
- [ ] No compiler warnings with `-Wall -Wextra`.
- [ ] Memory allocations use `NE_MALLOC` / `NE_CALLOC` / `NE_FREE`.
- [ ] NULL pointer checks on all public function parameters.
- [ ] Error codes follow the `NE_<MODULE>_ERR_*` convention.
- [ ] Documentation updated if APIs changed.

## Project Roadmap

The project roadmap is maintained in two documents:

- **[ReadME.md](ReadME.md)** – Original 9-step implementation plan.
- **[ROADMAP.md](ROADMAP.md)** – Extended roadmap covering Phases 1–7
  (DOS bring-up through release readiness).

## License

See the repository root for license information.

# WinDOS Installation Guide

## Overview

WinDOS is a replacement kernel for Windows 3.1, targeting real-mode DOS
environments.  This guide covers the requirements, file placement, and
boot procedure needed to run WinDOS on a DOS 5.0+ system or emulator.

## System Requirements

### Hardware (or Emulator)

| Component | Minimum                | Recommended              |
|-----------|------------------------|--------------------------|
| CPU       | Intel 8086/8088        | Intel 80286 or higher    |
| RAM       | 640 KB conventional    | 640 KB + extended memory |
| Storage   | 2 MB free disk space   | 5 MB free disk space     |
| Display   | VGA-compatible adapter | VGA with 256 KB VRAM     |
| Input     | Keyboard               | Keyboard + mouse         |

### Software

- **DOS 5.0 or later** (MS-DOS, PC DOS, FreeDOS, or compatible).
- **Open Watcom C compiler** (v2.0+) for building from source.
  Download from <https://github.com/open-watcom/open-watcom-v2>.

### Emulator Support

WinDOS has been validated on the following emulators:

| Emulator       | Version | Status  |
|----------------|---------|---------|
| DOSBox         | 0.74-3  | Works   |
| DOSBox-X       | 2024+   | Works   |
| QEMU (FreeDOS) | 8.0+    | Works   |
| 86Box           | 4.0+    | Works   |

## Building from Source

### Prerequisites

1. Install Open Watcom v2 and ensure `wcc` and `wlink` are on your `PATH`.
2. Clone the WinDOS repository:
   ```
   git clone https://github.com/MaiRat/WinDOS.git
   cd WinDOS
   ```

### Build Commands

```bash
# Build all modules and test binaries (Watcom / DOS target):
make all

# Build and run tests using the host POSIX compiler (CI / development):
make host-test

# Clean build artefacts:
make clean
```

### Build Output

All build artefacts are placed in the `build/` directory.  The primary
binary is the WinDOS kernel replacement module.  Test executables are
named `test_ne_*.exe` (Watcom) or `host_test_*` (host).

## Installation on DOS

### Step 1 – Prepare the Target Drive

Boot into DOS and ensure you have at least 2 MB of free disk space on
the target drive (typically `C:`).

### Step 2 – Create the WinDOS Directory

```
C:\> MKDIR C:\WINDOS
```

### Step 3 – Copy Files

Copy the following files from the build output to `C:\WINDOS`:

| File             | Description                              |
|------------------|------------------------------------------|
| `WINDOS.EXE`     | WinDOS kernel replacement binary         |
| `KERNEL.EXE`     | KERNEL API stub module                   |
| `USER.EXE`       | USER subsystem interface module          |
| `GDI.EXE`        | GDI subsystem interface module           |

```
C:\> COPY A:\BUILD\*.EXE C:\WINDOS\
```

### Step 4 – Copy Windows 3.1 System Files

WinDOS requires the original Windows 3.1 system files for application
compatibility.  Copy the following from a licensed Windows 3.1
installation:

```
C:\> COPY D:\WINDOWS\SYSTEM\*.DLL C:\WINDOS\SYSTEM\
C:\> COPY D:\WINDOWS\SYSTEM\*.DRV C:\WINDOS\SYSTEM\
C:\> COPY D:\WINDOWS\SYSTEM\*.FON C:\WINDOS\SYSTEM\
```

### Step 5 – Configure WINDOS.INI

Create `C:\WINDOS\WINDOS.INI` with the following minimal configuration:

```ini
[windos]
SystemDir=C:\WINDOS\SYSTEM
TempDir=C:\WINDOS\TEMP

[display]
Driver=VGA

[keyboard]
Type=Enhanced

[mouse]
Driver=INT33
```

## Boot Procedure

### Manual Boot

From the DOS command line:

```
C:\> CD \WINDOS
C:\WINDOS> WINDOS.EXE
```

### Automatic Boot via AUTOEXEC.BAT

To start WinDOS automatically at boot, add the following line to the
end of `C:\AUTOEXEC.BAT`:

```
C:\WINDOS\WINDOS.EXE
```

## Verifying the Installation

After booting into WinDOS, verify the following:

1. **Kernel loads** – The WinDOS banner appears with version information.
2. **System DLLs resolve** – No "missing module" errors during startup.
3. **Display driver initialises** – The screen switches to the configured
   display mode.
4. **Keyboard input works** – Keypresses are received and processed.
5. **A test application launches** – Try launching `NOTEPAD.EXE` from the
   Windows 3.1 accessories.

## Troubleshooting

| Symptom                        | Possible Cause               | Solution                         |
|-------------------------------|------------------------------|----------------------------------|
| "Bad command or file name"     | WinDOS not in PATH           | Run from `C:\WINDOS` directory   |
| "Module not found: KERNEL.EXE"| Missing system files         | Re-copy files per Step 3         |
| Screen remains in text mode   | Display driver not configured| Check `WINDOS.INI` [display]     |
| No keyboard response          | IRQ conflict                 | Verify INT 09h not hooked by TSR |
| Out of memory                 | Insufficient conventional RAM| Free memory; unload TSRs         |

## Uninstallation

To remove WinDOS, delete the installation directory:

```
C:\> DELTREE C:\WINDOS
```

Remove any WinDOS lines from `AUTOEXEC.BAT` if automatic boot was
configured.

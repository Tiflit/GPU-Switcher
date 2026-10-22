# GPU-Switcher 1.2.0

GPU-Switcher holds a Direct3D 11 device on a discrete GPU. On compatible NVIDIA
Advanced Optimus systems, the configured application profile can trigger panel
switching while the utility is running.

## Changes

- Transactional GPU acquisition retains a healthy device if a preferred-adapter
  creation attempt fails, and tries other eligible discrete adapters.
- Adapter identity tracking, hot-plug handling, and periodic recovery checks.
- Suspend releases the held device; resume delays reacquisition and coalesces
  duplicate wake notifications. The health check backs up a missing resume timer.
- Restart Display Adapters supervises an elevated helper, preserves initially
  disabled/stopped devices, verifies recovery, and reports incomplete resets.
  Cooperating reset helpers are serialized by a machine-wide mutex.
- A successful reset now exits GPU-Switcher without relaunching, allowing the
  dGPU to become inactive after other clients have been cleared. Cancelling UAC
  keeps the utility running and recovers acquisition.
- Startup checks the tray icon and health timer before acquiring a device.
  Failed tray restoration releases the device and retries without abandoning
  reset supervision.
- View Log command, build revision in startup logs, and corrected UTF-8 output.
- Four automated regression suites in Windows build and release workflows.

## Setup and use

On NVIDIA Advanced Optimus systems, configure the GPU-Switcher application
profile with **Automatic Display Switching: On** as described in README.md.
On the tested Legion, removing and recreating an old NVIDIA App profile resolved
activation without panel switching; an adapter reset was not needed to apply it.

Ordinary Exit releases GPU-Switcher's own device. Other applications may retain
NVIDIA contexts even after the panel returns to Intel. Restart Display Adapters
explicitly cycles the eligible PCI display adapters to clear existing clients,
then exits on verified success. It requires UAC approval. Save work and close
graphics workloads first; other applications can acquire the GPU again later.

A brief yellow tray icon during startup or recovery followed by NVIDIA green is
expected. The icon indicates application/device state, not measured panel routing.

## Validation and limits

Validated on one Lenovo Legion Slim 7 16IRH8 with Intel Iris Xe and an RTX 4060
Laptop GPU: launch and display switching, sleep/resume, UAC cancellation,
reset-to-exit, Explorer tray restoration, and ordinary Exit. See
[the validation record](https://github.com/Tiflit/GPU-Switcher/blob/v1.2.0/docs/LEGION-VALIDATION-2026-10-22.md) for exact tested builds and evidence.

The automated suites cover acquisition ordering/lifetime, power transitions,
reset policy, and cross-process reset exclusion. They do not reset physical
hardware or establish compatibility with every GPU or driver. Physical multi-GPU
fallback, permanent reset failures, injected Win32 API failures, and cross-account
or cross-session elevation remain unverified. Adapter classification remains
heuristic; reset recovery is bounded and cannot guarantee restoration.

## Download

Use `GPU-Switcher-v1.2.0-windows-x64.zip`. It contains GPU-Switcher.exe, README.md,
LICENSE, and the docs directory. A companion `.zip.sha256` file contains the ZIP's
SHA-256 checksum. Extract before running; no installer is required.

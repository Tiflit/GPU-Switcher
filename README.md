<p align="center">
  <img src="https://github.com/Tiflit/GPU-Switcher/blob/main/src/icons/png/main-rainbow-128.png" height="90" alt="GPU-Switcher Logo">
</p>

<h1 align="center">GPU‑Switcher</h1>

<p align="center">
  <strong>A lightweight, near-zero overhead GPU switching utility for Windows</strong>
</p>

<p align="center">
  <a href="https://github.com/Tiflit/GPU-Switcher/releases"><img src="https://img.shields.io/github/v/release/Tiflit/GPU-Switcher?color=blue" alt="Latest Release"></a>
  <img src="https://img.shields.io/badge/platform-Windows%2010%20%7C%2011%20(64--bit)-0078D6" alt="Windows 10 / 11 64-bit">
  <img src="https://img.shields.io/badge/DirectX-D3D11-orange" alt="DirectX 11">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-green" alt="MIT License"></a>
</p>

&nbsp;

**GPU-Switcher** is a small Windows system tray utility that keeps your laptop's discrete GPU (dGPU) activated with **near-zero CPU usage** and no persistent background rendering loops.

Designed primarily for hybrid laptops equipped with **NVIDIA Advanced Optimus**. It can keep the dGPU active without a game or benchmark running. Holding the dGPU active can increase power use; display switching depends on the laptop hardware, driver, and application profile. It does not directly control a hardware MUX.

---

## Features

- ⚡ **Near-Zero Overhead**: Creates a persistent Direct3D 11 device on the dGPU and sleeps in the Windows message pump. No persistent rendering loop or telemetry. Event handlers and a 30-second health check still perform work; CPU usage has not been benchmarked.
- 🎨 **Dynamic Vendor Tray Icons**: See which GPU device the utility holds with color-coded tray icons:
  - 🟢 **Green**: Active NVIDIA discrete GPU
  - 🔴 **Red**: Active AMD discrete GPU
  - 🔵 **Blue**: Active Intel discrete GPU
  - ⚪ **Grey**: Other / Unknown adapter
  - 🟡 **Yellow**: Starting, acquiring, waiting, suspended/resuming, resetting, or recovering; a brief yellow-to-vendor-color transition is expected
- 🩺 **Live Health Monitoring & eGPU Auto-Recovery**: Tracks discrete adapters by **Locally Unique Identifier (LUID)** and provides event-driven hot-plug detection via `WM_DEVICECHANGE` (`DBT_DEVNODES_CHANGED`) with periodic retry fallback. After adapter loss, it retries eligible discrete adapters and attempts to return to the preferred adapter when available. Physical eGPU and multi-dGPU recovery have not yet been validated.
- 💤 **Non-Blocking Power Management**: Flushes and releases the DirectX context cleanly before sleep (`PBT_APMSUSPEND`), and re-acquires automatically with a brief delay upon system resume using a timer rather than a blocking resume delay. Device creation itself runs on the UI thread.
- 🔄 **Verified Display Adapter Restart**: Cycles eligible PCI display adapters using the Windows Configuration Manager API (`CfgMgr32`), skips known virtual/software devices, checks device-start status via `CM_Get_DevNode_Status` (`DN_STARTED`), and surfaces partial or failed reset results.
- 🖥️ **CLI Controls**: Supports `--exit` / `--quit` for graceful shutdown from scripts or terminals, and `--help` for usage information.
- 🔔 **Single-Instance Aware**: Launching a duplicate instance highlights and displays the active GPU status balloon from the existing tray process.
- 🔍 **High-DPI & Per-User Logging**: Full Per-Monitor V2 DPI awareness and rolling diagnostic logs stored in `%LOCALAPPDATA%\GPU-Switcher\gpu_switcher.log`.

---

## Download & Installation

GPU-Switcher is a standalone portable application:

1. Download the latest `GPU-Switcher-*-windows-x64.zip` from the **[Releases](https://github.com/Tiflit/GPU-Switcher/releases)** page.
2. Extract the ZIP contents to a permanent location (e.g., `C:\Program Files\GPU-Switcher` or your tools directory).
3. Run `GPU-Switcher.exe`. It will appear in your notification area (system tray).

---

## One-Time GPU Preference Setup

GPU-Switcher explicitly selects an eligible discrete DXGI adapter. Windows and vendor preferences can provide additional driver hints, but the NVIDIA application profile controls the Advanced Optimus behavior tested here.

### Step 1: Windows Graphics Settings (Recommended)
1. Open Windows **Settings → System → Display → Graphics** (or search "Graphics Settings" in the Start menu).
2. Under **Custom options for apps**, click **Browse** and select `GPU-Switcher.exe`.
3. Click **Options** on the newly added entry and select **High performance** (identifying your discrete GPU, e.g. NVIDIA / AMD / Intel Arc).
4. Click **Save**.

### Step 2: NVIDIA application profile (for Advanced Optimus laptops)
For compatible NVIDIA Advanced Optimus laptops, configure the program profile. Available controls vary by driver and laptop. If using NVIDIA Control Panel:
1. Open **NVIDIA Control Panel**.
2. Navigate to **Manage 3D settings → Program Settings**.
3. Click **Add** and select `GPU-Switcher.exe`.
4. Set **Preferred graphics processor** to **High-performance NVIDIA processor**.
5. Ensure **Automatic display switching** is enabled in your global display mode settings.
6. Right-click the tray icon and select **Exit**, then relaunch `GPU-Switcher.exe`.

If configuring the executable through **NVIDIA App**, set its program option
**Automatic Display Switching** to **On**, then fully exit and relaunch the
utility. The [Legion validation report](docs/LEGION-VALIDATION-2026-10-22.md)
records successful ordinary launch switching after recreating that program
profile; a display-adapter reset was not required for that first successful launch.

On the tested Legion this configuration successfully triggered switching. It does not establish the same result on every hybrid laptop; confirm actual panel routing separately from the tray icon.

---

## Tray Controls

- **Hover**: Displays the active GPU description in the tooltip (e.g. `GPU-Switcher: NVIDIA GeForce RTX 4060 Laptop GPU`).
- **Left‑click**: Displays a status balloon with the active GPU name and health state.
- **Right‑click Context Menu**:
  - **Start with Windows**: Toggles a startup entry in `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.
  - **Restart Display Adapters**: Cycles physical display adapters via an elevated helper to clear existing GPU clients or recover from rendering stutters. On verified success, GPU-Switcher exits without relaunching so it does not keep the dGPU active (requires UAC confirmation). Incomplete resets keep the tray open with a warning.
  - **View Log**: Opens the current user's diagnostic log.
  - **Exit**: Cleanly releases the DirectX context and terminates the program; disabled while a reset is being monitored.

---

## Command‑Line Options

| Option | Description |
|---|---|
| *(none)* | Launches the tray utility. If already running, activates and notifies the existing instance. |
| `--exit` / `--quit` | Requests graceful closure of the running instance; closure is blocked while a reset is being monitored. |
| `--help` / `-h` / `/?` | Displays the help dialog with available options. |
| `--reset-gpu` | Helper flag used internally to perform the elevated display adapter reset. |

---

## How It Works

1. **Driver Enablement Hints**: Exports documented vendor activation symbols:
   - `NvOptimusEnablement = 1` (NVIDIA Optimus rendering hint)
   - `AmdPowerXpressRequestHighPerformance = 1` (AMD PowerXpress hint)
2. **Adapter Classification & LUID Tracking**: Enumerates all DXGI adapters, removes software renderers (`DXGI_ADAPTER_FLAG_SOFTWARE`), and uses vendor/VRAM heuristics to classify discrete candidates. These thresholds are not universal, and initial ranking includes vendor and VRAM scores. The first successfully acquired candidate becomes the preferred adapter for the lifetime of the process, tracked by its Windows **Locally Unique Identifier (LUID)**.
3. **Direct3D 11 Pinning**: Creates a persistent `ID3D11Device` on the selected discrete adapter with `D3D11_CREATE_DEVICE_BGRA_SUPPORT`. The held device keeps a GPU client active. On the tested Legion, the configured NVIDIA profile also switches the panel to the dGPU. GPU power state and panel routing remain driver/hardware dependent.
4. **State-Driven, Lightweight Lifecycle**: An explicit state machine (`Active`, `WaitingForDiscreteGpu`, `DeviceLost`, `Resetting`) handles adapter loss, eGPU hot-plugging via `WM_DEVICECHANGE`, system sleep/resume (`WM_POWERBROADCAST`), and taskbar recreation without a persistent rendering loop.
5. **Supervised Adapter Reset**: When display restart is requested, an elevated helper cycles PCI display adapters via CfgMgr32, preserves initially disabled or stopped adapters, retries recovery with refreshed device nodes, poll-verifies driver initialization, and propagates status back to the parent. A machine-wide mutex excludes cooperating GPU-Switcher helpers; it does not coordinate external device-management tools. Recovery is bounded and may fail. The tray monitors completion without a background wait thread; after 60 seconds it warns and keeps monitoring, with acquisition and another reset still blocked.

---

## Compatibility

| System Type | Behavior |
|---|---|
| **NVIDIA Advanced Optimus** | Device activation and profile-driven display switching validated on one Intel + RTX 4060 Legion; other systems require testing. |
| **Standard Optimus (Muxless)** | Intended to hold the dGPU active; internal display routing remains on iGPU. Not hardware-validated here. |
| **AMD / Intel discrete GPUs** | Eligible adapters can be selected by the heuristics; physical activation/recovery and display switching remain unverified. |

---

## Building from Source

Requires CMake 3.20+ and Visual Studio 2019/2022 (MSVC) with the Windows 10/11 SDK.

```powershell
# Clone the repository
git clone https://github.com/Tiflit/GPU-Switcher.git
cd GPU-Switcher

# Configure with CMake
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build Release binary
cmake --build build --config Release
```

The compiled binary will be located at `build/Release/GPU-Switcher.exe` (or `build/GPU-Switcher.exe` if using Ninja).

Run all four regression suites with `ctest --test-dir build -C Release --output-on-failure`. See [Validation](docs/VALIDATION.md) for coverage limits and the Legion hardware checklist.

---

## Troubleshooting & Diagnostics

- **Diagnostic Logs**: Use **View Log** in the tray menu. Startup records the version and build commit (or `unknown` when built without Git metadata). A rolling log file is written to `%LOCALAPPDATA%\GPU-Switcher\gpu_switcher.log` (trimmed after exceeding a 16 KB target; concurrent tray/helper writes may lose entries). Check this file if a device fails to acquire or to inspect detected adapter scores.
- **Display Stuttering**: **Restart Display Adapters** can cycle eligible adapters without a reboot, but is not a guaranteed remedy. Save work and close graphics workloads first. A successful reset exits GPU-Switcher; launch it again if you want to hold the dGPU active.
- **NVIDIA Still Active After Exit**: Returning the panel to Intel does not ensure the NVIDIA GPU becomes inactive. Other programs can retain GPU contexts; LockApp was reported in the Legion test. Ordinary Exit releases GPU-Switcher's own device. To clear other GPU clients, explicitly choose **Restart Display Adapters**; on verified success the utility exits without relaunching. Other programs may subsequently acquire the GPU again. See the [validation report](docs/LEGION-VALIDATION-2026-10-22.md).
- **Display Not Switching**: Ensure you have configured the program profile in NVIDIA App or NVIDIA Control Panel as described in the [One-Time GPU Preference Setup](#one-time-gpu-preference-setup).

---

## License

This project is licensed under the [MIT License](LICENSE) — Copyright © 2026 Tiflit.

<p align="center">
  <img src="https://github.com/Tiflit/GPU-Switcher/blob/main/src/icons/png/main-rainbow-128.png" height="90" alt="GPU-Switcher Logo">
</p>

<h1 align="center">GPU‑Switcher</h1>

<p align="center">
  <strong>A lightweight, zero-overhead GPU switching utility for Windows</strong>
</p>

<p align="center">
  <a href="https://github.com/Tiflit/GPU-Switcher/releases"><img src="https://img.shields.io/github/v/release/Tiflit/GPU-Switcher?color=blue" alt="Latest Release"></a>
  <img src="https://img.shields.io/badge/platform-Windows%2010%20%7C%2011%20(64--bit)-0078D6" alt="Windows 10 / 11 64-bit">
  <img src="https://img.shields.io/badge/DirectX-D3D11-orange" alt="DirectX 11">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-green" alt="MIT License"></a>
</p>

&nbsp;

**GPU-Switcher** is a tiny, battery-friendly Windows system tray utility that keeps your laptop's discrete GPU (dGPU) activated with **zero CPU usage** and no background polling threads.

Designed primarily for hybrid laptops equipped with **NVIDIA Advanced Optimus** (or MUX switch systems) where you want the dGPU ready for low-latency gaming, external displays, or 3D workloads without needing to keep a heavy game or benchmark running in the background.

---

## Features

- ⚡ **Zero Overhead**: Creates a persistent Direct3D 11 device on the dGPU and sleeps in the Windows message pump. No background loops, no telemetry, and 0% CPU consumption.
- 🎨 **Dynamic Vendor Tray Icons**: Instantly see which GPU is active with color-coded tray icons:
  - 🟢 **Green**: Active NVIDIA discrete GPU
  - 🔴 **Red**: Active AMD discrete GPU
  - 🔵 **Blue**: Active Intel discrete GPU
  - ⚪ **Grey**: Other / Unknown adapter
  - 🟡 **Yellow**: Display reset in progress or dGPU acquisition warning
- 🩺 **Live Health Monitoring & eGPU Auto-Recovery**: Tracks discrete adapters by **Locally Unique Identifier (LUID)** and listens for hardware notifications (`WM_DEVICECHANGE` / `DBT_DEVNODES_CHANGED`). If an eGPU is disconnected or driver reset occurs (`GetDeviceRemovedReason()`), the app enters an explicit waiting state without trapping the integrated GPU, and automatically re-acquires the dGPU the instant it reconnects.
- 💤 **Non-Blocking Power Management**: Flushes and releases the DirectX context cleanly before sleep (`PBT_APMSUSPEND`), and re-acquires automatically with a brief delay upon system resume without freezing the message loop.
- 🔄 **Verified Display Adapter Restart**: Safely cycles physical graphics drivers using the Windows Configuration Manager API (`CfgMgr32`), skips virtual/software devices, deterministically verifies driver initialization via `CM_Get_DevNode_Status` (`DN_STARTED`), and propagates granular error codes.
- 🖥️ **CLI Controls**: Supports `--exit` / `--quit` for graceful shutdown from scripts or terminals, and `--help` for usage information.
- 🔔 **Single-Instance Aware**: Launching a duplicate instance highlights and displays the active GPU status balloon from the existing tray process.
- 🔍 **High-DPI & Per-User Logging**: Full Per-Monitor V2 DPI awareness and rolling diagnostic logs stored safely in `%LOCALAPPDATA%\GPU-Switcher\gpu_switcher.log`.

---

## Download & Installation

GPU-Switcher is a standalone portable application:
1. Download the latest `GPU-Switcher-*-windows-x64.zip` from the **[Releases](https://github.com/Tiflit/GPU-Switcher/releases)** page.
2. Extract `GPU-Switcher.exe` to a permanent location (e.g., `C:\Program Files\GPU-Switcher` or your tools directory).
3. Run `GPU-Switcher.exe`. It will appear in your notification area (system tray).

---

## One‑Time GPU Preference Setup

Starting with Windows 10 (version 20H1+) and Windows 11, the operating system's per-application **Graphics Settings** can override driver-level preferences. To ensure your discrete GPU is consistently selected:

### Step 1: Windows Graphics Settings (Recommended)
1. Open Windows **Settings → System → Display → Graphics** (or search "Graphics Settings" in the Start menu).
2. Under **Custom options for apps**, click **Browse** and select `GPU-Switcher.exe`.
3. Click **Options** on the newly added entry and select **High performance** (identifying your discrete GPU, e.g. NVIDIA / AMD / Intel Arc).
4. Click **Save**.

### Step 2: NVIDIA Control Panel (for Advanced Optimus laptops)
For NVIDIA Advanced Optimus laptops to trigger dynamic internal display switching on launch:
1. Open **NVIDIA Control Panel**.
2. Navigate to **Manage 3D settings → Program Settings**.
3. Click **Add** and select `GPU-Switcher.exe`.
4. Set **Preferred graphics processor** to **High-performance NVIDIA processor**.
5. Ensure **Automatic display switching** is enabled in your global display mode settings.
6. Right-click the tray icon and select **Exit**, then relaunch `GPU-Switcher.exe`.

*After this one-time configuration, launching GPU-Switcher will consistently trigger discrete GPU pinning and Advanced Optimus display switching.*

---

## Tray Controls

- **Hover**: Displays the active GPU description in the tooltip (e.g. `GPU-Switcher: NVIDIA GeForce RTX 4060 Laptop GPU`).
- **Left‑click**: Displays a status balloon with the active GPU name and health state.
- **Right‑click Context Menu**:
  - **Start with Windows**: Toggles a startup entry in `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.
  - **Restart Display Adapters**: Safely cycles physical display adapters via elevated helper to fix rendering stutters, then automatically restarts the utility (requires UAC confirmation).
  - **Exit**: Cleanly releases the DirectX context and terminates the program.

---

## Command‑Line Options

| Option | Description |
|---|---|
| *(none)* | Launches the tray utility. If already running, activates and notifies the existing instance. |
| `--exit` / `--quit` | Gracefully closes the running GPU-Switcher instance. |
| `--help` / `-h` / `/?` | Displays the help dialog with available options. |
| `--reset-gpu` | Helper flag used internally to perform the elevated display adapter reset. |

---

## How It Works

1. **Driver Enablement Hints**: Exports documented vendor activation symbols:
   - `NvOptimusEnablement = 1` (NVIDIA Optimus rendering hint)
   - `AmdPowerXpressRequestHighPerformance = 1` (AMD PowerXpress hint)
2. **Adapter Classification & LUID Tracking**: Enumerates all DXGI adapters, removes software renderers (`DXGI_ADAPTER_FLAG_SOFTWARE`), and classifies discrete candidates (NVIDIA, AMD dGPU, Intel Arc) separate from integrated APUs. Remembers the preferred adapter via its Windows **Locally Unique Identifier (LUID)** so transient disconnects do not trap the system into integrated graphics.
3. **Direct3D 11 Pinning**: Creates a persistent `ID3D11Device` on the selected discrete adapter with `D3D11_CREATE_DEVICE_BGRA_SUPPORT`. This signals the graphics driver that a high-performance 3D process is active, preventing the GPU from entering deep sleep and keeping Advanced Optimus routed to the discrete GPU.
4. **State-Driven, Zero-Overhead Lifecycle**: An explicit state machine (`Active`, `WaitingForDiscreteGpu`, `DeviceLost`, `Resetting`) handles adapter loss, eGPU hot-plugging via `WM_DEVICECHANGE`, system sleep/resume (`WM_POWERBROADCAST`), and taskbar recreation with 0% resident CPU usage.
5. **Deterministic Driver Reset**: When display restart is requested, an elevated helper cycles physical PCI display adapters via CfgMgr32, poll-verifies driver initialization with `CM_Get_DevNode_Status` (`DN_STARTED`, problem code 0), and propagates verified status back to the parent.

---

## Compatibility

| System Type | Behavior |
|---|---|
| **NVIDIA Advanced Optimus** | Full dynamic display switching and dGPU activation. |
| **Standard Optimus (Muxless)** | Activates dGPU; internal display routing remains on iGPU (by hardware design). |
| **AMD Hybrid / SmartAccess** | Driver hints exported; display switching depends on OEM MUX architecture. |

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

---

## Troubleshooting & Diagnostics

- **Diagnostic Logs**: A rolling log file is written to `%LOCALAPPDATA%\GPU-Switcher\gpu_switcher.log` (capped at 16 KB with automatic rotation). Check this file if a device fails to acquire or to inspect detected adapter scores.
- **Display Stuttering**: If your laptop panel stutters after switching display modes, use **Restart Display Adapters** from the tray menu to reload the graphics driver stack without rebooting Windows.
- **Display Not Switching**: Ensure you have configured the program profile in NVIDIA Control Panel under Program Settings as described in the [One-Time Setup](#one-time-nvidia-optimus-setup).

---

## License

This project is licensed under the [MIT License](LICENSE) — Copyright © 2026 Tiflit.

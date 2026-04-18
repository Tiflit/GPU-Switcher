# gpu_tray — Lenovo Legion dGPU Tray Switcher

A minimal Windows tray application for Lenovo Legion laptops with Intel iGPU + NVIDIA dGPU.  
**No installation required. Build entirely online via GitHub Actions.**

---

## What it does

| Event | Behaviour |
|-------|-----------|
| **Launch** | Switches to dGPU (Discrete) mode → green **N** icon in tray |
| **Hover tray icon** | Tooltip shows current active GPU mode |
| **Right-click → Revert** | Switches back to Hybrid (iGPU) mid-session |
| **Exit** | Automatically reverts to Hybrid, icon briefly shows blue **I** |

Tray icon key:

- 🟢 **N** — NVIDIA dGPU active (Discrete mode)
- 🔵 **I** — Intel iGPU active (Hybrid/Optimus mode)
- ⚫ **?** — WMI unavailable / mode unknown

---

## Requirements

| | |
|---|---|
| **OS** | Windows 10 / 11 (64-bit) |
| **Device** | Lenovo Legion laptop (any generation with GPU Working Mode support) |
| **Privileges** | **Run as Administrator** (WMI LENOVO_GAMEZONE_DATA requires elevation) |

> **Will it work on non-Legion Lenovo laptops?**  
> Possibly — any Lenovo with the `LENOVO_GAMEZONE_DATA` WMI class should work.  
> This includes IdeaPad Gaming series. Try it and check the tray tooltip.

> **Will it work on non-Lenovo laptops?**  
> No. The WMI interface is Lenovo-specific. ASUS, MSI, Razer etc. each have  
> their own proprietary EC/WMI interfaces for GPU mode switching.

---

## How it works

The tool uses Lenovo's `LENOVO_GAMEZONE_DATA` WMI class (GUID `887B54E3-DDDC-4B2C-8B88-68A26A8835D0`),
the same interface used by LenovoLegionToolkit and Legion Zone internally:

| WMI Method | Method ID | Purpose |
|---|---|---|
| `GetGpuGpsState` | 5 | Read current GPU working mode |
| `SetGpuGpsState` | 6 | Set GPU working mode |

GPU mode values:

| Value | Name | Description |
|---|---|---|
| 1 | Hybrid | iGPU drives display, dGPU renders on-demand (best battery) |
| 2 | HybridIGPU | dGPU fully disconnected (maximum battery saving) |
| 3 | HybridAuto | Auto-switches on AC/battery |
| **4** | **dGPU** | **dGPU drives display directly (best performance)** |

This tool switches between **mode 4** (on launch) and **mode 1** (on exit/revert).

---

## Building (online — no local install needed)

1. **Fork or push** this repository to your GitHub account.
2. Go to **Actions** tab → select **Build gpu_tray** → click **Run workflow**.
3. After ~1 minute, click the completed run → download **`gpu_tray-x64`** artifact.
4. Extract `gpu_tray.exe` and run it as Administrator.

---

## Building locally (optional)

```powershell
# Requires Visual Studio 2019+ with "Desktop development with C++" workload
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

---

## Project structure

```
gpu_tray/
├── main.cpp              # Application logic, WMI switching, tray icon
├── nvapi_interface.h     # Legacy stub (no longer used, kept for reference)
├── gpu_tray.manifest     # DPI awareness + requireAdministrator
├── CMakeLists.txt        # Build definition
└── .github/
    └── workflows/
        └── build.yml     # GitHub Actions CI — builds and uploads .exe
```

---

## Limitations

- Lenovo Legion (and compatible Lenovo) laptops only
- Requires Administrator privileges
- The EC may take 1–2 seconds to apply the mode change after the WMI call
- Switching too frequently may confuse the EC; wait a few seconds between switches

---

## License

MIT — do what you want, no warranty.

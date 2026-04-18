# gpu_tray — Advanced Optimus dGPU Tray Switcher

A minimal Windows tray application for Intel + NVIDIA Advanced Optimus laptops.  
**No installation required. Build entirely online via GitHub Actions.**

---

## What it does

| Event | Behaviour |
|-------|-----------|
| **Launch** | Requests dGPU MUX via NVAPI → green **N** icon in tray |
| **Hover tray icon** | Tooltip shows active GPU name |
| **Right-click → Revert** | Switches back to iGPU mid-session |
| **Exit** | Automatically reverts to iGPU, icon briefly shows blue **I** |

Tray icon key:

- 🟢 **N** — NVIDIA dGPU active  
- 🔵 **I** — Intel iGPU active  
- ⚫ **?** — NVAPI unavailable / unknown state  

---

## Requirements

| | |
|---|---|
| **OS** | Windows 10 / 11 (64-bit) |
| **GPU** | Intel iGPU + NVIDIA dGPU with **Advanced Optimus** (MUX switch) |
| **Driver** | NVIDIA driver **510 or later** |
| **Privileges** | **Run as Administrator** (MUX switch requires elevation on most OEM firmware) |

> **Does it work without Advanced Optimus?**  
> On classic Optimus (no hardware MUX), NVAPI cannot redirect the display output  
> in-session. The tray icon will appear but switching will have no visible effect.  
> The standard approach for non-MUX Optimus is to force dGPU rendering per-app  
> via the NVIDIA Control Panel or a per-process NVAPI profile.

---

## Building (online — no local install needed)

1. **Fork or push** this repository to your GitHub account.
2. Go to **Actions** tab → select **Build gpu_tray** → click **Run workflow**.
3. After ~1 minute, click the completed run → download **`gpu_tray-x64`** artifact.
4. Extract `gpu_tray.exe` and run it as Administrator.

GitHub Actions uses a free Windows Server 2022 runner with MSVC 2022 — you don't need Visual Studio, CMake, or any SDK installed locally.

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
├── main.cpp              # Application logic, NVAPI dynamic loading, tray icon
├── nvapi_interface.h     # Minimal NVAPI typedefs (no SDK required)
├── gpu_tray.manifest     # DPI awareness + requireAdministrator
├── CMakeLists.txt        # Build definition
└── .github/
    └── workflows/
        └── build.yml     # GitHub Actions CI — builds and uploads .exe
```

---

## How NVAPI MUX switching works

NVAPI exposes two undocumented-but-stable function IDs used internally by  
the NVIDIA Control Panel for Advanced Optimus:

| Function | ID | Purpose |
|---|---|---|
| `NvAPI_Disp_SetPreferredDGPU` | `0x5F4C2664` | Request MUX route to dGPU |
| `NvAPI_Disp_GetCurrentDGPUMode` | `0x0DBAC0B0` | Query current MUX state |

Both are loaded at runtime from `nvapi64.dll` (present on any NVIDIA driver install)  
via `nvapi_QueryInterface`. No NVAPI SDK headers or `.lib` files are linked.

> **Note:** Some OEM firmware implementations of Advanced Optimus only apply the  
> MUX change at the next login/display reconnect. If the icon stays at **?** after  
> launch, try logging out and back in after the tool has set the preference.

---

## Limitations

- Windows only (Win32 tray API)
- Advanced Optimus hardware MUX required for display output switching
- AMD dGPU variant would need equivalent ADLX API calls (not implemented)
- MUX switch may require a display reconnect on some OEM firmware

---

## License

MIT — do what you want, no warranty.

# GPU‑Switcher

A tiny Windows tray utility that activates the laptop’s (or any PC, really) discrete GPU (dGPU) on launch and keeps it awake until exit. I needed an easy way to force dGPU display rendering without performance impact. This should allow low-latency streaming for games and a quick toggle to test and run apps on either the iGPU or dGPU for dual-graphics laptops that are difficult to manage.

Designed for hybrid GPU systems (NVIDIA Optimus, AMD Dynamic Switchable Graphics, Intel + NVIDIA, etc.) where you want the dGPU ready without running a heavy application or if you don't want to manually configure every app through the driver control panels.

---

## Requirements

- Windows 10 or 11 (64‑bit)
- Any hybrid GPU laptop (NVIDIA, AMD, Intel)
- D3D11‑capable discrete GPU
- One‑time “High Performance” profile in NVIDIA Control Panel or AMD Radeon Software

---

## One‑time setup (NVIDIA)

1. Run GPU-Switcher.exe
2. Open **NVIDIA Control Panel**  
3. Go to **Manage 3D settings → Program Settings**  
4. Add `GPU‑Switcher.exe`  
5. Set **Preferred graphics processor → High‑performance NVIDIA processor**
6. Enable Automatic Display Switching (or the AMD equivalent)

After this, GPU‑Switcher should consistently trigger a dGPU display switch on laptops with Advanced Optimus enabled.  
(AMD SmartShift / Smart Access Graphics behavior requires additional testing.)

---

## How it works

On startup, GPU‑Switcher:

1. **Exports GPU driver hints**  
   - `NvOptimusEnablement`  
   - `AmdPowerXpressRequestHighPerformance`  
   These tell the driver to prefer the dGPU for this process.

2. **Creates a minimal D3D11 device**  
   The device is created on the adapter with the most dedicated VRAM — always the dGPU on hybrid systems.  
   This registers the process with the GPU driver and makes it visible in NVIDIA Control Panel / AMD Radeon Software.

3. **Sits quietly in the system tray**  
   Zero CPU usage. No polling. No background threads.  
   Exiting the app releases the D3D device and the driver returns to normal routing.

---

## Tray icon

The icon reflects the detected GPU vendor:

| Vendor | Color |
|--------|--------|
| NVIDIA | Green |
| AMD    | Red   |
| Intel  | Blue  |
| Other  | Grey  |

Hovering the icon shows the full GPU name and state, e.g.:

```
NVIDIA GeForce RTX 4070 [dGPU active]
```

Right‑click menu:

- **Run at startup** — toggles a `HKCU\...\Run` entry  
- **Exit** — releases the GPU and removes the tray icon  

---

## Building

Requires:

- CMake 3.20+
- Visual Studio / MSVC with Windows SDK

```
cmake -B build
cmake --build build --config Release
```

The resulting binary is self‑contained.

---

## Why not Lenovo WMI / NvAPI?

These were tested extensively:

- Lenovo’s WMI GPU switching varies by BIOS and is unreliable  
- NvAPI display routing is undocumented and OEM‑dependent  

The D3D11 + driver‑hint approach is:

- stable  
- universal  
- vendor‑agnostic  
- future‑proof  
- requires no admin rights  

---

## License

MIT

## STILL WIP

# GPU‑Switcher

A tiny Windows tray utility that activates the discrete GPU (dGPU) on launch and keeps it alive, with zero CPU usage and no background threads.

Designed for hybrid GPU laptops with **NVIDIA Advanced Optimus** where you want the dGPU ready without running a heavy application, or without configuring every app manually through the driver control panel.

---

## Requirements

- Windows 10 or 11 (64‑bit)
- NVIDIA Advanced Optimus capable system (or any hybrid system with a MUX switch supporting on-the-fly display switching)
- D3D11‑capable discrete GPU
- One‑time setup in NVIDIA Control Panel

---

## One‑time setup

1. Run `GPU-Switcher.exe`
2. Open **NVIDIA Control Panel**
3. Go to **Manage 3D settings → Program Settings**
4. Add `GPU-Switcher.exe`
5. Set **Preferred graphics processor → High‑performance NVIDIA processor**
6. Enable **Automatic display switching**
7. Exit and restart `GPU-Switcher.exe`

After this, GPU‑Switcher will consistently trigger a dGPU display switch on launch.

---

## How it works

On startup, GPU‑Switcher:

1. **Exports GPU driver hints** (`NvOptimusEnablement`, `AmdPowerXpressRequestHighPerformance`) — read by the driver at process load to prefer the dGPU.
2. **Creates a persistent D3D11 device** on the adapter with the most dedicated VRAM, keeping the process registered with the GPU driver.
3. **Sits in the system tray** with zero CPU usage. Exiting releases the D3D device and the driver returns to normal routing.

---

## Tray menu

Right‑click the tray icon to access:

- **Start with Windows** — toggles a `HKCU\...\Run` registry entry
- **Reset display drivers** — sends `Ctrl+Win+Shift+B` to reset all display adapters, then exits (relaunch to re‑trigger display switching)
- **Exit** — releases the GPU and removes the tray icon

---

## Building

Requires CMake 3.20+ and Visual Studio / MSVC with Windows SDK.

```
cmake -B build
cmake --build build --config Release
```

---

## Compatibility

| System | Behaviour |
|---|---|
| NVIDIA Advanced Optimus | Full support — display switches to dGPU on launch |
| Standard Optimus (no MUX) | dGPU stays registered and visible in NVCP; no display switch |
| AMD hybrid (best‑effort) | Driver hint is sent; switching behaviour varies by OEM |
| Other | Not tested |

---

## License

MIT — do whatever you want, just link back to the original.

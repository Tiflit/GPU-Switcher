## STILL WIP

# GPU‑Switcher

A tiny Windows tray utility that activates the discrete GPU (dGPU) on launch and keeps it alive, with zero CPU usage and no background threads.

Designed for hybrid GPU laptops with **NVIDIA Advanced Optimus** where you want the dGPU ready without running a heavy application, or without configuring every app manually through the driver control panel.

---

## Requirements

- Windows 10 or 11 (64‑bit)
- NVIDIA Advanced Optimus capable system, or any hybrid system with a MUX switch supporting on-the-fly display switching
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

## Tray icon

Left‑click to see current status.

Right‑click for options:

- **Start with Windows** — toggles a `HKCU\...\Run` registry entry
- **Reset display drivers** — sends `Ctrl+Win+Shift+B` to reset all display adapters, then exits. Relaunch to re‑trigger automatic display switching.
- **Exit** — releases the GPU and removes the tray icon

---

## Compatibility

| System | Behaviour |
|--------|-----------|
| NVIDIA Advanced Optimus | Full support — display switches to dGPU on launch |
| Standard Optimus (no MUX) | dGPU stays registered and visible in NVCP; no display switch |
| AMD hybrid (best‑effort) | Driver hint is sent; switching behaviour varies by OEM |
| Other | Not tested |

---

## Building

Requires CMake 3.20+ and Visual Studio / MSVC with Windows SDK.

```
cmake -B build
cmake --build build --config Release
```

---

## Known behaviour and limitations

**Display switching requires a one‑time NVCP profile**
GPU‑Switcher does not switch the display on its own. It registers with the NVIDIA driver, which then triggers switching based on your NVCP profile. Without the profile configured, the app has no effect on display routing.

**The display switch happens at process load, not at runtime**
The driver hints (`NvOptimusEnablement`) are read once when the process starts. There is no way to trigger or cancel the switch after launch without restarting the app.

**Standard Optimus (no MUX switch) cannot switch the display**
On most non-Advanced-Optimus laptops, the iGPU is hardwired to the display panel. GPU‑Switcher will still register the dGPU with the driver and make it visible in NVCP, but no display switch will occur regardless of settings.

**The tray icon does not reflect which GPU is currently driving the display**
Reliably detecting the active display GPU via DXGI is not possible on Optimus systems — the NVIDIA adapter is always reported regardless of actual display routing. The icon is static by design.

**"Reset display drivers" closes the app**
After `Ctrl+Win+Shift+B`, the driver fully resets and the D3D device is invalidated. Re‑triggering automatic display switching requires a fresh process load, so the app exits. Relaunch manually, or enable "Start with Windows" and log out/in.

**Sleep and wake**
The app handles sleep/wake cycles by releasing and re‑acquiring the D3D device on resume. This re‑registers the process with the driver. Whether the display switch re‑triggers on resume depends on your system's driver behaviour and is not guaranteed.

**AMD hybrid systems**
The `AmdPowerXpressRequestHighPerformance` hint is exported and will be read by AMD drivers, but display switching behaviour on AMD hybrid systems has not been tested and varies significantly by OEM and driver version.

**No admin rights required**
The app runs entirely in user space. The startup registry entry is written to `HKCU`, not `HKLM`.

**Error logging**
A log file (`gpu_switcher.log`, capped at 10 KB) is only created if something goes wrong. No file is written during normal operation.

---

## Why not NvAPI or Lenovo WMI?

- NvAPI display routing is undocumented and OEM‑dependent
- Lenovo WMI GPU switching varies by BIOS and is unreliable

The D3D11 + driver hint approach requires no admin rights, works across vendors, and has no OEM dependencies.

---

## License

MIT — do whatever you want, just link back to the original.

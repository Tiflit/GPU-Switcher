<p align="center">
  <img src="https://github.com/Tiflit/GPU-Switcher/blob/main/src/icons/png/main-rainbow-128.png" height="90">
</p>

<h1 align="center">GPU‑Switcher</h1>

<p align="center"><strong>A lightweight GPU switching utility for Windows</strong></p>


&nbsp;


A tiny Windows tray utility that activates the discrete GPU (dGPU), with zero CPU usage and no background threads.

Designed for hybrid GPU laptops with **NVIDIA Advanced Optimus** where you want the dGPU ready without running a heavy application and without configuring every app manually through the driver control panel.

Please let me know if this also works for non-Nvidia display adapters.

---

## Requirements

- Windows 10 or 11 (64‑bit)
- Designed and tested for NVIDIA Advanced Optimus capable laptops, but this should work for any hybrid system with a MUX switch supporting on-the-fly dGPU display switching
- D3D11‑capable discrete GPU

---

## One‑time setup

1. Run `GPU-Switcher.exe`
2. Open **NVIDIA Control Panel**
3. Go to **Manage 3D settings → Program Settings**

4. Add `GPU-Switcher.exe`
5. Set **Preferred graphics processor → High‑performance NVIDIA processor**
6. Enable **Automatic display switching**
7. Exit and relaunch `GPU-Switcher.exe`

After this, GPU‑Switcher will consistently trigger a dGPU display switch on launch. Right-click on the tray icon to access extra options.

---

## Tray icon

Left‑click to see current status.

Right‑click for options:

- **Start with Windows** — toggle a `HKCU\...\Run` registry entry
- **Restart Display Adapters** — perform a ful reset of all display adapters and exit the program → requires UAC
- **Exit** — release the GPU and remove the tray icon

---

## How it works

On startup, GPU‑Switcher:

1. **Exports GPU driver hints** (`NvOptimusEnablement`, `AmdPowerXpressRequestHighPerformance`)
2. **Creates a persistent D3D11 device** on the adapter with the most dedicated VRAM
3. **Sits in the system tray** with zero CPU usage.

Exiting releases the D3D device and the driver returns to normal routing.

---

## Compatibility

| System | Behaviour |
|--------|-----------|
| NVIDIA Advanced Optimus | Everything should work |
| Standard Optimus (no MUX) | dGPU will activate but display switch is not available |
| AMD hybrid | Driver hint is sent; behaviour may vary by OEM |

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

**Rendering stutters after a display switch**
On some systems, switching display output between GPUs leaves the driver in a partially initialized state and causes rendering stutters. Use **Restart Display Adapters** from the tray menu to resolve this without rebooting.

**"Restart Display Adapters" closes the app**
Re‑triggering automatic display switching requires a fresh process load, so the app exits. Relaunch manually, or enable "Start with Windows" and log out/in.

**Sleep and wake**
The app handles sleep/wake cycles by releasing and re‑acquiring the D3D device on resume. This re‑registers the process with the driver. Whether the display switch re‑triggers on resume depends on your system's driver behaviour and is not guaranteed.

**AMD hybrid systems**
The `AmdPowerXpressRequestHighPerformance` hint is exported and will be read by AMD drivers, but display switching behaviour on AMD hybrid systems has not been tested and may vary by OEM and driver versions.

**UAC prompt required**
The app runs almost entirely in user space. The only exception is the Restart Display Adapters feature, which performs a full graphics driver reload. Restarting display adapters requires elevated privileges, so Windows will show a UAC prompt when this option is selected.

**Error logging**
A log file (`gpu_switcher.log`, capped at 10 KB) is only created if something goes wrong. No file is written during normal operation.

---

## License

MIT — do whatever you want, just link back to the original.

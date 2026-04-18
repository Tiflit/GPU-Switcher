# Lenovo Legion dGPU Display Switcher

A tiny Windows tray utility for Lenovo Legion / IdeaPad Gaming laptops that support GPU mode switching through Lenovo’s **LENOVO_GAMEZONE_DATA** WMI interface.

The tool automatically switches the system to **dGPU display mode** on launch, shows a **vendor‑colored tray icon**, and lets you manually switch between **iGPU** and **dGPU** display modes.  
On exit, it always returns the system to **iGPU display mode**.

---

## 🎨 Tray icon meaning

The icon shows the **active display GPU**:

| GPU | Vendor | Icon |
|-----|--------|------|
| iGPU | Intel | 🔵 **i** |
| iGPU | AMD | 🔴 **a** |
| iGPU | NVIDIA | 🟢 **n** |
| dGPU | Intel | 🔵 **I** |
| dGPU | NVIDIA | 🟢 **N** |
| dGPU | AMD | 🔴 **A** |
| Unknown | — | ⚫ **?** |

---

## 🖱️ How to use

After launching the `.exe`, the tool appears in the system tray.

Right‑click the icon to access:

- **Switch to iGPU display mode**  
- **Switch to dGPU display mode**  
- **Exit and switch to iGPU display mode (Vendor dGPU)**  

Hovering the icon shows the active GPU model.

---

## ⚠️ Requirements

- Windows 10 / 11 (64‑bit)  
- Lenovo Legion or IdeaPad Gaming laptop  
- Must be run as **Administrator**  
- System must expose the **LENOVO_GAMEZONE_DATA** WMI class  
- Any Intel / AMD / NVIDIA iGPU + dGPU combination

> This tool **does not work** on non‑Lenovo laptops.

---

## 📦 No installation needed

Just download the `.exe` and run it.  
The tool is portable and leaves no files behind.

---

## 📜 License

MIT — free to use, modify, and distribute.

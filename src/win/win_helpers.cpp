#include "win_helpers.h"

#include "../util/startup.h"
#include "../gpu/gpu_detection.h"

// Globals from main.cpp
extern GpuState g_displayGpuState;
extern bool     g_enableDgpuRendering;

// Registry path for settings
static const wchar_t* kRegPath      = L"Software\\GPU-Switcher";
static const wchar_t* kRegValueDgpu = L"EnableDgpuRendering";

bool IsDgpuRenderingEnabled()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD value = 0;
    DWORD size  = sizeof(value);
    DWORD type  = 0;

    LONG res = RegGetValueW(
        hKey,
        nullptr,
        kRegValueDgpu,
        RRF_RT_REG_DWORD,
        &type,
        &value,
        &size
    );

    RegCloseKey(hKey);

    if (res != ERROR_SUCCESS || type != REG_DWORD)
        return false;

    return value != 0;
}

void SetDgpuRenderingEnabled(bool enabled)
{
    HKEY hKey;
    if (RegCreateKeyExW(
            HKEY_CURRENT_USER,
            kRegPath,
            0,
            nullptr,
            0,
            KEY_WRITE,
            nullptr,
            &hKey,
            nullptr) != ERROR_SUCCESS)
        return;

    DWORD value = enabled ? 1u : 0u;
    RegSetValueExW(
        hKey,
        kRegValueDgpu,
        0,
        REG_DWORD,
        reinterpret_cast<const BYTE*>(&value),
        sizeof(value)
    );

    RegCloseKey(hKey);
}

// Restart GPU driver (Ctrl+Win+Shift+B)
static void RestartGpuDriver()
{
    keybd_event(VK_CONTROL, 0, 0, 0);
    keybd_event(VK_LWIN,    0, 0, 0);
    keybd_event(VK_SHIFT,   0, 0, 0);
    keybd_event('B',        0, 0, 0);

    keybd_event('B',        0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_SHIFT,   0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_LWIN,    0, KEYEVENTF_KEYUP, 0);
    keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_DISPLAYCHANGE:
    case WM_DEVICECHANGE:
    case WM_POWERBROADCAST:
        // Respect current setting after system/GPU changes
        if (g_enableDgpuRendering)
        {
            ActivateRenderGPU();
            Sleep(150);
        }
        RefreshGpuState(hwnd);
        return 0;

    case WM_TRAY:
        if (LOWORD(lParam) == WM_RBUTTONUP)
        {
            HMENU menu = CreatePopupMenu();

            AppendMenuW(menu,
                MF_STRING | (g_enableDgpuRendering ? MF_CHECKED : 0),
                ID_ENABLE_DGPU_RENDER,
                L"Enable dGPU rendering");

            AppendMenuW(menu,
                MF_STRING,
                ID_RESTART_GPU_DRIVER,
                L"Restart GPU driver");

            AppendMenuW(menu,
                MF_STRING | (IsStartupEnabled() ? MF_CHECKED : 0),
                ID_RUN_AT_STARTUP,
                L"Run at startup");

            AppendMenuW(menu,
                MF_STRING,
                ID_EXIT,
                L"Exit");

            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);

            int cmd = TrackPopupMenu(
                menu,
                TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                pt.x, pt.y,
                0,
                hwnd,
                nullptr
            );

            DestroyMenu(menu);

            switch (cmd)
            {
            case ID_ENABLE_DGPU_RENDER:
            {
                bool newVal = !g_enableDgpuRendering;
                g_enableDgpuRendering = newVal;
                SetDgpuRenderingEnabled(newVal);

                if (g_enableDgpuRendering)
                {
                    ActivateRenderGPU();
                    Sleep(150);
                }

                RefreshGpuState(hwnd);
                break;
            }

            case ID_RESTART_GPU_DRIVER:
            {
                RestartGpuDriver();
                Sleep(500);

                if (g_enableDgpuRendering)
                {
                    ActivateRenderGPU();
                    Sleep(150);
                }

                RefreshGpuState(hwnd);
                break;
            }

            case ID_RUN_AT_STARTUP:
                SetStartup(!IsStartupEnabled());
                break;

            case ID_EXIT:
                PostQuitMessage(0);
                break;
            }
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

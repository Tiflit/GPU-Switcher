#include "win_helpers.h"

#include "../util/startup.h"
#include "../util/logging.h"

// Globals from main.cpp
extern GpuState g_displayGpuState;
extern GpuState g_renderGpuState;
extern bool     g_forceRenderGpu;

// From main.cpp
extern void RefreshGpuState(HWND hwnd);

// Restart GPU driver (Ctrl+Win+Shift+B)
void RestartGpuDriver()
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
        // Event-driven refresh: no polling, no timers
        RefreshGpuState(hwnd);
        return 0;

    case WM_TRAY:
        if (LOWORD(lParam) == WM_RBUTTONUP)
        {
            HMENU menu = CreatePopupMenu();

            AppendMenuW(menu,
                MF_STRING | (g_forceRenderGpu ? MF_CHECKED : 0),
                ID_TOGGLE_RENDER_GPU,
                L"Force dGPU rendering");

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
            case ID_TOGGLE_RENDER_GPU:
                g_forceRenderGpu = !g_forceRenderGpu;
                RefreshGpuState(hwnd);
                break;

            case ID_RESTART_GPU_DRIVER:
                RestartGpuDriver();
                break;

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

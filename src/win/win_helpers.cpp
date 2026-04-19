#include "win_helpers.h"

#include "../util/startup.h"
#include "../tray/tray_menu.h"
#include "../gpu/gpu_state.h"

// This will be defined in main.cpp
extern GpuState g_displayGpuState;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAY:
        if (LOWORD(lParam) == WM_RBUTTONUP)
        {
            bool startup   = IsStartupEnabled();
            bool gpuError  = (g_displayGpuState.vendor == 0);

            int cmd = ShowTrayMenu(hwnd, startup, gpuError);

            if (cmd == ID_RUN_AT_STARTUP)
                SetStartup(!startup);

            if (cmd == ID_EXIT)
                PostQuitMessage(0);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

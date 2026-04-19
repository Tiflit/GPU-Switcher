#include <windows.h>
#include <shellapi.h>

#include "gpu/gpu_detection.h"
#include "gpu/gpu_state.h"
#include "tray/tray_icon.h"
#include "util/startup.h"
#include "win/win_helpers.h"
#include "resource.h"

// Global GPU state
GpuState g_displayGpuState;
bool     g_enableDgpuRendering = false;

HINSTANCE g_hInst = nullptr;

void RefreshGpuState(HWND hwnd)
{
    DetectDisplayGPU(g_displayGpuState);

    UINT activeVendor = g_displayGpuState.vendor;

    HICON icon = LoadDisplayIcon(activeVendor, g_hInst);

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd;
    nid.uID    = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.hIcon  = icon;

    std::wstring tip = BuildGpuTooltip(g_displayGpuState);
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hInst = hInst;

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"GPUSwitcherMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hMutex);
        return 0;
    }

    // Load persistent setting
    g_enableDgpuRendering = IsDgpuRenderingEnabled();

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"TrayHookClass";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"GPU-Switcher",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInst, nullptr
    );

    // Initial behavior based on setting
    if (g_enableDgpuRendering)
    {
        ActivateRenderGPU();
        Sleep(150);
    }

    DetectDisplayGPU(g_displayGpuState);

    UINT activeVendor = g_displayGpuState.vendor;

    HICON icon = LoadDisplayIcon(activeVendor, g_hInst);
    std::wstring tip = BuildGpuTooltip(g_displayGpuState);

    NOTIFYICONDATAW nid = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd;
    nid.uID              = TRAY_ID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon            = icon;
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);

    Shell_NotifyIconW(NIM_ADD, &nid);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
    CloseHandle(hMutex);
    return 0;
}

#include <windows.h>
#include <shellapi.h>

#include "gpu/gpu_detection.h"
#include "gpu/gpu_state.h"
#include "tray/tray_icon.h"
#include "util/startup.h"
#include "util/logging.h"
#include "win/win_helpers.h"
#include "resource.h"

// GPU vendor hints (NVIDIA Optimus + AMD Dynamic Switchable Graphics)
extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 1;
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
}

// Global GPU state (used by win_helpers.cpp)
GpuState g_displayGpuState;

HINSTANCE g_hInst = nullptr;

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hInst = hInst;

    // Prevent multiple instances
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"GPUSwitcherMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hMutex);
        return 0;
    }

    // Register hidden window class
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

    // Detect GPU
    DetectDisplayGPU(g_displayGpuState);

    // Load icon
    HICON icon = LoadDisplayIcon(g_displayGpuState.vendor, g_hInst);

    // Add tray icon
    NOTIFYICONDATAW nid = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd;
    nid.uID              = TRAY_ID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon            = icon;

    swprintf_s(nid.szTip, L"Display GPU: %s", g_displayGpuState.name.c_str());

    Shell_NotifyIconW(NIM_ADD, &nid);

    // Message loop
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

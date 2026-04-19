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

// Global GPU states
GpuState g_displayGpuState;
GpuState g_renderGpuState;

bool g_forceRenderGpu = true;   // User toggle
HINSTANCE g_hInst = nullptr;

void RefreshGpuState(HWND hwnd)
{
    DetectDisplayGPU(g_displayGpuState);

    if (g_forceRenderGpu)
        DetectRenderGPU(g_renderGpuState);
    else
        g_renderGpuState = {}; // Clear render GPU

    // Choose active GPU
    UINT activeVendor = (g_renderGpuState.vendor != 0)
                        ? g_renderGpuState.vendor
                        : g_displayGpuState.vendor;

    HICON icon = LoadDisplayIcon(activeVendor, g_hInst);

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd;
    nid.uID    = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.hIcon  = icon;

    std::wstring tip = BuildGpuTooltip(g_displayGpuState, g_renderGpuState);
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

    // Register hidden window
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

    // Add tray icon (temporary)
    NOTIFYICONDATAW nid = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd;
    nid.uID              = TRAY_ID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon            = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Initializing GPU detection...");
    Shell_NotifyIconW(NIM_ADD, &nid);

    // Delay detection by 1 second
    SetTimer(hwnd, 1, 1000, nullptr);

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

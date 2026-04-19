#include <windows.h>
#include <shellapi.h>
#include <dxgi.h>
#include <d3d11.h>

#include "resource.h"
#include "util/logging.h"
#include "util/startup.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

// Driver hints — read at process load time by NVIDIA/AMD drivers.
// These tell the driver to prefer the dGPU for this process,
// which triggers 'Automatic display switching' in NVCP if configured.
extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement                  = 1;
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
}

#define WM_TRAY             (WM_USER + 1)
#define TRAY_ID             1
#define ID_RUN_AT_STARTUP   1001
#define ID_RESTART_GPU      1002
#define ID_EXIT             1003

static HINSTANCE             g_hInst   = nullptr;
static ID3D11Device*         g_device  = nullptr;   // held alive to keep dGPU registered
static ID3D11DeviceContext*  g_context = nullptr;

// ───────────────────────────────────────────────────────────────
// Restart GPU driver (Ctrl + Win + Shift + B)
// ───────────────────────────────────────────────────────────────
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

// ───────────────────────────────────────────────────────────────
// Acquire dGPU by creating a persistent D3D11 device
// ───────────────────────────────────────────────────────────────
static bool AcquireDGpu()
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
    {
        LogError(L"CreateDXGIFactory1 failed");
        return false;
    }

    IDXGIAdapter1* best     = nullptr;
    SIZE_T         bestVram = 0;
    IDXGIAdapter1* adapter  = nullptr;

    for (UINT i = 0;
         factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
            !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            desc.DedicatedVideoMemory > bestVram)
        {
            bestVram = desc.DedicatedVideoMemory;
            if (best) best->Release();
            best = adapter;
            continue;
        }
        adapter->Release();
    }
    factory->Release();

    if (!best)
    {
        LogError(L"No discrete GPU adapter found");
        return false;
    }

    HRESULT hr = D3D11CreateDevice(
        best,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &g_device,
        nullptr,
        &g_context
    );
    best->Release();

    if (FAILED(hr))
    {
        LogError(L"D3D11CreateDevice failed");
        return false;
    }

    return true;
}

static void ReleaseDGpu()
{
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device)  { g_device->Release();  g_device  = nullptr; }
}

// ───────────────────────────────────────────────────────────────
// Window procedure
// ───────────────────────────────────────────────────────────────
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAY:
        if (LOWORD(lParam) == WM_RBUTTONUP)
        {
            HMENU menu = CreatePopupMenu();

            bool startup = IsStartupEnabled();

            AppendMenuW(menu,
                MF_STRING | (startup ? MF_CHECKED : 0),
                ID_RUN_AT_STARTUP,
                L"Run at startup");

            AppendMenuW(menu,
                MF_STRING,
                ID_RESTART_GPU,
                L"Restart GPUs");

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
            case ID_RUN_AT_STARTUP:
                SetStartup(!startup);
                break;

            case ID_RESTART_GPU:
                RestartGpuDriver();
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

// ───────────────────────────────────────────────────────────────
// Entry point
// ───────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hInst = hInst;

    // Single-instance guard
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"GPUSwitcherMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hMutex);
        return 0;
    }

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
        nullptr,
        nullptr,
        hInst,
        nullptr
    );

    AcquireDGpu(); // logs errors but tray still appears

    HICON icon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON));

    NOTIFYICONDATAW nid = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd;
    nid.uID              = TRAY_ID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon            = icon;
    wcsncpy_s(nid.szTip, L"GPU-Switcher", _TRUNCATE);

    Shell_NotifyIconW(NIM_ADD, &nid);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
    ReleaseDGpu();
    CloseHandle(hMutex);
    return 0;
}

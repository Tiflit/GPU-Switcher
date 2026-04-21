#include <windows.h>
#include <shellapi.h>
#include <dxgi.h>
#include <d3d11.h>

#include "resource.h"
#include "util/logging.h"
#include "util/startup.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

#define APP_VERSION L"1.0.0"

extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement                  = 1;
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
}

#define WM_TRAY           (WM_USER + 1)
#define TRAY_ID           1
#define ID_START_WINDOWS  1001
#define ID_RESET_DISPLAYS 1002
#define ID_EXIT           1003

static HINSTANCE            g_hInst   = nullptr;
static ID3D11Device*        g_device  = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static NOTIFYICONDATAW      g_nid     = {};
static UINT                 g_taskbarCreatedMsg = 0;

static void ReleaseDGpu()
{
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device)  { g_device->Release();  g_device  = nullptr; }
}

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
    wchar_t        bestName[128] = {};

    for (UINT i = 0;
         factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND;
         ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc))        &&
            !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            desc.DedicatedVideoMemory > bestVram)
        {
            bestVram = desc.DedicatedVideoMemory;
            wcsncpy_s(bestName, desc.Description, _TRUNCATE);
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

    D3D_FEATURE_LEVEL level = {};
    HRESULT hr = D3D11CreateDevice(
        best, D3D_DRIVER_TYPE_UNKNOWN,
        nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        &g_device, &level, &g_context);
    best->Release();

    if (FAILED(hr))
    {
        wchar_t buf[64];
        swprintf_s(buf, L"D3D11CreateDevice failed: 0x%08X", hr);
        LogError(buf);
        return false;
    }

    if (level < D3D_FEATURE_LEVEL_11_0)
        LogError(L"Warning: acquired adapter does not support D3D11 FL 11.0");

    LogInfo(std::wstring(L"Acquired adapter: ") + bestName);
    return true;
}

static void ResetDisplayDrivers()
{
    INPUT inputs[8] = {};

    inputs[0].type = INPUT_KEYBOARD; inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD; inputs[1].ki.wVk = VK_LWIN;
    inputs[2].type = INPUT_KEYBOARD; inputs[2].ki.wVk = VK_SHIFT;
    inputs[3].type = INPUT_KEYBOARD; inputs[3].ki.wVk = 'B';

    inputs[4].type = INPUT_KEYBOARD; inputs[4].ki.wVk = 'B';        inputs[4].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[5].type = INPUT_KEYBOARD; inputs[5].ki.wVk = VK_SHIFT;   inputs[5].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[6].type = INPUT_KEYBOARD; inputs[6].ki.wVk = VK_LWIN;    inputs[6].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[7].type = INPUT_KEYBOARD; inputs[7].ki.wVk = VK_CONTROL; inputs[7].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(8, inputs, sizeof(INPUT));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == g_taskbarCreatedMsg)
    {
        Shell_NotifyIconW(NIM_ADD, &g_nid);
        g_nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
        return 0;
    }

    switch (msg)
    {
    case WM_TRAY:
    {
        UINT event = LOWORD(lParam);

        if (event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK)
        {
            NOTIFYICONDATAW balloon = g_nid;
            balloon.uFlags      |= NIF_INFO;
            balloon.dwInfoFlags  = NIIF_INFO | NIIF_NOSOUND;
            wcsncpy_s(balloon.szInfoTitle, L"GPU-Switcher", _TRUNCATE);
            wcsncpy_s(balloon.szInfo,
                g_device ? L"dGPU active" : L"dGPU acquisition failed — check log",
                _TRUNCATE);
            Shell_NotifyIconW(NIM_MODIFY, &balloon);
            return 0;
        }

        if (event == WM_RBUTTONUP)
        {
            HMENU menu = CreatePopupMenu();
            bool startup = IsStartupEnabled();

            AppendMenuW(menu, MF_STRING | (startup ? MF_CHECKED : 0), ID_START_WINDOWS,  L"Start with Windows");
            AppendMenuW(menu, MF_STRING,                               ID_RESET_DISPLAYS, L"Reset display drivers");
            AppendMenuW(menu, MF_STRING,                               ID_EXIT,           L"Exit");

            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);

            int cmd = TrackPopupMenu(menu,
                TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);

            switch (cmd)
            {
            case ID_START_WINDOWS:
                SetStartup(!startup);
                break;

            case ID_RESET_DISPLAYS:
                LogInfo(L"Reset display drivers requested — exiting");
                ReleaseDGpu();
                Shell_NotifyIconW(NIM_DELETE, &g_nid);
                ResetDisplayDrivers();
                Sleep(100);
                PostQuitMessage(0);
                break;

            case ID_EXIT:
                PostQuitMessage(0);
                break;
            }
        }
        return 0;
    }

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMRESUMEAUTOMATIC)
        {
            LogInfo(L"System resumed — re-acquiring dGPU");
            ReleaseDGpu();
            Sleep(1000);
            AcquireDGpu();
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hInst = hInst;

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"GPUSwitcherMutex");
    if (!hMutex)
    {
        LogError(L"Failed to create mutex");
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        LogInfo(L"Another instance is already running");
        CloseHandle(hMutex);
        return 0;
    }

    LogInfo(L"GPU-Switcher v" APP_VERSION L" started");

    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc     = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"TrayHookClass";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW, wc.lpszClassName, L"GPU-Switcher",
        WS_POPUP, 0, 0, 0, 0,
        nullptr, nullptr, hInst, nullptr);

    Sleep(200);

    if (!AcquireDGpu())
        LogError(L"Failed to acquire dGPU on startup");

    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = TRAY_ID;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon            = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON));
    if (!g_nid.hIcon)
        g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(g_nid.szTip, L"GPU-Switcher", _TRUNCATE);

    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    ReleaseDGpu();
    LogInfo(L"GPU-Switcher exited cleanly");
    CloseHandle(hMutex);
    return 0;
}

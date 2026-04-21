#include <windows.h>
#include <shellapi.h>
#include <dxgi.h>
#include <d3d11.h>

#include "resource.h"
#include "util/logging.h"
#include "util/startup.h"
#include "util/reset.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

#define APP_VERSION L"1.1.0"

extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement                  = 1;
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
}

#define WM_TRAY           (WM_USER + 1)
#define WM_RESET_DONE     (WM_USER + 2)
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

// Updates only the tooltip text, leaves all other g_nid fields intact
static void SetTrayTip(const wchar_t* tip)
{
    wcsncpy_s(g_nid.szTip, tip, _TRUNCATE);
    g_nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP; // restore
}

// Thread proc: waits for the elevated child to exit, then posts WM_RESET_DONE
struct WaitCtx { HANDLE hProcess; HWND hwnd; };

static DWORD WINAPI WaitForResetThread(LPVOID param)
{
    auto* ctx = reinterpret_cast<WaitCtx*>(param);
    WaitForSingleObject(ctx->hProcess, INFINITE);
    CloseHandle(ctx->hProcess);
    PostMessageW(ctx->hwnd, WM_RESET_DONE, 0, 0);
    delete ctx;
    return 0;
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
            AppendMenuW(menu, MF_STRING,                               ID_RESET_DISPLAYS, L"Full GPU reset");
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
            {
                SetTrayTip(L"Full GPU reset requested…");
                LogInfo(L"Full GPU reset requested — launching elevated child");

                wchar_t exePath[MAX_PATH] = {};
                GetModuleFileNameW(nullptr, exePath, MAX_PATH);

                SHELLEXECUTEINFOW sei  = {};
                sei.cbSize       = sizeof(sei);
                sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
                sei.lpVerb       = L"runas";
                sei.lpFile       = exePath;
                sei.lpParameters = L"--reset-gpu";
                sei.nShow        = SW_HIDE;

                if (!ShellExecuteExW(&sei) || !sei.hProcess)
                {
                    // UAC was cancelled or launch failed — restore tooltip
                    LogInfo(L"Elevated launch cancelled or failed");
                    SetTrayTip(L"GPU-Switcher");
                    break;
                }

                // UAC accepted — release GPU, update tooltip, start wait thread
                LogInfo(L"Elevated child launched — releasing dGPU and waiting");
                ReleaseDGpu();
                SetTrayTip(L"Resetting GPU… screen may flicker");

                auto* ctx    = new WaitCtx{ sei.hProcess, hwnd };
                HANDLE hThread = CreateThread(nullptr, 0, WaitForResetThread,
                                              ctx, 0, nullptr);
                if (hThread)
                    CloseHandle(hThread); // we don't need to track it
                else
                {
                    // Thread creation failed — clean up and exit anyway
                    LogError(L"CreateThread failed — exiting immediately");
                    CloseHandle(sei.hProcess);
                    delete ctx;
                    Shell_NotifyIconW(NIM_DELETE, &g_nid);
                    PostQuitMessage(0);
                }
                break;
            }

            case ID_EXIT:
                PostQuitMessage(0);
                break;
            }
        }
        return 0;
    }

    case WM_RESET_DONE:
        LogInfo(L"Elevated child finished — exiting");
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;

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

// Elevated path: called when launched with --reset-gpu
static int RunElevatedReset()
{
    LogInfo(L"GPU-Switcher elevated reset started");
    CycleAllDisplayAdapters();
    LogInfo(L"GPU-Switcher elevated reset complete");
    return 0;
}

// lpCmdLine is unused because we parse the command line via GetCommandLineW().
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    // Check for elevated reset mode first
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool resetMode = false;
    if (argv)
    {
        for (int i = 1; i < argc; ++i)
            if (_wcsicmp(argv[i], L"--reset-gpu") == 0)
                resetMode = true;
        LocalFree(argv);
    }

    if (resetMode)
        return RunElevatedReset();

    // Normal tray mode
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

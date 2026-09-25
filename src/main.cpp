#include <windows.h>
#include <shellapi.h>
#include <dxgi.h>
#include <d3d11.h>

#include <wrl/client.h>

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

using Microsoft::WRL::ComPtr;

static HINSTANCE                            g_hInst   = nullptr;
static ComPtr<ID3D11Device>                 g_device;
static ComPtr<ID3D11DeviceContext>          g_context;
static NOTIFYICONDATAW                      g_nid     = {};
static UINT                                 g_taskbarCreatedMsg = 0;

static void ReleaseDGpu()
{
    if (g_context)
    {
        g_context->ClearState();
        g_context->Flush();
        g_context.Reset();
    }
    g_device.Reset();
}

static bool AcquireDGpu()
{
    ReleaseDGpu();

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
    {
        LogError(L"CreateDXGIFactory1 failed");
        return false;
    }

    ComPtr<IDXGIAdapter1> bestAdapter;
    DXGI_ADAPTER_DESC1    bestDesc = {};
    int                   bestScore = -1;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)))
        {
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            {
                adapter.Reset();
                continue;
            }

            int score = 0;
            // Prioritize dedicated discrete vendors targeting Optimus / PowerXpress
            if (desc.VendorId == 0x10DE)       // NVIDIA
                score = 100;
            else if (desc.VendorId == 0x1002)  // AMD
                score = 50;
            else if (desc.VendorId == 0x8086)  // Intel
                score = 10;

            // Add score proportional to dedicated VRAM (64 MB increments)
            score += static_cast<int>(desc.DedicatedVideoMemory / (1024 * 1024 * 64));

            wchar_t logBuf[256];
            swprintf_s(logBuf, L"Detected GPU [%u]: %s (Vendor: 0x%04X, Dedicated VRAM: %llu MB)",
                i, desc.Description, desc.VendorId,
                static_cast<unsigned long long>(desc.DedicatedVideoMemory / (1024 * 1024)));
            LogInfo(logBuf);

            if (score > bestScore)
            {
                bestScore   = score;
                bestDesc    = desc;
                bestAdapter = adapter;
            }
        }
        adapter.Reset();
    }

    if (!bestAdapter)
    {
        LogError(L"No suitable graphics adapter found");
        return false;
    }

    LogInfo(std::wstring(L"Selected target adapter: ") + bestDesc.Description);

    D3D_FEATURE_LEVEL level = {};
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    HRESULT hr = D3D11CreateDevice(
        bestAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN,
        nullptr, 0,
        featureLevels, ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &g_device, &level, &g_context);

    if (FAILED(hr))
    {
        wchar_t buf[64];
        swprintf_s(buf, L"D3D11CreateDevice failed: 0x%08X", hr);
        LogError(buf);
        return false;
    }

    if (level < D3D_FEATURE_LEVEL_11_0)
        LogError(L"Warning: acquired adapter feature level is below D3D_FEATURE_LEVEL_11_0");

    LogInfo(std::wstring(L"Successfully initialized D3D11 device on: ") + bestDesc.Description);
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

static bool                 g_resetInProgress   = false;

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

        // Left-click / select: show status balloon
        if (event == WM_LBUTTONUP || event == WM_LBUTTONDBLCLK ||
            event == NIN_SELECT || event == NIN_KEYSELECT)
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

        // Right-click / context menu key: show context menu
        if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU)
        {
            HMENU menu = CreatePopupMenu();
            bool startup = IsStartupEnabled();

            AppendMenuW(menu, MF_STRING | (startup ? MF_CHECKED : 0),
                        ID_START_WINDOWS, L"Start with Windows");

            AppendMenuW(menu, MF_STRING | (g_resetInProgress ? MF_GRAYED : 0),
                        ID_RESET_DISPLAYS, L"Restart Display Adapters");

            AppendMenuW(menu, MF_STRING,
                        ID_EXIT, L"Exit");

            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);

            int cmd = TrackPopupMenu(menu,
                TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                pt.x, pt.y, 0, hwnd, nullptr);
            PostMessageW(hwnd, WM_NULL, 0, 0);
            DestroyMenu(menu);

            switch (cmd)
            {
            case ID_START_WINDOWS:
                SetStartup(!startup);
                break;

            case ID_RESET_DISPLAYS:
            {
                if (g_resetInProgress)
                    break;

                g_resetInProgress = true;
                SetTrayTip(L"Restarting display adapters…");
                LogInfo(L"Restart Display Adapters requested");

                // Release dGPU BEFORE spawning elevated child to prevent driver conflicts
                ReleaseDGpu();

                wchar_t exePath[MAX_PATH] = {};
                GetModuleFileNameW(nullptr, exePath, MAX_PATH);

                SHELLEXECUTEINFOW sei = {};
                sei.cbSize       = sizeof(sei);
                sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
                sei.lpVerb       = L"runas";
                sei.lpFile       = exePath;
                sei.lpParameters = L"--reset-gpu";
                sei.nShow        = SW_HIDE;

                if (!ShellExecuteExW(&sei) || !sei.hProcess)
                {
                    LogInfo(L"Elevated launch cancelled or failed — reacquiring dGPU");
                    g_resetInProgress = false;
                    SetTrayTip(L"GPU-Switcher");
                    AcquireDGpu();
                    break;
                }

                LogInfo(L"Elevated child launched — waiting for reset completion");
                SetTrayTip(L"Restarting display adapters… screen may flicker");

                auto* ctx = new WaitCtx{ sei.hProcess, hwnd };
                HANDLE hThread = CreateThread(nullptr, 0, WaitForResetThread,
                                              ctx, 0, nullptr);
                if (hThread)
                {
                    CloseHandle(hThread);
                }
                else
                {
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

// lpCmdLine is unused because we parse via GetCommandLineW().
// Removing the parameter name silences MSVC warning C4100.
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

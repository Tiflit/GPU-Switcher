#include <windows.h>
#include <shellapi.h>
#include <dxgi.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <vector>

#include "resource.h"
#include "util/logging.h"
#include "util/startup.h"
#include "util/reset.h"

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")

#define APP_VERSION L"1.2.0"

extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement                  = 1;
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
}

#define WM_TRAY                 (WM_USER + 1)
#define WM_RESET_DONE           (WM_USER + 2)
#define WM_ACTIVATE_INSTANCE    (WM_USER + 3)
#define TRAY_ID                 1
#define ID_START_WINDOWS        1001
#define ID_RESET_DISPLAYS       1002
#define ID_EXIT                 1003

#define TIMER_RESUME_REACQUIRE  2001
#define TIMER_HEALTH_CHECK      2002

using Microsoft::WRL::ComPtr;

enum class AppState
{
    NoDiscreteGpu,          // No discrete GPU candidate detected
    Acquiring,              // Creating D3D11 device
    Active,                 // Holding D3D11 device on discrete GPU
    DeviceLost,             // D3D11 device was removed or reset
    WaitingForDiscreteGpu,  // Preferred or discrete GPU is absent (e.g. eGPU disconnected)
    Resetting               // Elevated display adapter reset in progress
};

static const wchar_t* AppStateToString(AppState state)
{
    switch (state)
    {
    case AppState::NoDiscreteGpu:         return L"NoDiscreteGpu";
    case AppState::Acquiring:             return L"Acquiring";
    case AppState::Active:                return L"Active";
    case AppState::DeviceLost:            return L"DeviceLost";
    case AppState::WaitingForDiscreteGpu: return L"WaitingForDiscreteGpu";
    case AppState::Resetting:             return L"Resetting";
    default:                              return L"Unknown";
    }
}

struct GpuCandidate
{
    ComPtr<IDXGIAdapter1> adapter;
    DXGI_ADAPTER_DESC1    desc = {};
    LUID                  luid = {};
    bool                  isDiscrete = false;
    int                   score = 0;
};

static HINSTANCE                            g_hInst             = nullptr;
static HANDLE                               g_hMutex            = nullptr;
static ComPtr<ID3D11Device>                 g_device;
static ComPtr<ID3D11DeviceContext>          g_context;
static NOTIFYICONDATAW                      g_nid               = {};
static UINT                                 g_taskbarCreatedMsg = 0;

static AppState                             g_appState          = AppState::NoDiscreteGpu;
static UINT                                 g_activeVendorId    = 0;
static std::wstring                         g_activeGpuName;
static LUID                                 g_activeLuid        = { 0, 0 };
static LUID                                 g_preferredLuid     = { 0, 0 };
static std::wstring                         g_preferredGpuName;

static bool HasPreferredLuid()
{
    return (g_preferredLuid.LowPart != 0 || g_preferredLuid.HighPart != 0);
}

static void SetAppState(AppState newState)
{
    if (g_appState != newState)
    {
        wchar_t buf[128];
        swprintf_s(buf, L"State transition: %s -> %s", AppStateToString(g_appState), AppStateToString(newState));
        LogInfo(buf);
        g_appState = newState;
    }
}

static bool IsAdapterPresent(const LUID& targetLuid)
{
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.GetAddressOf()))))
        return false;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)))
        {
            if (desc.AdapterLuid.LowPart == targetLuid.LowPart &&
                desc.AdapterLuid.HighPart == targetLuid.HighPart)
            {
                return true;
            }
        }
        adapter.Reset();
    }
    return false;
}

static bool EvaluateAdapter(IDXGIAdapter1* pAdapter, GpuCandidate& info)
{
    if (FAILED(pAdapter->GetDesc1(&info.desc)))
        return false;

    if (info.desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        return false;

    info.adapter = pAdapter;
    info.luid = info.desc.AdapterLuid;

    // Distinguish discrete vs integrated:
    // - 0x10DE (NVIDIA): Dedicated discrete GPU (Optimus dGPU or eGPU)
    // - 0x1002 (AMD): Discrete if dedicated VRAM > 512 MB (filters out integrated APU carveouts)
    // - 0x8086 (Intel): Discrete (Arc) if dedicated VRAM > 1024 MB (integrated Iris Xe/UHD typically 0-128 MB)
    // - Other: Discrete if dedicated VRAM exceeds shared system memory
    if (info.desc.VendorId == 0x10DE)
    {
        info.isDiscrete = true;
    }
    else if (info.desc.VendorId == 0x1002)
    {
        info.isDiscrete = (info.desc.DedicatedVideoMemory > 512 * 1024 * 1024);
    }
    else if (info.desc.VendorId == 0x8086)
    {
        info.isDiscrete = (info.desc.DedicatedVideoMemory > 1024 * 1024 * 1024);
    }
    else
    {
        info.isDiscrete = (info.desc.DedicatedVideoMemory > info.desc.SharedSystemMemory);
    }

    // Scoring:
    // Base tier for discrete: NVIDIA: 300, AMD: 200, Intel: 100, Other: 50
    // Base tier for integrated: AMD: 20, Intel: 10, Other: 1
    if (info.isDiscrete)
    {
        if (info.desc.VendorId == 0x10DE)
            info.score = 300;
        else if (info.desc.VendorId == 0x1002)
            info.score = 200;
        else if (info.desc.VendorId == 0x8086)
            info.score = 100;
        else
            info.score = 50;
    }
    else
    {
        if (info.desc.VendorId == 0x1002)
            info.score = 20;
        else if (info.desc.VendorId == 0x8086)
            info.score = 10;
        else
            info.score = 1;
    }

    // Add VRAM in 64MB increments to score
    info.score += static_cast<int>(info.desc.DedicatedVideoMemory / (1024 * 1024 * 64));
    return true;
}

static void ReleaseDGpu()
{
    if (g_context)
    {
        g_context->ClearState();
        g_context->Flush();
        g_context.Reset();
    }
    g_device.Reset();
    g_activeVendorId = 0;
    g_activeGpuName.clear();
    g_activeLuid = { 0, 0 };
    if (g_appState == AppState::Active)
    {
        SetAppState(AppState::WaitingForDiscreteGpu);
    }
}

static bool AcquireDGpu()
{
    ReleaseDGpu();

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.GetAddressOf()))))
    {
        LogError(L"CreateDXGIFactory1 failed");
        return false;
    }

    std::vector<GpuCandidate> discreteCandidates;

    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        GpuCandidate info;
        if (EvaluateAdapter(adapter.Get(), info))
        {
            wchar_t logBuf[256];
            swprintf_s(logBuf, L"Detected GPU [%u]: %s (Vendor: 0x%04X, Dedicated VRAM: %llu MB, Discrete: %s, Score: %d)",
                i, info.desc.Description, info.desc.VendorId,
                static_cast<unsigned long long>(info.desc.DedicatedVideoMemory / (1024 * 1024)),
                info.isDiscrete ? L"yes" : L"no", info.score);
            LogInfo(logBuf);

            if (info.isDiscrete)
            {
                discreteCandidates.push_back(info);
            }
        }
        adapter.Reset();
    }

    if (discreteCandidates.empty())
    {
        if (HasPreferredLuid())
        {
            SetAppState(AppState::WaitingForDiscreteGpu);
            LogInfo(L"Preferred discrete GPU is not present — waiting for reconnection");
        }
        else
        {
            SetAppState(AppState::NoDiscreteGpu);
            LogInfo(L"No discrete GPU detected on the system");
        }
        return false;
    }

    // Select target adapter:
    // If a preferred LUID was previously tracked, look for an exact match among discrete candidates
    GpuCandidate best;
    bool foundPreferred = false;
    if (HasPreferredLuid())
    {
        for (const auto& cand : discreteCandidates)
        {
            if (cand.luid.LowPart == g_preferredLuid.LowPart &&
                cand.luid.HighPart == g_preferredLuid.HighPart)
            {
                best = cand;
                foundPreferred = true;
                LogInfo(std::wstring(L"Matched preferred discrete GPU: ") + best.desc.Description);
                break;
            }
        }
    }

    if (!foundPreferred)
    {
        // Pick the highest scoring discrete candidate (either initial selection, or temporary fallback)
        int bestScore = -1;
        for (const auto& cand : discreteCandidates)
        {
            if (cand.score > bestScore)
            {
                bestScore = cand.score;
                best = cand;
            }
        }

        if (HasPreferredLuid())
        {
            LogInfo(std::wstring(L"Preferred GPU not found — acquiring fallback discrete GPU: ") + best.desc.Description);
        }
        else
        {
            LogInfo(std::wstring(L"Selected initial preferred discrete GPU: ") + best.desc.Description);
        }
    }

    SetAppState(AppState::Acquiring);

    D3D_FEATURE_LEVEL level = {};
    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };

    HRESULT hr = D3D11CreateDevice(
        best.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN,
        nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        featureLevels, static_cast<UINT>(ARRAYSIZE(featureLevels)),
        D3D11_SDK_VERSION,
        g_device.GetAddressOf(), &level, g_context.GetAddressOf());

    if (FAILED(hr))
    {
        wchar_t buf[64];
        swprintf_s(buf, L"D3D11CreateDevice failed: 0x%08X", hr);
        LogError(buf);
        SetAppState(AppState::DeviceLost);
        return false;
    }

    if (level < D3D_FEATURE_LEVEL_11_0)
        LogError(L"Warning: acquired adapter feature level is below D3D_FEATURE_LEVEL_11_0");

    g_activeVendorId = best.desc.VendorId;
    g_activeGpuName  = best.desc.Description;
    g_activeLuid     = best.luid;

    // Establish preferred LUID only on initial selection to prevent preference drift
    if (!HasPreferredLuid())
    {
        g_preferredLuid    = best.luid;
        g_preferredGpuName = best.desc.Description;
        LogInfo(std::wstring(L"Established preferred discrete GPU: ") + best.desc.Description);
    }

    SetAppState(AppState::Active);

    LogInfo(std::wstring(L"Successfully initialized D3D11 device on: ") + best.desc.Description);
    return true;
}

static HICON GetVendorIcon(bool isActive, UINT vendorId)
{
    UINT resId = IDI_ICON_WARNING;
    if (isActive)
    {
        switch (vendorId)
        {
        case 0x10DE: resId = IDI_ICON_NVIDIA;  break;
        case 0x1002: resId = IDI_ICON_AMD;     break;
        case 0x8086: resId = IDI_ICON_INTEL;   break;
        default:     resId = IDI_ICON_UNKNOWN; break;
        }
    }

    HICON hIcon = static_cast<HICON>(LoadImageW(
        g_hInst,
        MAKEINTRESOURCEW(resId),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_SHARED));

    if (!hIcon)
    {
        hIcon = LoadIconW(g_hInst, MAKEINTRESOURCEW(resId));
    }
    if (!hIcon)
    {
        hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    }
    return hIcon;
}

static void UpdateTrayStatus(const wchar_t* customTip = nullptr)
{
    if (!g_nid.hWnd)
        return;

    bool isTrayActive = (g_appState == AppState::Active);
    g_nid.hIcon = GetVendorIcon(isTrayActive, g_activeVendorId);

    if (customTip)
    {
        wcsncpy_s(g_nid.szTip, customTip, _TRUNCATE);
    }
    else
    {
        switch (g_appState)
        {
        case AppState::Active:
            if (HasPreferredLuid() && (g_activeLuid.LowPart != g_preferredLuid.LowPart || g_activeLuid.HighPart != g_preferredLuid.HighPart))
            {
                swprintf_s(g_nid.szTip, L"GPU-Switcher: %s (Fallback)", g_activeGpuName.c_str());
            }
            else
            {
                swprintf_s(g_nid.szTip, L"GPU-Switcher: %s (Active)", g_activeGpuName.c_str());
            }
            break;
        case AppState::Resetting:
            wcsncpy_s(g_nid.szTip, L"GPU-Switcher: Restarting display adapters…", _TRUNCATE);
            break;
        case AppState::WaitingForDiscreteGpu:
            if (!g_preferredGpuName.empty())
                swprintf_s(g_nid.szTip, L"GPU-Switcher: Waiting for %s", g_preferredGpuName.c_str());
            else
                wcsncpy_s(g_nid.szTip, L"GPU-Switcher: Waiting for discrete GPU", _TRUNCATE);
            break;
        case AppState::NoDiscreteGpu:
            wcsncpy_s(g_nid.szTip, L"GPU-Switcher: No discrete GPU detected", _TRUNCATE);
            break;
        case AppState::DeviceLost:
            wcsncpy_s(g_nid.szTip, L"GPU-Switcher: GPU device lost — recovering", _TRUNCATE);
            break;
        case AppState::Acquiring:
            wcsncpy_s(g_nid.szTip, L"GPU-Switcher: Acquiring graphics adapter…", _TRUNCATE);
            break;
        }
    }

    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

// Thread proc: waits for the elevated child to exit, then posts WM_RESET_DONE with child's exit code
struct WaitCtx { HANDLE hProcess; HWND hwnd; };

static DWORD WINAPI WaitForResetThread(LPVOID param)
{
    auto* ctx = reinterpret_cast<WaitCtx*>(param);
    WaitForSingleObject(ctx->hProcess, INFINITE);
    DWORD exitCode = static_cast<DWORD>(ResetResult::EnableFailed);
    GetExitCodeProcess(ctx->hProcess, &exitCode);
    CloseHandle(ctx->hProcess);
    PostMessageW(ctx->hwnd, WM_RESET_DONE, static_cast<WPARAM>(exitCode), 0);
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
        UpdateTrayStatus();
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
            if (g_appState == AppState::Active && g_device)
            {
                HRESULT hr = g_device->GetDeviceRemovedReason();
                if (hr != S_OK)
                {
                    wchar_t buf[128];
                    swprintf_s(buf, L"D3D11 device lost (0x%08X) — re-acquiring", hr);
                    LogError(buf);
                    SetAppState(AppState::DeviceLost);
                    AcquireDGpu();
                    UpdateTrayStatus();
                }
            }

            NOTIFYICONDATAW balloon = g_nid;
            balloon.uFlags      |= NIF_INFO;
            balloon.dwInfoFlags  = (g_appState == AppState::Active ? NIIF_INFO : NIIF_WARNING) | NIIF_NOSOUND;
            wcsncpy_s(balloon.szInfoTitle, L"GPU-Switcher", _TRUNCATE);

            switch (g_appState)
            {
            case AppState::Active:
                if (HasPreferredLuid() && (g_activeLuid.LowPart != g_preferredLuid.LowPart || g_activeLuid.HighPart != g_preferredLuid.HighPart))
                {
                    swprintf_s(balloon.szInfo, L"Active discrete GPU (Fallback):\n%s\n\nPreferred GPU:\n%s",
                        g_activeGpuName.c_str(), g_preferredGpuName.c_str());
                }
                else
                {
                    swprintf_s(balloon.szInfo, L"Active discrete GPU:\n%s", g_activeGpuName.c_str());
                }
                break;
            case AppState::Resetting:
                wcsncpy_s(balloon.szInfo, L"Display adapter restart in progress…", _TRUNCATE);
                break;
            case AppState::WaitingForDiscreteGpu:
                if (!g_preferredGpuName.empty())
                    swprintf_s(balloon.szInfo, L"Discrete GPU (%s) is disconnected or absent.\nWaiting for reconnection.", g_preferredGpuName.c_str());
                else
                    wcsncpy_s(balloon.szInfo, L"Discrete GPU is absent.\nWaiting for reconnection.", _TRUNCATE);
                break;
            case AppState::NoDiscreteGpu:
                wcsncpy_s(balloon.szInfo, L"No discrete GPU detected on this system.\nRunning on integrated graphics.", _TRUNCATE);
                break;
            case AppState::DeviceLost:
                wcsncpy_s(balloon.szInfo, L"Discrete GPU device was lost or removed.\nAttempting recovery...", _TRUNCATE);
                break;
            case AppState::Acquiring:
                wcsncpy_s(balloon.szInfo, L"Acquiring discrete GPU...", _TRUNCATE);
                break;
            }

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

            AppendMenuW(menu, MF_STRING | (g_appState == AppState::Resetting ? MF_GRAYED : 0),
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
                if (g_appState == AppState::Resetting)
                    break;

                SetAppState(AppState::Resetting);
                UpdateTrayStatus(L"GPU-Switcher: Restarting display adapters…");
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
                    SetAppState(AppState::DeviceLost);
                    AcquireDGpu();
                    UpdateTrayStatus();
                    break;
                }

                LogInfo(L"Elevated child launched — waiting for reset completion");
                UpdateTrayStatus(L"Restarting display adapters… screen may flicker");

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
    {
        DWORD resetExitCode = static_cast<DWORD>(wParam);
        ResetResult res = static_cast<ResetResult>(resetExitCode);

        if (res == ResetResult::Success)
        {
            LogInfo(L"Elevated reset completed successfully — re-launching GPU-Switcher and exiting");
            Shell_NotifyIconW(NIM_DELETE, &g_nid);

            if (g_hMutex)
            {
                CloseHandle(g_hMutex);
                g_hMutex = nullptr;
            }

            wchar_t exePath[MAX_PATH] = {};
            if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
            {
                HINSTANCE hInstApp = ShellExecuteW(nullptr, L"open", exePath, nullptr, nullptr, SW_SHOWNORMAL);
                if (reinterpret_cast<INT_PTR>(hInstApp) <= 32)
                {
                    LogError(L"Failed to re-launch GPU-Switcher after reset");
                }
                else
                {
                    LogInfo(L"Successfully re-launched GPU-Switcher instance");
                }
            }

            PostQuitMessage(0);
        }
        else
        {
            wchar_t errBuf[256];
            swprintf_s(errBuf, L"Display adapter reset finished with non-success status: %s (code %u)",
                ResetResultToString(res), resetExitCode);
            LogError(errBuf);

            SetAppState(AppState::DeviceLost);
            AcquireDGpu();
            UpdateTrayStatus();

            NOTIFYICONDATAW balloon = g_nid;
            balloon.uFlags      |= NIF_INFO;
            balloon.dwInfoFlags  = NIIF_WARNING | NIIF_NOSOUND;
            wcsncpy_s(balloon.szInfoTitle, L"GPU-Switcher: Reset Incomplete", _TRUNCATE);

            if (res == ResetResult::PartialDisable)
            {
                wcsncpy_s(balloon.szInfo,
                    L"One or more display adapters could not be disabled. The reset was only partially applied.",
                    _TRUNCATE);
            }
            else
            {
                swprintf_s(balloon.szInfo,
                    L"Adapter reset failed with status: %s (code %u). Check log for details.",
                    ResetResultToString(res), resetExitCode);
            }
            Shell_NotifyIconW(NIM_MODIFY, &balloon);
        }
        return 0;
    }

    case WM_POWERBROADCAST:
        if (wParam == PBT_APMSUSPEND)
        {
            LogInfo(L"System entering suspend — releasing dGPU");
            ReleaseDGpu();
            UpdateTrayStatus(L"GPU-Switcher: Suspended");
        }
        else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
        {
            LogInfo(L"System resumed — scheduling dGPU re-acquisition");
            ReleaseDGpu();
            UpdateTrayStatus(L"GPU-Switcher: Resuming…");
            SetTimer(hwnd, TIMER_RESUME_REACQUIRE, 1500, nullptr);
        }
        return TRUE;

    case WM_DEVICECHANGE:
        // 0x0007 = DBT_DEVNODES_CHANGED
        if (wParam == 0x0007)
        {
            if (g_appState != AppState::Resetting)
            {
                LogInfo(L"Hardware devnodes changed — verifying GPU status");
                if (g_appState == AppState::Active && g_device)
                {
                    HRESULT hr = g_device->GetDeviceRemovedReason();
                    if (hr != S_OK)
                    {
                        wchar_t buf[128];
                        swprintf_s(buf, L"D3D11 device lost on devnodes change (0x%08X) — re-acquiring", hr);
                        LogError(buf);
                        SetAppState(AppState::DeviceLost);
                        AcquireDGpu();
                        UpdateTrayStatus();
                    }
                    else if (HasPreferredLuid() && (g_activeLuid.LowPart != g_preferredLuid.LowPart || g_activeLuid.HighPart != g_preferredLuid.HighPart))
                    {
                        // Only re-acquire if the preferred discrete GPU has actually reappeared in the topology
                        if (IsAdapterPresent(g_preferredLuid))
                        {
                            LogInfo(L"Preferred discrete GPU re-detected in topology — restoring preference");
                            if (AcquireDGpu())
                            {
                                UpdateTrayStatus();
                            }
                        }
                    }
                }
                else
                {
                    if (AcquireDGpu())
                    {
                        LogInfo(L"Discrete GPU detected and acquired following devnodes notification");
                        UpdateTrayStatus();
                    }
                }
            }
        }
        return TRUE;

    case WM_TIMER:
        if (wParam == TIMER_RESUME_REACQUIRE)
        {
            KillTimer(hwnd, TIMER_RESUME_REACQUIRE);
            LogInfo(L"Power resume timer fired — re-acquiring dGPU");
            if (!AcquireDGpu())
            {
                LogError(L"Failed to re-acquire dGPU after system resume");
            }
            UpdateTrayStatus();
        }
        else if (wParam == TIMER_HEALTH_CHECK)
        {
            if (g_appState == AppState::Active && g_device)
            {
                HRESULT hr = g_device->GetDeviceRemovedReason();
                if (hr != S_OK)
                {
                    wchar_t buf[128];
                    swprintf_s(buf, L"D3D11 device removed or lost (0x%08X) — re-acquiring", hr);
                    LogError(buf);
                    SetAppState(AppState::DeviceLost);
                    AcquireDGpu();
                    UpdateTrayStatus();
                }
                else if (HasPreferredLuid() && (g_activeLuid.LowPart != g_preferredLuid.LowPart || g_activeLuid.HighPart != g_preferredLuid.HighPart))
                {
                    // Periodically test if preferred GPU has re-appeared in the topology
                    if (IsAdapterPresent(g_preferredLuid))
                    {
                        LogInfo(L"Preferred discrete GPU detected in topology during periodic scan — restoring preference");
                        if (AcquireDGpu())
                        {
                            UpdateTrayStatus();
                        }
                    }
                }
            }
            else if (g_appState == AppState::WaitingForDiscreteGpu ||
                     g_appState == AppState::NoDiscreteGpu ||
                     g_appState == AppState::DeviceLost)
            {
                if (AcquireDGpu())
                {
                    LogInfo(L"Discrete GPU detected and acquired during periodic scan");
                    UpdateTrayStatus();
                }
            }
        }
        return 0;

    case WM_ACTIVATE_INSTANCE:
    {
        UpdateTrayStatus();
        NOTIFYICONDATAW balloon = g_nid;
        balloon.uFlags      |= NIF_INFO;
        balloon.dwInfoFlags  = (g_appState == AppState::Active ? NIIF_INFO : NIIF_WARNING) | NIIF_NOSOUND;
        wcsncpy_s(balloon.szInfoTitle, L"GPU-Switcher", _TRUNCATE);

        switch (g_appState)
        {
        case AppState::Active:
            if (HasPreferredLuid() && (g_activeLuid.LowPart != g_preferredLuid.LowPart || g_activeLuid.HighPart != g_preferredLuid.HighPart))
            {
                swprintf_s(balloon.szInfo, L"Already active in system tray.\nGPU (Fallback): %s\n(Preferred: %s)",
                    g_activeGpuName.c_str(), g_preferredGpuName.c_str());
            }
            else
            {
                swprintf_s(balloon.szInfo, L"Already active in system tray.\nGPU: %s", g_activeGpuName.c_str());
            }
            break;
        case AppState::Resetting:
            wcsncpy_s(balloon.szInfo, L"Already running (display reset in progress)", _TRUNCATE);
            break;
        case AppState::WaitingForDiscreteGpu:
            if (!g_preferredGpuName.empty())
                swprintf_s(balloon.szInfo, L"Already running (waiting for %s)", g_preferredGpuName.c_str());
            else
                wcsncpy_s(balloon.szInfo, L"Already running (waiting for discrete GPU)", _TRUNCATE);
            break;
        case AppState::NoDiscreteGpu:
            wcsncpy_s(balloon.szInfo, L"Already running (no discrete GPU detected)", _TRUNCATE);
            break;
        case AppState::DeviceLost:
            wcsncpy_s(balloon.szInfo, L"Already running (discrete GPU lost — recovering)", _TRUNCATE);
            break;
        case AppState::Acquiring:
            wcsncpy_s(balloon.szInfo, L"Already running (acquiring discrete GPU)", _TRUNCATE);
            break;
        }

        Shell_NotifyIconW(NIM_MODIFY, &balloon);
        return 0;
    }

    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_RESUME_REACQUIRE);
        KillTimer(hwnd, TIMER_HEALTH_CHECK);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Elevated path: called when launched with --reset-gpu
static int RunElevatedReset()
{
    LogInfo(L"GPU-Switcher elevated reset started");
    ResetResult res = CycleAllDisplayAdapters();
    wchar_t buf[128];
    swprintf_s(buf, L"GPU-Switcher elevated reset completed with result: %s (%d)",
        ResetResultToString(res), static_cast<int>(res));
    LogInfo(buf);
    return static_cast<int>(res);
}

// lpCmdLine is unused because we parse via GetCommandLineW().
// Removing the parameter name silences MSVC warning C4100.
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    // Check command line arguments first
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool resetMode = false;
    bool exitMode  = false;
    bool helpMode  = false;

    if (argv)
    {
        for (int i = 1; i < argc; ++i)
        {
            if (_wcsicmp(argv[i], L"--reset-gpu") == 0)
                resetMode = true;
            else if (_wcsicmp(argv[i], L"--exit") == 0 || _wcsicmp(argv[i], L"--quit") == 0)
                exitMode = true;
            else if (_wcsicmp(argv[i], L"--help") == 0 || _wcsicmp(argv[i], L"-h") == 0 || _wcsicmp(argv[i], L"/?") == 0)
                helpMode = true;
        }
        LocalFree(argv);
    }

    if (helpMode)
    {
        MessageBoxW(nullptr,
            L"GPU-Switcher v" APP_VERSION L"\n\n"
            L"Usage:\n"
            L"  GPU-Switcher.exe          Start tray application\n"
            L"  GPU-Switcher.exe --exit   Gracefully close running instance\n"
            L"  GPU-Switcher.exe --help   Show this help dialog\n\n"
            L"Helper Flags:\n"
            L"  --reset-gpu               Cycle display adapters (elevated helper)",
            L"GPU-Switcher", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    if (exitMode)
    {
        HWND hExisting = FindWindowW(L"TrayHookClass", L"GPU-Switcher");
        if (hExisting)
        {
            PostMessageW(hExisting, WM_CLOSE, 0, 0);
            LogInfo(L"Sent exit request to running GPU-Switcher instance");
        }
        else
        {
            LogInfo(L"No running GPU-Switcher instance found to exit");
        }
        return 0;
    }

    if (resetMode)
        return RunElevatedReset();

    // Normal tray mode
    g_hInst = hInst;

    g_hMutex = CreateMutexW(nullptr, TRUE, L"GPUSwitcherMutex");
    if (!g_hMutex)
    {
        LogError(L"Failed to create mutex");
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        LogInfo(L"Another instance is already running — notifying running instance");
        HWND hExisting = FindWindowW(L"TrayHookClass", L"GPU-Switcher");
        if (hExisting)
        {
            PostMessageW(hExisting, WM_ACTIVATE_INSTANCE, 0, 0);
        }
        CloseHandle(g_hMutex);
        g_hMutex = nullptr;
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
        LogError(L"Failed to acquire discrete GPU on startup");

    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = hwnd;
    g_nid.uID              = TRAY_ID;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon            = GetVendorIcon(g_appState == AppState::Active, g_activeVendorId);
    if (!g_nid.hIcon)
        g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);

    UpdateTrayStatus();

    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);

    // Periodic health check every 30 seconds
    SetTimer(hwnd, TIMER_HEALTH_CHECK, 30000, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    ReleaseDGpu();
    LogInfo(L"GPU-Switcher exited cleanly");
    if (g_hMutex)
    {
        CloseHandle(g_hMutex);
        g_hMutex = nullptr;
    }
    return 0;
}

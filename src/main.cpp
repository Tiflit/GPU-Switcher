#include <windows.h>
#include <shellapi.h>
#include <dxgi.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <algorithm>
#include <optional>
#include <utility>

#include "resource.h"
#include "gpu/acquisition.h"
#include "gpu/power_gate.h"
#include "reset/reset_lock.h"
#include "reset/reset_policy.h"
#include "build_info.h"
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
#define ID_VIEW_LOG             1004

#define TIMER_RESUME_REACQUIRE  2001
#define TIMER_HEALTH_CHECK      2002
#define TIMER_RESET_MONITOR     2003

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
static HWND                                 g_window            = nullptr;
static HANDLE                               g_resetProcess      = nullptr;
static reset::Watch                         g_resetWatch;
static gpu::PowerGate                       g_powerGate;
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
    for (UINT i = 0; ; ++i)
    {
        HRESULT hr = factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf());
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(hr))
        {
            LogError(L"EnumAdapters1 failed during adapter presence check");
            return false;
        }
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

static bool SameLuid(const LUID& left, const LUID& right)
{
    return left.LowPart == right.LowPart && left.HighPart == right.HighPart;
}

struct PendingGpu
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
};

static bool AcquireDGpu()
{
    // All acquisition paths must leave devices alone while the helper resets them.
    if (g_appState == AppState::Resetting || g_resetWatch.Active())
        return false;
    if (!g_powerGate.CanAcquire())
    {
        // Reset start can cancel a pending resume timer. Rearm the remaining
        // delay on its cancellation/failure path instead of acquiring early.
        if (g_powerGate.AwaitingResume())
            SetTimer(g_window, TIMER_RESUME_REACQUIRE,
                g_powerGate.RemainingDelay(GetTickCount64()), nullptr);
        return false;
    }
    if (reset::Active())
    {
        ReleaseDGpu();
        SetAppState(AppState::Resetting);
        g_resetWatch.Start(GetTickCount64());
        SetTimer(g_window, TIMER_RESET_MONITOR, 500, nullptr);
        LogInfo(L"External display reset detected; waiting before GPU acquisition");
        return false;
    }

    const bool currentHealthy = g_device && g_device->GetDeviceRemovedReason() == S_OK;
    if (!currentHealthy)
        ReleaseDGpu();

    ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1),
        reinterpret_cast<void**>(factory.GetAddressOf()));
    if (FAILED(hr))
    {
        LogError(L"CreateDXGIFactory1 failed");
        SetAppState(currentHealthy ? AppState::Active : AppState::DeviceLost);
        return currentHealthy;
    }

    std::vector<GpuCandidate> discreteCandidates;
    for (UINT i = 0; ; ++i)
    {
        ComPtr<IDXGIAdapter1> adapter;
        hr = factory->EnumAdapters1(i, adapter.GetAddressOf());
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(hr))
        {
            LogError(L"EnumAdapters1 failed during GPU acquisition");
            SetAppState(currentHealthy ? AppState::Active : AppState::DeviceLost);
            return currentHealthy;
        }

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
                discreteCandidates.push_back(info);
        }
    }

    if (discreteCandidates.empty())
    {
        if (currentHealthy)
        {
            LogInfo(L"No discrete candidates enumerated; retaining healthy active device");
            SetAppState(AppState::Active);
            return true;
        }
        SetAppState(HasPreferredLuid() ? AppState::WaitingForDiscreteGpu : AppState::NoDiscreteGpu);
        LogInfo(L"No discrete GPU detected; waiting for an eligible adapter");
        return false;
    }

    // Preserve the existing vendor/VRAM score within each tier. Equal scores
    // retain enumeration order; selection-policy changes are a separate task.
    auto priority = [currentHealthy](const GpuCandidate& candidate)
    {
        return gpu::CandidatePriority{
            HasPreferredLuid() && SameLuid(candidate.luid, g_preferredLuid),
            currentHealthy && SameLuid(candidate.luid, g_activeLuid),
            candidate.score };
    };
    std::stable_sort(discreteCandidates.begin(), discreteCandidates.end(),
        [&](const GpuCandidate& left, const GpuCandidate& right)
        {
            return gpu::HigherPriority(priority(left), priority(right));
        });

    if (!currentHealthy)
        SetAppState(AppState::Acquiring);

    const auto result = gpu::AcquireInOrder(discreteCandidates,
        [] { return g_device && g_device->GetDeviceRemovedReason() == S_OK; },
        [](const GpuCandidate& candidate) { return SameLuid(candidate.luid, g_activeLuid); },
        [](const GpuCandidate& candidate) -> std::optional<PendingGpu>
        {
            PendingGpu pending;
            D3D_FEATURE_LEVEL level = {};
            const D3D_FEATURE_LEVEL levels[] = {
                D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
            HRESULT createHr = D3D11CreateDevice(candidate.adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN,
                nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, static_cast<UINT>(ARRAYSIZE(levels)),
                D3D11_SDK_VERSION, pending.device.GetAddressOf(), &level, pending.context.GetAddressOf());
            if (FAILED(createHr))
            {
                wchar_t buf[256];
                swprintf_s(buf, L"D3D11CreateDevice failed for %s: 0x%08X",
                    candidate.desc.Description, createHr);
                LogError(buf);
                return std::nullopt;
            }
            if (level < D3D_FEATURE_LEVEL_11_0)
                LogError(L"Warning: acquired adapter feature level is below D3D_FEATURE_LEVEL_11_0");
            return std::optional<PendingGpu>{ std::move(pending) };
        },
        [](const GpuCandidate& candidate, PendingGpu&& pending)
        {
            // Commit only after a replacement device and context exist.
            ReleaseDGpu();
            g_device = std::move(pending.device);
            g_context = std::move(pending.context);
            g_activeVendorId = candidate.desc.VendorId;
            g_activeGpuName = candidate.desc.Description;
            g_activeLuid = candidate.luid;
            if (!HasPreferredLuid())
            {
                g_preferredLuid = candidate.luid;
                g_preferredGpuName = candidate.desc.Description;
                LogInfo(std::wstring(L"Established preferred discrete GPU: ") + g_preferredGpuName);
            }
            LogInfo(std::wstring(L"Successfully initialized D3D11 device on: ") + g_activeGpuName);
        });

    if (result == gpu::AcquisitionResult::Failed)
    {
        ReleaseDGpu();
        SetAppState(AppState::DeviceLost);
        return false;
    }
    SetAppState(AppState::Active);
    if (result == gpu::AcquisitionResult::Retained)
        LogInfo(L"Retaining healthy active GPU device");
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
            wcsncpy_s(g_nid.szTip, g_resetWatch.TimedOut() ?
                L"GPU-Switcher: Reset taking longer than expected; still waiting" :
                L"GPU-Switcher: Restarting display adapters…", _TRUNCATE);
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

static void ShowResetNotice(const wchar_t* message)
{
    NOTIFYICONDATAW balloon = g_nid;
    balloon.uFlags |= NIF_INFO;
    balloon.dwInfoFlags = NIIF_WARNING | NIIF_NOSOUND;
    wcsncpy_s(balloon.szInfoTitle, L"GPU-Switcher: Display Reset", _TRUNCATE);
    wcsncpy_s(balloon.szInfo, message, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &balloon);
}

static void PollReset(HWND hwnd)
{
    if (!g_resetWatch.Active()) return;
    const bool ownHelper = g_resetProcess != nullptr;
    const DWORD waitResult = ownHelper ? WaitForSingleObject(g_resetProcess, 0) :
        (reset::Active() ? WAIT_TIMEOUT : WAIT_OBJECT_0);
    const auto event = g_resetWatch.Observe(waitResult == WAIT_OBJECT_0 ? reset::WaitState::Complete :
        (waitResult == WAIT_TIMEOUT ? reset::WaitState::Running : reset::WaitState::Failed), GetTickCount64());
    if (event == reset::WatchEvent::Completed)
    {
        KillTimer(hwnd, TIMER_RESET_MONITOR);
        if (ownHelper)
        {
            DWORD exitCode = static_cast<DWORD>(ResetResult::VerifyFailed);
            if (!GetExitCodeProcess(g_resetProcess, &exitCode))
                LogError(L"Could not read completed reset helper exit code");
            CloseHandle(g_resetProcess);
            g_resetProcess = nullptr;
            SendMessageW(hwnd, WM_RESET_DONE, static_cast<WPARAM>(exitCode), 0);
        }
        else
        {
            LogInfo(L"External reset lock released; recovering GPU (external result unknown)");
            SetAppState(AppState::DeviceLost);
            AcquireDGpu();
            UpdateTrayStatus();
        }
    }
    else if (event == reset::WatchEvent::TimedOut)
    {
        LogError(L"Reset exceeded 60 seconds; supervision and acquisition exclusion remain active");
        UpdateTrayStatus();
        ShowResetNotice(L"Reset is taking longer than expected. Still waiting for the helper; GPU acquisition and another reset remain blocked.");
    }
    else if (event == reset::WatchEvent::WaitFailed)
    {
        LogError(L"Reset process wait failed; retaining the handle and continuing supervision");
        ShowResetNotice(L"Could not query reset completion. The utility will keep monitoring; another reset remains blocked.");
    }
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
                wcsncpy_s(balloon.szInfo, g_resetWatch.TimedOut() ?
                    L"Reset is taking longer than expected. Still monitoring; acquisition and another reset remain blocked." :
                    L"Display adapter restart in progress…", _TRUNCATE);
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

            AppendMenuW(menu, MF_STRING, ID_VIEW_LOG, L"View Log");
            AppendMenuW(menu, MF_STRING | (g_appState == AppState::Resetting ? MF_GRAYED : 0),
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
                if (g_appState == AppState::Resetting || g_resetWatch.Active())
                    break;
                if (reset::Active())
                {
                    AcquireDGpu();
                    UpdateTrayStatus();
                    ShowResetNotice(L"Another display reset is active or its lock cannot be inspected. Waiting for it to finish.");
                    break;
                }
                if (!SetTimer(hwnd, TIMER_RESET_MONITOR, 500, nullptr))
                {
                    LogError(L"Could not start reset supervision timer; helper was not launched");
                    ShowResetNotice(L"Could not start reset monitoring. No reset was launched.");
                    break;
                }

                SetAppState(AppState::Resetting);
                KillTimer(hwnd, TIMER_RESUME_REACQUIRE);
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
                    KillTimer(hwnd, TIMER_RESET_MONITOR);
                    LogInfo(L"Elevated launch cancelled or failed — reacquiring dGPU");
                    SetAppState(AppState::DeviceLost);
                    AcquireDGpu();
                    UpdateTrayStatus();
                    break;
                }

                LogInfo(L"Elevated child launched — waiting for reset completion");
                UpdateTrayStatus(L"Restarting display adapters… screen may flicker");

                g_resetProcess = sei.hProcess;
                g_resetWatch.Start(GetTickCount64());
                break;
            }

            case ID_VIEW_LOG:
                ShellExecuteW(hwnd, L"open", GetLogPath().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;

            case ID_EXIT:
                SendMessageW(hwnd, WM_CLOSE, 0, 0);
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
            // A reset is an explicit request to clear GPU clients and leave the
            // dGPU available to sleep. Normal shutdown below releases our device
            // and mutex; launching a replacement process would pin it again.
            LogInfo(L"Elevated reset completed successfully — exiting GPU-Switcher without relaunch");
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
            g_powerGate.Suspend();
            KillTimer(hwnd, TIMER_RESUME_REACQUIRE);
            LogInfo(L"System entering suspend — releasing dGPU");
            ReleaseDGpu();
            if (g_appState == AppState::Resetting)
                UpdateTrayStatus();
            else
                UpdateTrayStatus(L"GPU-Switcher: Suspended");
        }
        else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
        {
            // Windows can deliver both notifications for the same wake. Keep
            // one delay, and update power state even while a reset is active.
            if (!g_powerGate.Resume(GetTickCount64()))
                return TRUE;
            LogInfo(L"System resumed — scheduling dGPU re-acquisition");
            ReleaseDGpu();
            if (g_appState == AppState::Resetting)
                UpdateTrayStatus();
            else
                UpdateTrayStatus(L"GPU-Switcher: Resuming…");
            SetTimer(hwnd, TIMER_RESUME_REACQUIRE, 1500, nullptr);
        }
        return TRUE;

    case WM_DEVICECHANGE:
        // 0x0007 = DBT_DEVNODES_CHANGED
        if (wParam == 0x0007)
        {
            if (g_appState != AppState::Resetting && g_powerGate.CanAcquire())
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
        if (wParam == TIMER_RESET_MONITOR)
        {
            PollReset(hwnd);
            return 0;
        }
        if (wParam == TIMER_RESUME_REACQUIRE)
        {
            // Ignore stale/early timer messages, including timers from a
            // previous wake or timers already queued when suspend began.
            if (!g_powerGate.CompleteResume(GetTickCount64()))
            {
                KillTimer(hwnd, TIMER_RESUME_REACQUIRE);
                if (g_powerGate.AwaitingResume())
                    SetTimer(hwnd, TIMER_RESUME_REACQUIRE,
                        g_powerGate.RemainingDelay(GetTickCount64()), nullptr);
                return 0;
            }
            KillTimer(hwnd, TIMER_RESUME_REACQUIRE);
            if (g_appState == AppState::Resetting || g_resetWatch.Active())
                return 0;

            LogInfo(L"Power resume timer fired — re-acquiring dGPU");
            if (!AcquireDGpu())
            {
                LogError(L"Failed to re-acquire dGPU after system resume");
            }
            UpdateTrayStatus();
        }
        else if (wParam == TIMER_HEALTH_CHECK)
        {
            if (g_resetWatch.Active())
            {
                PollReset(hwnd); // Fallback if the dedicated timer could not be established.
                return 0;
            }
            if (!g_powerGate.CanAcquire())
                return 0;
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
        if (g_appState == AppState::Resetting || g_resetWatch.Active())
        {
            ShowResetNotice(L"A display reset is still being monitored. Exit is available after the helper finishes.");
            return 0;
        }
        PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_RESUME_REACQUIRE);
        KillTimer(hwnd, TIMER_HEALTH_CHECK);
        KillTimer(hwnd, TIMER_RESET_MONITOR);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Elevated path: called when launched with --reset-gpu
static int RunElevatedReset()
{
    LogInfo(L"GPU-Switcher v" APP_VERSION L" build " GPU_SWITCHER_BUILD_REVISION L" elevated reset started");
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

    LogInfo(L"GPU-Switcher v" APP_VERSION L" build " GPU_SWITCHER_BUILD_REVISION L" started");

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

    if (!hwnd)
    {
        LogError(L"Could not create tray window; reset monitoring is unavailable");
        CloseHandle(g_hMutex);
        g_hMutex = nullptr;
        return 1;
    }
    g_window = hwnd;
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
    if (g_resetProcess) CloseHandle(g_resetProcess);
    LogInfo(L"GPU-Switcher exited cleanly");
    if (g_hMutex)
    {
        CloseHandle(g_hMutex);
        g_hMutex = nullptr;
    }
    return 0;
}

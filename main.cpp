/*
 * gpu_tray — Lenovo Legion dGPU Display Switcher (NVAPI-only)
 * - Attempts to force dGPU ownership using NVIDIA NVAPI
 * - Automatic switch to dGPU on launch
 * - Log file overwritten on each launch (gpu_tray.log)
 * - Correct GPU detection (Intel = iGPU, NVIDIA/AMD = dGPU)
 * - Uses ComPtr for COM (no leaks)
 * - Single-file implementation
 *
 * NOTE:
 *  - Requires NVIDIA NVAPI (nvapi.h + nvapi64.lib or nvapi.lib)
 *  - This uses NVAPI to request dGPU display ownership; behavior depends on
 *    firmware/driver and may still be overridden by Advanced Optimus logic.
 */

#include <Windows.h>
#include <shellapi.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <fstream>
#include <strsafe.h>

// NVAPI
#include "nvapi.h"

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "nvapi64.lib") // or nvapi.lib depending on target

// ============================================================================
//  Logging (overwrites file on each launch)
// ============================================================================

static std::wofstream g_log;

static void Log(const std::wstring& msg)
{
    if (g_log.is_open())
        g_log << msg << L"\n";
    OutputDebugStringW((L"[gpu_tray] " + msg + L"\n").c_str());
}

// ============================================================================
//  Admin privilege check
// ============================================================================

static bool IsAdmin()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(
            &NtAuthority, 2,
            SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS,
            0,0,0,0,0,0,
            &adminGroup))
    {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

// ============================================================================
//  GPU logic (DXGI + NVAPI)
// ============================================================================

enum class GpuMode { Unknown, Hybrid, DGPU };
static GpuMode g_currentMode = GpuMode::Unknown;

struct GpuInfo {
    std::wstring name;
    UINT vendorId = 0;
    bool isIntegrated = false;
};

static GpuInfo g_iGpu, g_dGpu;
static bool g_haveIGpu = false, g_haveDGpu = false;

// NVAPI handles
static bool g_nvapiOk = false;
static NvPhysicalGpuHandle g_nvGpu = nullptr;

static void DetectGpus()
{
    Log(L"DetectGpus: starting");

    ComPtr<IDXGIFactory1> pFactory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&pFactory))))
    {
        Log(L"DetectGpus: CreateDXGIFactory1 failed");
        return;
    }

    for (UINT i = 0;; ++i)
    {
        ComPtr<IDXGIAdapter1> pAdapter;
        HRESULT hr = pFactory->EnumAdapters1(i, &pAdapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;

        DXGI_ADAPTER_DESC1 desc;
        if (FAILED(pAdapter->GetDesc1(&desc))) continue;
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

        GpuInfo info;
        info.vendorId = desc.VendorId;
        info.name = desc.Description;

        bool integrated = (desc.VendorId == 0x8086);
        info.isIntegrated = integrated;

        if (integrated && !g_haveIGpu)
        {
            g_iGpu = info;
            g_haveIGpu = true;
        }
        else if (!integrated && !g_haveDGpu)
        {
            g_dGpu = info;
            g_haveDGpu = true;
        }
    }

    if (!g_haveIGpu && g_haveDGpu)
    {
        g_iGpu = g_dGpu;
        g_haveIGpu = true;
        Log(L"DetectGpus: iGPU hidden, using dGPU as fallback");
    }

    Log(L"DetectGpus: done");
}

static void InitNvApi()
{
    Log(L"NVAPI: Initialize");

    NvAPI_Status st = NvAPI_Initialize();
    if (st != NVAPI_OK)
    {
        Log(L"NVAPI: NvAPI_Initialize failed");
        return;
    }

    NvPhysicalGpuHandle handles[NVAPI_MAX_PHYSICAL_GPUS] = {};
    NvU32 count = 0;
    st = NvAPI_EnumPhysicalGPUs(handles, &count);
    if (st != NVAPI_OK || count == 0)
    {
        Log(L"NVAPI: NvAPI_EnumPhysicalGPUs failed or no GPUs");
        return;
    }

    // Pick the first NVIDIA GPU
    g_nvGpu = handles[0];
    g_nvapiOk = true;
    Log(L"NVAPI: initialized, physical GPU handle acquired");
}

// We cannot reliably query "Hybrid vs dGPU" mode via NVAPI in a generic way,
// so we treat the mode as "last requested" for UI purposes.
static GpuMode QueryMode()
{
    return g_currentMode;
}

// Attempt to force dGPU ownership via NVAPI.
// This is best-effort and may still be overridden by Advanced Optimus.
static bool SetMode(GpuMode target)
{
    if (!g_nvapiOk || !g_nvGpu)
    {
        Log(L"SetMode: NVAPI not initialized");
        return false;
    }

    if (target != GpuMode::DGPU)
    {
        // We don't try to force iGPU via NVAPI; we just treat this as "Hybrid".
        Log(L"SetMode: requested HYBRID (no NVAPI action)");
        g_currentMode = GpuMode::Hybrid;
        return true;
    }

    // Try to set display owner to the NVIDIA GPU.
    // We use NVAPI_DEFAULT_HANDLE to target all displays where possible.
    Log(L"SetMode: attempting NVAPI GPU_SetDisplayOwner to DGPU");

    NvAPI_Status st = NvAPI_GPU_SetDisplayOwner(g_nvGpu, NVAPI_DEFAULT_HANDLE);
    if (st == NVAPI_OK)
    {
        Log(L"SetMode: NVAPI_GPU_SetDisplayOwner succeeded");
        g_currentMode = GpuMode::DGPU;
        return true;
    }
    else
    {
        Log(L"SetMode: NVAPI_GPU_SetDisplayOwner failed");
        return false;
    }
}

static const GpuInfo* ActiveGpuInfo(GpuMode mode)
{
    if (mode == GpuMode::DGPU && g_haveDGpu) return &g_dGpu;
    if (mode == GpuMode::Hybrid && g_haveIGpu) return &g_iGpu;
    return nullptr;
}

// ============================================================================
//  Icons
// ============================================================================

static HICON MakeLetterIcon(WCHAR letter, COLORREF bg, int size)
{
    int SZ = (size > 0) ? size : 16;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(bih);
    bih.biWidth = SZ;
    bih.biHeight = -SZ;
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;

    BITMAPINFO bi = {};
    bi.bmiHeader = bih;

    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(
        hdcMem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBmp);

    RECT rc = {0,0,SZ,SZ};
    HBRUSH hBg = CreateSolidBrush(RGB(0,0,0));
    FillRect(hdcMem, &rc, hBg);
    DeleteObject(hBg);

    HBRUSH hBrush = CreateSolidBrush(bg);
    HPEN hPen = CreatePen(PS_SOLID, 1, bg);
    SelectObject(hdcMem, hBrush);
    SelectObject(hdcMem, hPen);
    Ellipse(hdcMem, 1, 1, SZ-1, SZ-1);
    DeleteObject(hBrush);
    DeleteObject(hPen);

    HFONT hFont = CreateFontW(
        SZ-3, 0, 0, 0, FW_HEAVY, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS,
        L"Segoe UI");
    HFONT hOldF = (HFONT)SelectObject(hdcMem, hFont);
    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(255,255,255));
    WCHAR s[2] = {letter, 0};
    DrawTextW(hdcMem, s, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdcMem, hOldF);
    DeleteObject(hFont);

    HDC hdcMask = CreateCompatibleDC(hdcScreen);
    HBITMAP hMask = CreateBitmap(SZ, SZ, 1, 1, nullptr);
    HBITMAP hOldM = (HBITMAP)SelectObject(hdcMask, hMask);
    SetBkColor(hdcMem, RGB(0,0,0));
    BitBlt(hdcMask, 0, 0, SZ, SZ, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMask, hOldM);
    SelectObject(hdcMem, hOld);

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = hBmp;
    ii.hbmMask = hMask;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hBmp);
    DeleteObject(hMask);
    DeleteDC(hdcMem);
    DeleteDC(hdcMask);
    ReleaseDC(nullptr, hdcScreen);

    return hIcon;
}

static HICON g_hIconUnknown = nullptr;
static HICON g_hIconIntel_i = nullptr;
static HICON g_hIconIntel_I = nullptr;
static HICON g_hIconNvidia_n = nullptr;
static HICON g_hIconNvidia_N = nullptr;
static HICON g_hIconAmd_a = nullptr;
static HICON g_hIconAmd_A = nullptr;

static void CreateIcons()
{
    int sz = GetSystemMetrics(SM_CXSMICON);
    if (sz <= 0) sz = 16;

    g_hIconUnknown = MakeLetterIcon(L'?', RGB(136,136,136), sz);
    g_hIconIntel_i = MakeLetterIcon(L'i', RGB(91,141,184), sz);
    g_hIconIntel_I = MakeLetterIcon(L'I', RGB(91,141,184), sz);
    g_hIconNvidia_n = MakeLetterIcon(L'n', RGB(90,160,106), sz);
    g_hIconNvidia_N = MakeLetterIcon(L'N', RGB(90,160,106), sz);
    g_hIconAmd_a = MakeLetterIcon(L'a', RGB(200,80,80), sz);
    g_hIconAmd_A = MakeLetterIcon(L'A', RGB(200,80,80), sz);
}

static void DestroyIcons()
{
    DestroyIcon(g_hIconUnknown);
    DestroyIcon(g_hIconIntel_i);
    DestroyIcon(g_hIconIntel_I);
    DestroyIcon(g_hIconNvidia_n);
    DestroyIcon(g_hIconNvidia_N);
    DestroyIcon(g_hIconAmd_a);
    DestroyIcon(g_hIconAmd_A);
}

static HICON ModeIcon(GpuMode mode)
{
    const GpuInfo* info = ActiveGpuInfo(mode);
    if (!info) return g_hIconUnknown;

    UINT v = info->vendorId;
    bool integrated = info->isIntegrated;

    if (v == 0x8086) return integrated ? g_hIconIntel_i : g_hIconIntel_I;
    if (v == 0x10DE) return integrated ? g_hIconNvidia_n : g_hIconNvidia_N;
    if (v == 0x1002 || v == 0x1022) return integrated ? g_hIconAmd_a : g_hIconAmd_A;

    return g_hIconUnknown;
}

static std::wstring ModeTooltip(GpuMode mode)
{
    const GpuInfo* info = ActiveGpuInfo(mode);
    if (!info)
        return L"GPU mode unknown";

    std::wstring tip;
    if (mode == GpuMode::DGPU) tip = L"dGPU requested — ";
    else if (mode == GpuMode::Hybrid) tip = L"Hybrid/iGPU requested — ";
    else tip = L"GPU mode unknown — ";

    tip += info->name;
    if (mode == GpuMode::DGPU) tip += L" (dGPU)";
    else if (mode == GpuMode::Hybrid) tip += L" (iGPU display)";

    return tip;
}

// ============================================================================
//  Tray + notifications
// ============================================================================

#define WM_TRAY          (WM_USER + 1)
#define TRAY_ID          1
#define POLL_TIMER_ID    1
#define POLL_INTERVAL_MS 5000

enum class MenuCmd : UINT {
    Toggle = 1001,
    Exit   = 1002
};

static HWND g_hwnd = nullptr;

static void ShowSwitchNotification(GpuMode mode)
{
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_INFO;

    StringCchCopyW(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle),
                    L"GPU display mode change requested");

    const GpuInfo* info = ActiveGpuInfo(mode);
    std::wstring msg;

    if (!info)
        msg = L"Requested GPU display change.";
    else if (mode == GpuMode::DGPU)
        msg = L"Requested display to be driven by dGPU: " + info->name;
    else
        msg = L"Requested display to be driven by iGPU: " + info->name;

    StringCchCopyW(nid.szInfo, ARRAYSIZE(nid.szInfo), msg.c_str());
    nid.dwInfoFlags = NIIF_INFO;

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void AddTrayIcon(GpuMode mode)
{
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = ModeIcon(mode);

    std::wstring tip = ModeTooltip(mode);
    StringCchCopyW(nid.szTip, ARRAYSIZE(nid.szTip), tip.c_str());

    Shell_NotifyIconW(NIM_ADD, &nid);

    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

static void RefreshTrayIcon(GpuMode newMode)
{
    if (newMode == g_currentMode) return;

    bool first = (g_currentMode == GpuMode::Unknown);
    g_currentMode = newMode;

    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_TIP;
    nid.hIcon = ModeIcon(newMode);

    std::wstring tip = ModeTooltip(newMode);
    StringCchCopyW(nid.szTip, ARRAYSIZE(nid.szTip), tip.c_str());

    Shell_NotifyIconW(NIM_MODIFY, &nid);

    if (!first)
        ShowSwitchNotification(newMode);
}

static void RemoveTrayIcon()
{
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = TRAY_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

// ============================================================================
//  Window procedure
// ============================================================================

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_TIMER && wParam == POLL_TIMER_ID)
    {
        // We don't have a reliable query; just keep the last requested mode.
        RefreshTrayIcon(g_currentMode);
        return 0;
    }

    if (msg == WM_DPICHANGED)
    {
        DestroyIcons();
        CreateIcons();
        RefreshTrayIcon(g_currentMode);
        return 0;
    }

    if (msg == WM_TRAY)
    {
        UINT mouseMsg = LOWORD(lParam);

        if (mouseMsg == WM_CONTEXTMENU || mouseMsg == WM_RBUTTONUP)
        {
            GpuMode mode = g_currentMode;

            const GpuInfo* info = ActiveGpuInfo(mode);
            std::wstring label;

            if (!info)
                label = L"Requested GPU: Unknown";
            else
            {
                label = L"Requested GPU: ";
                label += info->name;
                if (mode == GpuMode::DGPU) label += L" (dGPU)";
                else if (mode == GpuMode::Hybrid) label += L" (iGPU display)";
            }

            std::wstring toggleText;
            if (mode == GpuMode::DGPU)
                toggleText = L"Request iGPU / Hybrid display mode";
            else
                toggleText = L"Request dGPU display mode";

            std::wstring exitText = L"Exit (no further GPU changes)";

            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, label.c_str());
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, (UINT)MenuCmd::Toggle, toggleText.c_str());
            AppendMenuW(menu, MF_STRING, (UINT)MenuCmd::Exit,   exitText.c_str());

            SetForegroundWindow(hwnd);

            POINT pt;
            GetCursorPos(&pt);

            int cmd = TrackPopupMenu(menu,
                                     TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                     pt.x, pt.y, 0, hwnd, nullptr);
            PostMessage(hwnd, WM_NULL, 0, 0);
            DestroyMenu(menu);

            if (cmd == (int)MenuCmd::Toggle)
            {
                if (!g_nvapiOk) return 0;

                GpuMode target =
                    (g_currentMode == GpuMode::DGPU) ? GpuMode::Hybrid : GpuMode::DGPU;

                if (SetMode(target))
                {
                    Sleep(1000);
                    RefreshTrayIcon(target);
                }
            }
            else if (cmd == (int)MenuCmd::Exit)
            {
                PostQuitMessage(0);
            }
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================================
//  Entry point
// ============================================================================

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    // Open log file (overwrite each launch)
    g_log.open(L"gpu_tray.log", std::ios::out | std::ios::trunc);
    if (g_log.is_open())
        g_log << L"gpu_tray started (NVAPI-only)\n";

    if (!IsAdmin())
    {
        MessageBoxW(nullptr,
                    L"This tool must be run as Administrator.\n\n"
                    L"Right-click the .exe and choose \"Run as administrator\".",
                    L"GPU Tray Switcher",
                    MB_ICONERROR | MB_OK);
        return 1;
    }

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"LenovoGpuTrayMutex_NVAPI");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    DetectGpus();
    InitNvApi();
    CreateIcons();

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"GpuTrayClassNvApi";
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"",
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        nullptr, nullptr, hInst, nullptr
    );

    if (!g_hwnd)
    {
        DestroyIcons();
        if (hMutex)
        {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        return 1;
    }

    ShowWindow(g_hwnd, SW_HIDE);

    // Automatic request: dGPU on launch
    if (g_nvapiOk)
    {
        Log(L"Startup: requesting DGPU via NVAPI");
        if (SetMode(GpuMode::DGPU))
        {
            Sleep(1000);
            AddTrayIcon(GpuMode::DGPU);
            RefreshTrayIcon(GpuMode::DGPU);
        }
        else
        {
            Log(L"Startup: SetMode(DGPU) failed");
            AddTrayIcon(GpuMode::Unknown);
            RefreshTrayIcon(GpuMode::Unknown);
        }
    }
    else
    {
        Log(L"Startup: NVAPI not available, cannot request DGPU");
        AddTrayIcon(GpuMode::Unknown);
        RefreshTrayIcon(GpuMode::Unknown);
    }

    SetTimer(g_hwnd, POLL_TIMER_ID, POLL_INTERVAL_MS, nullptr);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    KillTimer(g_hwnd, POLL_TIMER_ID);

    RemoveTrayIcon();
    DestroyIcons();

    if (hMutex)
    {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    if (g_log.is_open())
    {
        g_log << L"gpu_tray exiting\n";
        g_log.close();
    }

    return 0;
}

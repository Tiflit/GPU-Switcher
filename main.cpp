/*
 * gpu_tray — Lenovo Legion dGPU Display Switcher
 * - Uses ComPtr for COM (no leaks)
 * - Checks for Administrator privileges at startup
 * - Correct GPU detection (Intel = iGPU, NVIDIA/AMD = dGPU)
 * - Correct exit menu text
 * - Lightweight logging via OutputDebugStringW
 */

#include <Windows.h>
#include <shellapi.h>
#include <wbemidl.h>
#include <comdef.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <string>
#include <strsafe.h>

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "comsuppw.lib") // for _bstr_t

// ============================================================================
//  Lightweight logging
// ============================================================================

static void Log(const wchar_t* msg)
{
    OutputDebugStringW(L"[gpu_tray] ");
    OutputDebugStringW(msg);
    OutputDebugStringW(L"\n");
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
//  WMI configuration
// ============================================================================

#define GAMEZONE_WMI_NAMESPACE  L"ROOT\\WMI"
#define GAMEZONE_WMI_CLASS      L"LENOVO_GAMEZONE_DATA"
#define METHOD_GET_GPU_GPS      L"GetGpuGpsState"
#define METHOD_SET_GPU_GPS      L"SetGpuGpsState"

#define GPU_MODE_HYBRID   1u
#define GPU_MODE_DGPU     4u

struct WmiCtx {
    ComPtr<IWbemLocator>  pLocator;
    ComPtr<IWbemServices> pServices;
    bool ok = false;
};

static WmiCtx g_wmi;

static bool WmiInit()
{
    Log(L"WmiInit: starting");

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        Log(L"WmiInit: CoInitializeEx failed");
        return false;
    }

    hr = CoInitializeSecurity(
        nullptr, -1, nullptr, nullptr,
        RPC_C_AUTHN_LEVEL_DEFAULT,
        RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE, nullptr);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE)
    {
        Log(L"WmiInit: CoInitializeSecurity failed");
        return false;
    }

    hr = CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&g_wmi.pLocator));
    if (FAILED(hr))
    {
        Log(L"WmiInit: CoCreateInstance(IWbemLocator) failed");
        return false;
    }

    hr = g_wmi.pLocator->ConnectServer(
        _bstr_t(GAMEZONE_WMI_NAMESPACE),
        nullptr, nullptr, nullptr,
        0, nullptr, nullptr,
        &g_wmi.pServices);
    if (FAILED(hr))
    {
        Log(L"WmiInit: ConnectServer failed");
        return false;
    }

    hr = CoSetProxyBlanket(
        g_wmi.pServices.Get(),
        RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
        RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE);
    if (FAILED(hr))
    {
        Log(L"WmiInit: CoSetProxyBlanket failed");
        return false;
    }

    g_wmi.ok = true;
    Log(L"WmiInit: success");
    return true;
}

static void WmiShutdown()
{
    Log(L"WmiShutdown");
    g_wmi = {};
    CoUninitialize();
}

static HRESULT WmiCallGameZone(
    const wchar_t* method,
    const wchar_t* inParamName, DWORD inValue,
    const wchar_t* outParamName, DWORD* outValue)
{
    if (!g_wmi.ok) return E_NOT_VALID_STATE;

    ComPtr<IWbemClassObject> pClass;
    HRESULT hr = g_wmi.pServices->GetObject(
        _bstr_t(GAMEZONE_WMI_CLASS), 0, nullptr, &pClass, nullptr);
    if (FAILED(hr)) return hr;

    ComPtr<IEnumWbemClassObject> pEnum;
    hr = g_wmi.pServices->CreateInstanceEnum(
        _bstr_t(GAMEZONE_WMI_CLASS), 0, nullptr, &pEnum);
    if (FAILED(hr)) return hr;

    ComPtr<IWbemClassObject> pInst;
    ULONG uReturned = 0;
    hr = pEnum->Next(WBEM_INFINITE, 1, &pInst, &uReturned);
    if (FAILED(hr) || uReturned == 0) return E_NOINTERFACE;

    _variant_t vPath;
    hr = pInst->Get(L"__PATH", 0, &vPath, nullptr, nullptr);
    if (FAILED(hr)) return hr;

    ComPtr<IWbemClassObject> pInInst;
    if (inParamName)
    {
        ComPtr<IWbemClassObject> pInClass;
        hr = pClass->GetMethod(method, 0, &pInClass, nullptr);
        if (SUCCEEDED(hr))
        {
            pInClass->SpawnInstance(0, &pInInst);
            _variant_t v((long)inValue);
            pInInst->Put(inParamName, 0, &v, 0);
        }
    }

    ComPtr<IWbemClassObject> pOutInst;
    hr = g_wmi.pServices->ExecMethod(
        V_BSTR(&vPath), _bstr_t(method), 0, nullptr,
        pInInst.Get(), &pOutInst, nullptr);

    if (SUCCEEDED(hr) && outParamName && outValue && pOutInst)
    {
        _variant_t vOut;
        if (SUCCEEDED(pOutInst->Get(outParamName, 0, &vOut, nullptr, nullptr)))
            *outValue = (DWORD)V_I4(&vOut);
    }

    return hr;
}

// ============================================================================
//  GPU logic
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

        // Intel = iGPU, NVIDIA/AMD = dGPU
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
        // Fallback for systems where iGPU is hidden
        g_iGpu = g_dGpu;
        g_haveIGpu = true;
        Log(L"DetectGpus: iGPU hidden, using dGPU as fallback");
    }

    Log(L"DetectGpus: done");
}

static GpuMode QueryMode()
{
    DWORD val = 0;
    HRESULT hr = WmiCallGameZone(METHOD_GET_GPU_GPS, nullptr, 0, L"Data", &val);
    if (FAILED(hr))
    {
        Log(L"QueryMode: WmiCallGameZone failed");
        return GpuMode::Unknown;
    }
    return (val == GPU_MODE_DGPU) ? GpuMode::DGPU : GpuMode::Hybrid;
}

static bool SetMode(GpuMode target)
{
    DWORD val = (target == GpuMode::DGPU) ? GPU_MODE_DGPU : GPU_MODE_HYBRID;
    HRESULT hr = WmiCallGameZone(METHOD_SET_GPU_GPS, L"Data", val, nullptr, nullptr);
    if (FAILED(hr))
    {
        Log(L"SetMode: WmiCallGameZone failed");
        return false;
    }
    if (target == GpuMode::DGPU)
        Log(L"SetMode: requested DGPU");
    else
        Log(L"SetMode: requested HYBRID");
    return true;
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
    if (!g_wmi.ok)
        return L"GPU mode unknown — WMI unavailable";

    const GpuInfo* info = ActiveGpuInfo(mode);
    if (!info)
        return L"GPU mode unknown";

    std::wstring tip;
    if (mode == GpuMode::DGPU) tip = L"dGPU active — ";
    else if (mode == GpuMode::Hybrid) tip = L"Hybrid active — ";
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
                    L"GPU display mode changed");

    const GpuInfo* info = ActiveGpuInfo(mode);
    std::wstring msg;

    if (!info)
        msg = L"Display GPU changed.";
    else if (mode == GpuMode::DGPU)
        msg = L"Display is now driven by dGPU: " + info->name;
    else
        msg = L"Display is now driven by iGPU: " + info->name;

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
        GpuMode live = QueryMode();
        RefreshTrayIcon(live);
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
        // NOTIFYICON_VERSION_4 packs the mouse message in the low word of lParam
        UINT mouseMsg = LOWORD(lParam);

        if (mouseMsg == WM_CONTEXTMENU || mouseMsg == WM_RBUTTONUP)
        {
            GpuMode mode = QueryMode();
            RefreshTrayIcon(mode);

            const GpuInfo* info = ActiveGpuInfo(mode);
            std::wstring label;

            if (!g_wmi.ok)
                label = L"Active GPU: Unknown (WMI unavailable)";
            else if (!info)
                label = L"Active GPU: Unknown";
            else
            {
                label = L"Active GPU: ";
                label += info->name;
                if (mode == GpuMode::DGPU) label += L" (dGPU)";
                else if (mode == GpuMode::Hybrid) label += L" (iGPU display)";
            }

            std::wstring toggleText;
            if (mode == GpuMode::DGPU)
                toggleText = L"Switch to iGPU display mode";
            else if (mode == GpuMode::Hybrid)
                toggleText = L"Switch to dGPU display mode";
            else
                toggleText = L"Switch GPU display mode";

            std::wstring exitText = L"Exit and switch to iGPU display mode (";
            if (g_haveIGpu)
                exitText += g_iGpu.name;
            else
                exitText += L"iGPU";
            exitText += L")";

            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, label.c_str());
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, (UINT)MenuCmd::Toggle, toggleText.c_str());
            AppendMenuW(menu, MF_STRING, (UINT)MenuCmd::Exit,   exitText.c_str());

            SetForegroundWindow(hwnd);

            // Get current mouse position for modern tray behavior
            POINT pt;
            GetCursorPos(&pt);

            int cmd = TrackPopupMenu(menu,
                                     TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                     pt.x, pt.y, 0, hwnd, nullptr);
            PostMessage(hwnd, WM_NULL, 0, 0);
            DestroyMenu(menu);

            if (cmd == (int)MenuCmd::Toggle)
            {
                if (!g_wmi.ok) return 0;

                GpuMode current = QueryMode();
                GpuMode target =
                    (current == GpuMode::DGPU) ? GpuMode::Hybrid : GpuMode::DGPU;

                if (SetMode(target))
                {
                    Sleep(1500);
                    RefreshTrayIcon(QueryMode());
                }
            }
            else if (cmd == (int)MenuCmd::Exit)
            {
                if (g_wmi.ok)
                {
                    SetMode(GpuMode::Hybrid);
                    Sleep(1500);
                }
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
    if (!IsAdmin())
    {
        MessageBoxW(nullptr,
                    L"This tool must be run as Administrator.\n\n"
                    L"Right-click the .exe and choose \"Run as administrator\".",
                    L"GPU Tray Switcher",
                    MB_ICONERROR | MB_OK);
        return 1;
    }

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"LenovoGpuTrayMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    if (!WmiInit())
    {
        MessageBoxW(nullptr,
                    L"Lenovo WMI interface (LENOVO_GAMEZONE_DATA) not found.\n"
                    L"This tool only works on supported Lenovo systems.",
                    L"GPU Tray Switcher",
                    MB_ICONERROR | MB_OK);
        if (hMutex)
        {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        return 1;
    }

    DetectGpus();
    CreateIcons();

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"GpuTrayClass";
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
        WmiShutdown();
        if (hMutex)
        {
            ReleaseMutex(hMutex);
            CloseHandle(hMutex);
        }
        return 1;
    }

    ShowWindow(g_hwnd, SW_HIDE);

    // Initial sync: prefer dGPU on launch
    GpuMode initial = QueryMode();
    if (initial != GpuMode::DGPU && g_wmi.ok)
    {
        if (SetMode(GpuMode::DGPU))
            Sleep(1500);
    }

    g_currentMode = GpuMode::Unknown;
    GpuMode live = QueryMode();
    AddTrayIcon(live);
    RefreshTrayIcon(live);

    SetTimer(g_hwnd, POLL_TIMER_ID, POLL_INTERVAL_MS, nullptr);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    KillTimer(g_hwnd, POLL_TIMER_ID);

    if (g_wmi.ok)
    {
        SetMode(GpuMode::Hybrid);
        Sleep(1500);
    }

    RemoveTrayIcon();
    DestroyIcons();
    WmiShutdown();

    if (hMutex)
    {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return 0;
}

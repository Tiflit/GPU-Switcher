/*
 * gpu_tray — Lenovo Legion dGPU Tray Switcher
 * Targets: Lenovo Legion / IdeaPad Gaming laptops with GPU Working Mode support
 * Uses:    LENOVO_GAMEZONE_DATA WMI (methods 5/6: Get/SetGpuGpsState)
 *
 * GPU Working Mode values:
 *   1 = Hybrid      (iGPU display, dGPU on-demand — best battery)
 *   2 = HybridIGPU  (dGPU fully disconnected)
 *   3 = HybridAuto  (auto-switch on AC/battery)
 *   4 = dGPU        (dGPU drives display directly — best performance)
 *
 * Behaviour:
 *   Launch  -> set mode 4 (dGPU)
 *   Exit    -> set mode 1 (Hybrid / iGPU display)
 *   Right-click:
 *      - Shows active GPU + model (iGPU/dGPU)
 *      - Offers "Switch to iGPU display mode" or "Switch to dGPU display mode"
 *        depending on current mode
 *      - Offers "Exit and switch to iGPU display mode (Vendor dGPU)"
 *
 * Tray icon:
 *   - Letter indicates GPU type and vendor:
 *       i / I = Intel (blue)
 *       n / N = NVIDIA (green)
 *       a / A = AMD (red)
 *     Lowercase = iGPU, uppercase = dGPU
 *   - Grey '?' when unsupported / WMI unavailable
 */

#include <Windows.h>
#include <shellapi.h>
#include <wbemidl.h>
#include <comdef.h>
#include <dxgi1_6.h>
#include <string>
#include <strsafe.h>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "dxgi.lib")

// ── WMI / LENOVO_GAMEZONE_DATA ───────────────────────────────────────────────

#define GAMEZONE_WMI_NAMESPACE  L"ROOT\\WMI"
#define GAMEZONE_WMI_CLASS      L"LENOVO_GAMEZONE_DATA"
#define METHOD_GET_GPU_GPS      L"GetGpuGpsState"   // WmiMethodId 5
#define METHOD_SET_GPU_GPS      L"SetGpuGpsState"   // WmiMethodId 6

#define GPU_MODE_HYBRID   1u   // Optimus: iGPU display, dGPU on-demand
#define GPU_MODE_DGPU     4u   // dGPU drives display directly

struct WmiCtx {
    IWbemLocator*  pLocator  = nullptr;
    IWbemServices* pServices = nullptr;
    bool           ok        = false;
};

static WmiCtx g_wmi;

static bool WmiInit()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

    hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr,
                              RPC_C_AUTHN_LEVEL_DEFAULT,
                              RPC_C_IMP_LEVEL_IMPERSONATE,
                              nullptr, EOAC_NONE, nullptr);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE)
    {
        CoUninitialize();
        return false;
    }

    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator, (void**)&g_wmi.pLocator);
    if (FAILED(hr))
    {
        CoUninitialize();
        return false;
    }

    hr = g_wmi.pLocator->ConnectServer(
            _bstr_t(GAMEZONE_WMI_NAMESPACE), nullptr, nullptr, nullptr,
            0, nullptr, nullptr, &g_wmi.pServices);
    if (FAILED(hr))
    {
        g_wmi.pLocator->Release();
        g_wmi.pLocator = nullptr;
        CoUninitialize();
        return false;
    }

    hr = CoSetProxyBlanket(g_wmi.pServices,
                           RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                           RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                           nullptr, EOAC_NONE);
    if (FAILED(hr))
    {
        g_wmi.pServices->Release();
        g_wmi.pServices = nullptr;
        g_wmi.pLocator->Release();
        g_wmi.pLocator = nullptr;
        CoUninitialize();
        return false;
    }

    g_wmi.ok = true;
    return true;
}

static void WmiShutdown()
{
    if (g_wmi.pServices)
    {
        g_wmi.pServices->Release();
        g_wmi.pServices = nullptr;
    }
    if (g_wmi.pLocator)
    {
        g_wmi.pLocator->Release();
        g_wmi.pLocator = nullptr;
    }
    if (g_wmi.ok)
    {
        CoUninitialize();
    }
    g_wmi.ok = false;
}

// Execute a LENOVO_GAMEZONE_DATA WMI method.
// Pass inParamName=nullptr to call with no input params (Get methods).
// Pass outParamName=nullptr to ignore output (Set methods).
static HRESULT WmiCallGameZone(
    const wchar_t* method,
    const wchar_t* inParamName,  DWORD  inValue,
    const wchar_t* outParamName, DWORD* outValue)
{
    if (!g_wmi.ok) return E_NOT_VALID_STATE;

    IWbemClassObject* pClass   = nullptr;
    IWbemClassObject* pInClass = nullptr;
    IWbemClassObject* pInInst  = nullptr;
    IWbemClassObject* pOutInst = nullptr;

    HRESULT hr = g_wmi.pServices->GetObject(
        _bstr_t(GAMEZONE_WMI_CLASS), 0, nullptr, &pClass, nullptr);
    if (FAILED(hr)) return hr;

    // Find first instance path
    IEnumWbemClassObject* pEnum = nullptr;
    hr = g_wmi.pServices->CreateInstanceEnum(
        _bstr_t(GAMEZONE_WMI_CLASS), 0, nullptr, &pEnum);
    if (FAILED(hr))
    {
        pClass->Release();
        return hr;
    }

    IWbemClassObject* pInstance = nullptr;
    ULONG uReturned = 0;
    hr = pEnum->Next(WBEM_INFINITE, 1, &pInstance, &uReturned);
    pEnum->Release();
    if (FAILED(hr) || uReturned == 0)
    {
        pClass->Release();
        return E_NOINTERFACE;
    }

    VARIANT vPath;
    VariantInit(&vPath);
    hr = pInstance->Get(L"__PATH", 0, &vPath, nullptr, nullptr);
    pInstance->Release();
    if (FAILED(hr))
    {
        VariantClear(&vPath);
        pClass->Release();
        return hr;
    }

    // Build input parameters if needed
    if (inParamName)
    {
        hr = pClass->GetMethod(method, 0, &pInClass, nullptr);
        if (SUCCEEDED(hr) && pInClass)
        {
            pInClass->SpawnInstance(0, &pInInst);
            VARIANT vIn;
            VariantInit(&vIn);
            vIn.vt   = VT_I4;
            vIn.lVal = (LONG)inValue;
            pInInst->Put(inParamName, 0, &vIn, 0);
            VariantClear(&vIn);
        }
    }

    hr = g_wmi.pServices->ExecMethod(
        V_BSTR(&vPath), _bstr_t(method), 0, nullptr,
        pInInst, &pOutInst, nullptr);

    if (SUCCEEDED(hr) && outParamName && outValue && pOutInst)
    {
        VARIANT vOut;
        VariantInit(&vOut);
        if (SUCCEEDED(pOutInst->Get(outParamName, 0, &vOut, nullptr, nullptr)))
        {
            *outValue = (DWORD)V_I4(&vOut);
            VariantClear(&vOut);
        }
    }

    VariantClear(&vPath);
    if (pInInst)  pInInst->Release();
    if (pInClass) pInClass->Release();
    if (pOutInst) pOutInst->Release();
    pClass->Release();
    return hr;
}

// ── GPU state ────────────────────────────────────────────────────────────────

enum class GpuMode { Unknown, Hybrid, DGPU };
static GpuMode g_currentMode = GpuMode::Unknown;

static GpuMode QueryMode()
{
    DWORD val = 0;
    HRESULT hr = WmiCallGameZone(METHOD_GET_GPU_GPS, nullptr, 0, L"Data", &val);
    if (FAILED(hr)) return GpuMode::Unknown;
    return (val == GPU_MODE_DGPU) ? GpuMode::DGPU : GpuMode::Hybrid;
}

static bool SetMode(GpuMode target)
{
    DWORD val = (target == GpuMode::DGPU) ? GPU_MODE_DGPU : GPU_MODE_HYBRID;
    HRESULT hr = WmiCallGameZone(METHOD_SET_GPU_GPS, L"Data", val, nullptr, nullptr);
    return SUCCEEDED(hr);
}

// ── DXGI GPU detection ───────────────────────────────────────────────────────

struct GpuInfo
{
    std::wstring name;
    UINT         vendorId   = 0;
    bool         isIntegrated = false;
};

static GpuInfo g_iGpu;
static GpuInfo g_dGpu;
static bool    g_haveIGpu = false;
static bool    g_haveDGpu = false;

static void DetectGpus()
{
    IDXGIFactory1* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory)))
        return;

    for (UINT i = 0; ; ++i)
    {
        IDXGIAdapter1* pAdapter = nullptr;
        HRESULT hr = pFactory->EnumAdapters1(i, &pAdapter);
        if (hr == DXGI_ERROR_NOT_FOUND)
            break;
        if (FAILED(hr) || !pAdapter)
            continue;

        DXGI_ADAPTER_DESC1 desc;
        if (FAILED(pAdapter->GetDesc1(&desc)))
        {
            pAdapter->Release();
            continue;
        }

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
        {
            pAdapter->Release();
            continue;
        }

        bool isIntegrated = (desc.DedicatedVideoMemory == 0);

        GpuInfo info;
        info.vendorId     = desc.VendorId;
        info.isIntegrated = isIntegrated;
        info.name         = desc.Description;

        if (isIntegrated && !g_haveIGpu)
        {
            g_iGpu = info;
            g_haveIGpu = true;
        }
        else if (!isIntegrated && !g_haveDGpu)
        {
            g_dGpu = info;
            g_haveDGpu = true;
        }

        pAdapter->Release();
    }

    pFactory->Release();
}

static const GpuInfo* ActiveGpuInfo(GpuMode mode)
{
    if (mode == GpuMode::DGPU && g_haveDGpu)
        return &g_dGpu;
    if (mode == GpuMode::Hybrid && g_haveIGpu)
        return &g_iGpu;
    return nullptr;
}

// ── Tray icon rendering ──────────────────────────────────────────────────────
// DPI-aware small icon: filled circle + centred bold letter.
// Colors:
//   Intel   -> blue
//   NVIDIA -> green
//   AMD    -> red
//   Unknown -> grey

static HICON MakeLetterIcon(WCHAR letter, COLORREF bg, COLORREF fg, int size)
{
    const int SZ = (size > 0) ? size : 16;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem    = CreateCompatibleDC(hdcScreen);

    BITMAPINFOHEADER bih = {};
    bih.biSize        = sizeof(bih);
    bih.biWidth       = SZ;
    bih.biHeight      = -SZ;
    bih.biPlanes      = 1;
    bih.biBitCount    = 32;
    bih.biCompression = BI_RGB;

    BITMAPINFO bi = {};
    bi.bmiHeader  = bih;

    void*   bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBmp);

    RECT rc = { 0, 0, SZ, SZ };
    HBRUSH hBgBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdcMem, &rc, hBgBrush);
    DeleteObject(hBgBrush);

    HBRUSH hBrush = CreateSolidBrush(bg);
    HPEN   hPen   = CreatePen(PS_SOLID, 1, bg);
    HPEN   hOldP  = (HPEN)SelectObject(hdcMem, hPen);
    HBRUSH hOldB  = (HBRUSH)SelectObject(hdcMem, hBrush);
    Ellipse(hdcMem, 1, 1, SZ - 1, SZ - 1);
    SelectObject(hdcMem, hOldP);
    SelectObject(hdcMem, hOldB);
    DeleteObject(hBrush);
    DeleteObject(hPen);

    int fontHeight = SZ - 3;
    if (fontHeight < 8) fontHeight = 8;

    HFONT hFont = CreateFontW(
        fontHeight, 0, 0, 0, FW_HEAVY, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_SWISS,
        L"Segoe UI"
    );
    HFONT hOldF = (HFONT)SelectObject(hdcMem, hFont);
    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, fg);
    WCHAR s[2] = { letter, 0 };
    DrawTextW(hdcMem, s, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdcMem, hOldF);
    DeleteObject(hFont);

    HDC     hdcMask = CreateCompatibleDC(hdcScreen);
    HBITMAP hMask   = CreateBitmap(SZ, SZ, 1, 1, nullptr);
    HBITMAP hOldM   = (HBITMAP)SelectObject(hdcMask, hMask);
    SetBkColor(hdcMem, RGB(0, 0, 0));
    BitBlt(hdcMask, 0, 0, SZ, SZ, hdcMem, 0, 0, SRCCOPY);
    SelectObject(hdcMask, hOldM);
    SelectObject(hdcMem, hOld);

    ICONINFO ii = {};
    ii.fIcon    = TRUE;
    ii.hbmColor = hBmp;
    ii.hbmMask  = hMask;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hBmp);
    DeleteObject(hMask);
    DeleteDC(hdcMem);
    DeleteDC(hdcMask);
    ReleaseDC(nullptr, hdcScreen);
    return hIcon;
}

static HICON g_hIconUnknown   = nullptr;
static HICON g_hIconIntel_i   = nullptr;
static HICON g_hIconIntel_I   = nullptr;
static HICON g_hIconNvidia_n  = nullptr;
static HICON g_hIconNvidia_N  = nullptr;
static HICON g_hIconAmd_a     = nullptr;
static HICON g_hIconAmd_A     = nullptr;

static void CreateIcons()
{
    int sz = GetSystemMetrics(SM_CXSMICON);
    if (sz <= 0) sz = 16;

    g_hIconUnknown  = MakeLetterIcon(L'?', RGB(136, 136, 136), RGB(255, 255, 255), sz);

    // Intel: blue
    g_hIconIntel_i  = MakeLetterIcon(L'i', RGB( 91, 141, 184), RGB(255, 255, 255), sz);
    g_hIconIntel_I  = MakeLetterIcon(L'I', RGB( 91, 141, 184), RGB(255, 255, 255), sz);

    // NVIDIA: green
    g_hIconNvidia_n = MakeLetterIcon(L'n', RGB( 90, 160, 106), RGB(255, 255, 255), sz);
    g_hIconNvidia_N = MakeLetterIcon(L'N', RGB( 90, 160, 106), RGB(255, 255, 255), sz);

    // AMD: red
    g_hIconAmd_a    = MakeLetterIcon(L'a', RGB(200,  80,  80), RGB(255, 255, 255), sz);
    g_hIconAmd_A    = MakeLetterIcon(L'A', RGB(200,  80,  80), RGB(255, 255, 255), sz);
}

static void DestroyIcons()
{
    if (g_hIconUnknown)   { DestroyIcon(g_hIconUnknown);   g_hIconUnknown   = nullptr; }
    if (g_hIconIntel_i)   { DestroyIcon(g_hIconIntel_i);   g_hIconIntel_i   = nullptr; }
    if (g_hIconIntel_I)   { DestroyIcon(g_hIconIntel_I);   g_hIconIntel_I   = nullptr; }
    if (g_hIconNvidia_n)  { DestroyIcon(g_hIconNvidia_n);  g_hIconNvidia_n  = nullptr; }
    if (g_hIconNvidia_N)  { DestroyIcon(g_hIconNvidia_N);  g_hIconNvidia_N  = nullptr; }
    if (g_hIconAmd_a)     { DestroyIcon(g_hIconAmd_a);     g_hIconAmd_a     = nullptr; }
    if (g_hIconAmd_A)     { DestroyIcon(g_hIconAmd_A);     g_hIconAmd_A     = nullptr; }
}

static HICON ModeIcon(GpuMode mode)
{
    const GpuInfo* info = ActiveGpuInfo(mode);
    if (!info) return g_hIconUnknown;

    UINT v = info->vendorId;
    bool integrated = info->isIntegrated;

    // Intel
    if (v == 0x8086)
        return integrated ? g_hIconIntel_i : g_hIconIntel_I;

    // NVIDIA
    if (v == 0x10DE)
        return integrated ? g_hIconNvidia_n : g_hIconNvidia_N;

    // AMD (0x1002 / 0x1022)
    if (v == 0x1002 || v == 0x1022)
        return integrated ? g_hIconAmd_a : g_hIconAmd_A;

    return g_hIconUnknown;
}

static std::wstring ModeTooltip(GpuMode mode)
{
    if (!g_wmi.ok)
        return L"GPU mode unknown \u2014 WMI unavailable (Lenovo-only interface)";

    const GpuInfo* info = ActiveGpuInfo(mode);
    if (!info)
        return L"GPU mode unknown \u2014 no GPU information available";

    std::wstring tip;
    if (mode == GpuMode::DGPU)
        tip = L"dGPU active \u2014 ";
    else if (mode == GpuMode::Hybrid)
        tip = L"Hybrid active \u2014 ";
    else
        tip = L"GPU mode unknown \u2014 ";

    tip += info->name;
    if (mode == GpuMode::DGPU)
        tip += L" (dGPU)";
    else if (mode == GpuMode::Hybrid)
        tip += L" (iGPU display)";

    return tip;
}

// ── Tray management ──────────────────────────────────────────────────────────

#define WM_TRAY          (WM_USER + 1)
#define TRAY_ID          1
#define POLL_TIMER_ID    1
#define POLL_INTERVAL_MS 5000            // re-query GPU mode every 5 seconds

static HWND      g_hwnd  = nullptr;
static HINSTANCE g_hInst = nullptr;

static void AddTrayIcon(GpuMode mode)
{
    std::wstring tip = ModeTooltip(mode);
    NOTIFYICONDATA nid   = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = g_hwnd;
    nid.uID              = TRAY_ID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon            = ModeIcon(mode);
    StringCchCopyW(nid.szTip, ARRAYSIZE(nid.szTip), tip.c_str());
    Shell_NotifyIconW(NIM_ADD, &nid);

    // Enable modern behavior (better DPI handling, tooltips, etc.)
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

// Updates the tray icon only if the mode has actually changed — avoids
// unnecessary flicker from constant NIM_MODIFY calls.
static void RefreshTrayIcon(GpuMode newMode)
{
    if (newMode == g_currentMode) return;
    g_currentMode = newMode;

    std::wstring tip = ModeTooltip(newMode);
    NOTIFYICONDATA nid = {};
    nid.cbSize         = sizeof(nid);
    nid.hWnd           = g_hwnd;
    nid.uID            = TRAY_ID;
    nid.uFlags         = NIF_ICON | NIF_TIP;
    nid.hIcon          = ModeIcon(newMode);
    StringCchCopyW(nid.szTip, ARRAYSIZE(nid.szTip), tip.c_str());
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void RemoveTrayIcon()
{
    NOTIFYICONDATA nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = g_hwnd;
    nid.uID    = TRAY_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

// ── Window procedure ─────────────────────────────────────────────────────────

#define CMD_TOGGLE 1
#define CMD_EXIT   2

static std::wstring DgpuVendorLabel()
{
    if (!g_haveDGpu)
        return L"dGPU";

    const GpuInfo& d = g_dGpu;
    // Use vendor name only if we can infer it
    if (d.vendorId == 0x8086) return L"Intel dGPU";
    if (d.vendorId == 0x10DE) return L"NVIDIA dGPU";
    if (d.vendorId == 0x1002 || d.vendorId == 0x1022) return L"AMD dGPU";
    return L"dGPU";
}

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
        // DPI changed (e.g. tray moved between monitors) — regenerate icons
        DestroyIcons();
        CreateIcons();
        RefreshTrayIcon(g_currentMode);
        return 0;
    }

    if (msg == WM_TRAY && lParam == WM_RBUTTONUP)
    {
        // Always show the real current state in the menu label
        GpuMode mode = QueryMode();
        RefreshTrayIcon(mode);

        const GpuInfo* info = ActiveGpuInfo(mode);
        std::wstring label;

        if (!g_wmi.ok)
        {
            label = L"Active GPU: Unknown (WMI unavailable)";
        }
        else if (!info)
        {
            label = L"Active GPU: Unknown (no GPU info)";
        }
        else
        {
            label = L"Active GPU: ";
            label += info->name;
            if (mode == GpuMode::DGPU)
                label += L" (dGPU)";
            else if (mode == GpuMode::Hybrid)
                label += L" (iGPU display)";
        }

        std::wstring toggleText;
        if (mode == GpuMode::DGPU)
            toggleText = L"Switch to iGPU display mode";
        else if (mode == GpuMode::Hybrid)
            toggleText = L"Switch to dGPU display mode";
        else
            toggleText = L"Switch GPU display mode";

        std::wstring exitText = L"Exit and switch to iGPU display mode (";
        exitText += DgpuVendorLabel();
        exitText += L")";

        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, label.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CMD_TOGGLE, toggleText.c_str());
        AppendMenuW(menu, MF_STRING, CMD_EXIT,   exitText.c_str());

        SetForegroundWindow(hwnd);
        POINT pt;
        GetCursorPos(&pt);
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                 pt.x, pt.y, 0, hwnd, nullptr);
        PostMessage(hwnd, WM_NULL, 0, 0);
        DestroyMenu(menu);

        if (cmd == CMD_TOGGLE)
        {
            if (!g_wmi.ok)
                return 0;

            GpuMode current = QueryMode();
            if (current == GpuMode::DGPU)
            {
                if (SetMode(GpuMode::Hybrid))
                {
                    Sleep(1500);
                    RefreshTrayIcon(QueryMode());
                }
            }
            else if (current == GpuMode::Hybrid)
            {
                if (SetMode(GpuMode::DGPU))
                {
                    Sleep(1500);
                    RefreshTrayIcon(QueryMode());
                }
            }
        }
        else if (cmd == CMD_EXIT)
        {
            if (g_wmi.ok)
            {
                // Ensure we leave the system in iGPU display mode
                SetMode(GpuMode::Hybrid);
                Sleep(1500);
            }
            PostQuitMessage(0);
        }

        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ── Entry point ──────────────────────────────────────────────────────────────

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hInst = hInst;

    // Single-instance guard
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"GpuTraySwitcher_Mutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(nullptr,
            L"GPU Tray Switcher is already running.",
            L"Already Running", MB_OK | MB_ICONINFORMATION);
        return 1;
    }

    // Message-only window
    WNDCLASSW wc     = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"GpuTrayClass";
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"",
                             0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, hInst, nullptr);

    // Detect GPUs (for tooltips/icons) and create icons (DPI-aware)
    DetectGpus();
    CreateIcons();

    bool wmiOk = WmiInit();
    if (!wmiOk)
    {
        AddTrayIcon(GpuMode::Unknown);
        MessageBoxW(nullptr,
            L"GPU switching is not available on this device.\n\n"
            L"This tool requires the Lenovo-specific WMI class:\n"
            L"  LENOVO_GAMEZONE_DATA\n\n"
            L"Your system does not expose this interface.\n"
            L"The tray icon will show status only.",
            L"GPU Tray Switcher \u2014 WMI Error",
            MB_OK | MB_ICONWARNING);
    }
    else
    {
        bool switched = SetMode(GpuMode::DGPU);
        // Wait for the EC to apply the MUX change before first query
        Sleep(1500);
        g_currentMode = QueryMode();
        AddTrayIcon(g_currentMode);

        if (!switched)
        {
            MessageBoxW(nullptr,
                L"The GPU mode could not be changed.\n\n"
                L"Possible causes:\n"
                L"\u2022 Administrator privileges are required\n"
                L"\u2022 Lenovo Vantage / Legion Zone is blocking access\n"
                L"\u2022 The EC is busy or switching modes\n\n"
                L"Your GPU mode may not have been changed.",
                L"GPU Tray Switcher \u2014 Switch Error",
                MB_OK | MB_ICONWARNING);
        }

        // Start live-poll timer: refreshes icon every 5 s with minimal overhead
        SetTimer(g_hwnd, POLL_TIMER_ID, POLL_INTERVAL_MS, nullptr);
    }

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup: ensure iGPU display mode on exit, then clean up
    KillTimer(g_hwnd, POLL_TIMER_ID);
    if (wmiOk)
    {
        SetMode(GpuMode::Hybrid);
        Sleep(1500);
    }
    RemoveTrayIcon();
    WmiShutdown();
    DestroyIcons();

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return 0;
}

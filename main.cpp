// -------------- FULL main.cpp CONTENT BELOW --------------
// (This file replaces your existing main.cpp completely)

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

// ============================================================================
//  Lenovo WMI Interface
// ============================================================================

#define GAMEZONE_WMI_NAMESPACE  L"ROOT\\WMI"
#define GAMEZONE_WMI_CLASS      L"LENOVO_GAMEZONE_DATA"
#define METHOD_GET_GPU_GPS      L"GetGpuGpsState"
#define METHOD_SET_GPU_GPS      L"SetGpuGpsState"

#define GPU_MODE_HYBRID   1u
#define GPU_MODE_DGPU     4u

struct WmiCtx {
    IWbemLocator*  pLocator  = nullptr;
    IWbemServices* pServices = nullptr;
    bool ok = false;
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
    if (g_wmi.pServices) g_wmi.pServices->Release();
    if (g_wmi.pLocator)  g_wmi.pLocator->Release();
    if (g_wmi.ok)        CoUninitialize();
    g_wmi = {};
}

static HRESULT WmiCallGameZone(
    const wchar_t* method,
    const wchar_t* inParamName, DWORD inValue,
    const wchar_t* outParamName, DWORD* outValue)
{
    if (!g_wmi.ok) return E_NOT_VALID_STATE;

    IWbemClassObject* pClass = nullptr;
    HRESULT hr = g_wmi.pServices->GetObject(
        _bstr_t(GAMEZONE_WMI_CLASS), 0, nullptr, &pClass, nullptr);
    if (FAILED(hr)) return hr;

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

    IWbemClassObject* pInClass = nullptr;
    IWbemClassObject* pInInst  = nullptr;
    if (inParamName)
    {
        hr = pClass->GetMethod(method, 0, &pInClass, nullptr);
        if (SUCCEEDED(hr))
        {
            pInClass->SpawnInstance(0, &pInInst);
            VARIANT v;
            VariantInit(&v);
            v.vt = VT_I4;
            v.lVal = (LONG)inValue;
            pInInst->Put(inParamName, 0, &v, 0);
            VariantClear(&v);
        }
    }

    IWbemClassObject* pOutInst = nullptr;
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

    if (pInInst)  pInInst->Release();
    if (pInClass) pInClass->Release();
    if (pOutInst) pOutInst->Release();
    VariantClear(&vPath);
    pClass->Release();
    return hr;
}

// ============================================================================
//  GPU Mode
// ============================================================================

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

// ============================================================================
//  DXGI GPU Detection
// ============================================================================

struct GpuInfo {
    std::wstring name;
    UINT vendorId = 0;
    bool isIntegrated = false;
};

static GpuInfo g_iGpu;
static GpuInfo g_dGpu;
static bool g_haveIGpu = false;
static bool g_haveDGpu = false;

static void DetectGpus()
{
    IDXGIFactory1* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory)))
        return;

    for (UINT i = 0;; ++i)
    {
        IDXGIAdapter1* pAdapter = nullptr;
        HRESULT hr = pFactory->EnumAdapters1(i, &pAdapter);
        if (hr == DXGI_ERROR_NOT_FOUND) break;
        if (FAILED(hr) || !pAdapter) continue;

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

        bool integrated = (desc.DedicatedVideoMemory == 0);

        GpuInfo info;
        info.vendorId = desc.VendorId;
        info.isIntegrated = integrated;
        info.name = desc.Description;

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

        pAdapter->Release();
    }

    pFactory->Release();
}

static const GpuInfo* ActiveGpuInfo(GpuMode mode)
{
    if (mode == GpuMode::DGPU && g_haveDGpu) return &g_dGpu;
    if (mode == GpuMode::Hybrid && g_haveIGpu) return &g_iGpu;
    return nullptr;
}

// ============================================================================
//  Icon Rendering
// ============================================================================

static HICON MakeLetterIcon(WCHAR letter, COLORREF bg, COLORREF fg, int size)
{
    const int SZ = (size > 0) ? size : 16;

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
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBmp);

    RECT rc = { 0,0,SZ,SZ };
    HBRUSH hBg = CreateSolidBrush(RGB(0,0,0));
    FillRect(hdcMem, &rc, hBg);
    DeleteObject(hBg);

    HBRUSH hBrush = CreateSolidBrush(bg);
    HPEN hPen = CreatePen(PS_SOLID, 1, bg);
    HPEN hOldP = (HPEN)SelectObject(hdcMem, hPen);
    HBRUSH hOldB = (HBRUSH)SelectObject(hdcMem, hBrush);
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

    g_hIconUnknown = MakeLetterIcon(L'?', RGB(136,136,136), RGB(255,255,255), sz);

    g_hIconIntel_i = MakeLetterIcon(L'i', RGB(91,141,184), RGB(255,255,255), sz);
    g_hIconIntel_I = MakeLetterIcon(L'I', RGB(91,141,184), RGB(255,255,255), sz);

    g_hIconNvidia_n = MakeLetterIcon(L'n', RGB(90,160,106), RGB(255,255,255), sz);
    g_hIconNvidia_N = MakeLetterIcon(L'N', RGB(90,160,106), RGB(255,255,255), sz);

    g_hIconAmd_a = MakeLetterIcon(L'a', RGB(200,80,80), RGB(255,255,255), sz);
    g_hIconAmd_A = MakeLetterIcon(L'A', RGB(200,80,80), RGB(255,255,255), sz);
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
//  Tray Icon + Notifications
// ============================================================================

#define WM_TRAY (WM_USER + 1)
#define TRAY_ID 1
#define POLL_TIMER_ID 1
#define POLL_INTERVAL_MS 5000

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


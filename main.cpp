/*
 * gpu_tray — Lenovo Legion dGPU Tray Switcher
 * Targets: Lenovo Legion / IdeaPad Gaming laptops with GPU Working Mode support
 * Uses:    LENOVO_GAMEZONE_DATA WMI (methods 5/6: Get/SetGpuGpsState)
 *
 * GPU Working Mode values:
 * 1 = Hybrid      (iGPU display, dGPU on-demand — best battery)
 * 2 = HybridIGPU  (dGPU fully disconnected)
 * 3 = HybridAuto  (auto-switch on AC/battery)
 * 4 = dGPU        (dGPU drives display directly — best performance)
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

// ── WMI Configuration ────────────────────────────────────────────────────────

#define GAMEZONE_WMI_NAMESPACE  L"ROOT\\WMI"
#define GAMEZONE_WMI_CLASS      L"LENOVO_GAMEZONE_DATA"
#define METHOD_GET_GPU_GPS      L"GetGpuGpsState"
#define METHOD_SET_GPU_GPS      L"SetGpuGpsState"

#define GPU_MODE_HYBRID   1u
#define GPU_MODE_DGPU     4u

struct WmiCtx {
    IWbemLocator* pLocator = nullptr;
    IWbemServices* pServices = nullptr;
    bool ok = false;
};

static WmiCtx g_wmi;

static bool WmiInit() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return false;

    hr = CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                              RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    
    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (void**)&g_wmi.pLocator);
    if (FAILED(hr)) return false;

    hr = g_wmi.pLocator->ConnectServer(_bstr_t(GAMEZONE_WMI_NAMESPACE), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &g_wmi.pServices);
    if (FAILED(hr)) return false;

    CoSetProxyBlanket(g_wmi.pServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                      RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    g_wmi.ok = true;
    return true;
}

static void WmiShutdown() {
    if (g_wmi.pServices) g_wmi.pServices->Release();
    if (g_wmi.pLocator) g_wmi.pLocator->Release();
    if (g_wmi.ok) CoUninitialize();
}

static HRESULT WmiCallGameZone(const wchar_t* method, const wchar_t* inParam, DWORD inVal, const wchar_t* outParam, DWORD* outVal) {
    if (!g_wmi.ok) return E_NOT_VALID_STATE;
    IWbemClassObject *pClass = nullptr, *pInInst = nullptr, *pOutInst = nullptr;
    
    HRESULT hr = g_wmi.pServices->GetObject(_bstr_t(GAMEZONE_WMI_CLASS), 0, nullptr, &pClass, nullptr);
    if (FAILED(hr)) return hr;

    IEnumWbemClassObject* pEnum = nullptr;
    g_wmi.pServices->CreateInstanceEnum(_bstr_t(GAMEZONE_WMI_CLASS), 0, nullptr, &pEnum);
    IWbemClassObject* pInst = nullptr;
    ULONG uRet = 0;
    pEnum->Next(WBEM_INFINITE, 1, &pInst, &uRet);
    pEnum->Release();

    VARIANT vPath; VariantInit(&vPath);
    pInst->Get(L"__PATH", 0, &vPath, nullptr, nullptr);
    pInst->Release();

    if (inParam) {
        IWbemClassObject* pInClass = nullptr;
        pClass->GetMethod(method, 0, &pInClass, nullptr);
        pInClass->SpawnInstance(0, &pInInst);
        VARIANT v; v.vt = VT_I4; v.lVal = (LONG)inVal;
        pInInst->Put(inParam, 0, &v, 0);
        pInClass->Release();
    }

    hr = g_wmi.pServices->ExecMethod(V_BSTR(&vPath), _bstr_t(method), 0, nullptr, pInInst, &pOutInst, nullptr);

    if (SUCCEEDED(hr) && outParam && outVal && pOutInst) {
        VARIANT vOut; VariantInit(&vOut);
        if (SUCCEEDED(pOutInst->Get(outParam, 0, &vOut, nullptr, nullptr))) *outVal = (DWORD)V_I4(&vOut);
    }

    if (pInInst) pInInst->Release();
    if (pOutInst) pOutInst->Release();
    pClass->Release();
    VariantClear(&vPath);
    return hr;
}

// ── GPU Logic ───────────────────────────────────────────────────────────────

enum class GpuMode { Unknown, Hybrid, DGPU };
static GpuMode g_currentMode = GpuMode::Unknown;

struct GpuInfo { std::wstring name; UINT vendorId = 0; bool isIntegrated = false; };
static GpuInfo g_iGpu, g_dGpu;
static bool g_haveIGpu = false, g_haveDGpu = false;

static GpuMode QueryMode() {
    DWORD val = 0;
    if (FAILED(WmiCallGameZone(METHOD_GET_GPU_GPS, nullptr, 0, L"Data", &val))) return GpuMode::Unknown;
    return (val == GPU_MODE_DGPU) ? GpuMode::DGPU : GpuMode::Hybrid;
}

static bool SetMode(GpuMode target) {
    DWORD val = (target == GpuMode::DGPU) ? GPU_MODE_DGPU : GPU_MODE_HYBRID;
    return SUCCEEDED(WmiCallGameZone(METHOD_SET_GPU_GPS, L"Data", val, nullptr, nullptr));
}

static void DetectGpus() {
    IDXGIFactory1* pFactory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&pFactory))) return;
    for (UINT i = 0; ; ++i) {
        IDXGIAdapter1* pAdapter = nullptr;
        if (pFactory->EnumAdapters1(i, &pAdapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 desc;
        pAdapter->GetDesc1(&desc);
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
            bool integrated = (desc.DedicatedVideoMemory == 0);
            GpuInfo info = { desc.Description, desc.VendorId, integrated };
            if (integrated && !g_haveIGpu) { g_iGpu = info; g_haveIGpu = true; }
            else if (!integrated && !g_haveDGpu) { g_dGpu = info; g_haveDGpu = true; }
        }
        pAdapter->Release();
    }
    pFactory->Release();
}

// ── UI / Icons ──────────────────────────────────────────────────────────────

static HICON g_icons[7] = { nullptr }; // Unknown, i, I, n, N, a, A

static HICON MakeLetterIcon(WCHAR letter, COLORREF bg, int size) {
    int SZ = (size > 0) ? size : 16;
    HDC hdc = GetDC(nullptr);
    HDC mdc = CreateCompatibleDC(hdc);
    BITMAPINFO bi = { {sizeof(BITMAPINFOHEADER), SZ, -SZ, 1, 32, BI_RGB} };
    void* bits;
    HBITMAP hBmp = CreateDIBSection(mdc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP oldB = (HBITMAP)SelectObject(mdc, hBmp);

    RECT rc = {0, 0, SZ, SZ};
    HBRUSH bBr = CreateSolidBrush(RGB(0,0,0)); FillRect(mdc, &rc, bBr); DeleteObject(bBr);
    HBRUSH cBr = CreateSolidBrush(bg); SelectObject(mdc, cBr);
    HPEN cPn = CreatePen(PS_SOLID, 1, bg); SelectObject(mdc, cPn);
    Ellipse(mdc, 1, 1, SZ-1, SZ-1);

    HFONT hf = CreateFontW(SZ-3, 0, 0, 0, FW_HEAVY, 0, 0, 0, 0, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SelectObject(mdc, hf); SetBkMode(mdc, TRANSPARENT); SetTextColor(mdc, RGB(255,255,255));
    DrawTextW(mdc, &letter, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    HBITMAP hMsk = CreateBitmap(SZ, SZ, 1, 1, nullptr);
    HDC mskDC = CreateCompatibleDC(hdc); SelectObject(mskDC, hMsk);
    SetBkColor(mdc, RGB(0,0,0)); BitBlt(mskDC, 0, 0, SZ, SZ, mdc, 0, 0, SRCCOPY);

    ICONINFO ii = {TRUE, 0, 0, hMsk, hBmp};
    HICON res = CreateIconIndirect(&ii);
    
    DeleteObject(hf); DeleteObject(cBr); DeleteObject(cPn); DeleteObject(hBmp); DeleteObject(hMsk);
    DeleteDC(mdc); DeleteDC(mskDC); ReleaseDC(nullptr, hdc);
    return res;
}

static void CreateIcons() {
    int sz = GetSystemMetrics(SM_CXSMICON);
    g_icons[0] = MakeLetterIcon(L'?', RGB(136,136,136), sz);
    g_icons[1] = MakeLetterIcon(L'i', RGB(91,141,184), sz);
    g_icons[2] = MakeLetterIcon(L'I', RGB(91,141,184), sz);
    g_icons[3] = MakeLetterIcon(L'n', RGB(90,160,106), sz);
    g_icons[4] = MakeLetterIcon(L'N', RGB(90,160,106), sz);
    g_icons[5] = MakeLetterIcon(L'a', RGB(200,80,80), sz);
    g_icons[6] = MakeLetterIcon(L'A', RGB(200,80,80), sz);
}

static HICON GetModeIcon(GpuMode mode) {
    const GpuInfo* info = (mode == GpuMode::DGPU) ? &g_dGpu : &g_iGpu;
    if (!info->vendorId) return g_icons[0];
    if (info->vendorId == 0x8086) return info->isIntegrated ? g_icons[1] : g_icons[2];
    if (info->vendorId == 0x10DE) return info->isIntegrated ? g_icons[3] : g_icons[4];
    return info->isIntegrated ? g_icons[5] : g_icons[6];
}

// ── Tray & Window Procedure ─────────────────────────────────────────────────

#define WM_TRAY (WM_USER + 1)
#define ID_POLL 1
#define CMD_TOGGLE 2
#define CMD_EXIT 3

static HWND g_hwnd = nullptr;

static void UpdateTray(GpuMode mode, bool notify = false) {
    if (!notify && mode == g_currentMode) return;
    g_currentMode = mode;
    NOTIFYICONDATA nid = { sizeof(nid), g_hwnd, 1, NIF_ICON | NIF_TIP | NIF_MESSAGE };
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = GetModeIcon(mode);
    
    std::wstring tip = (mode == GpuMode::DGPU) ? L"dGPU active" : L"Hybrid active";
    const GpuInfo* info = (mode == GpuMode::DGPU) ? &g_dGpu : &g_iGpu;
    if (info->vendorId) tip += L" - " + info->name;
    StringCchCopyW(nid.szTip, ARRAYSIZE(nid.szTip), tip.c_str());

    if (notify) {
        nid.uFlags |= NIF_INFO;
        StringCchCopyW(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), L"GPU Mode Changed");
        StringCchCopyW(nid.szInfo, ARRAYSIZE(nid.szInfo), tip.c_str());
        nid.dwInfoFlags = NIIF_INFO;
    }
    Shell_NotifyIconW(g_currentMode == GpuMode::Unknown ? NIM_ADD : NIM_MODIFY, &nid);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TRAY && lp == WM_RBUTTONUP) {
        POINT pt; GetCursorPos(&pt);
        HMENU m = CreatePopupMenu();
        std::wstring label = L"Active: " + ((g_currentMode == GpuMode::DGPU) ? g_dGpu.name : g_iGpu.name);
        AppendMenuW(m, MF_STRING | MF_GRAYED, 0, label.c_str());
        AppendMenuW(m, MF_SEPARATOR, 0, 0);
        AppendMenuW(m, MF_STRING, CMD_TOGGLE, (g_currentMode == GpuMode::DGPU) ? L"Switch to Hybrid" : L"Switch to dGPU");
        AppendMenuW(m, MF_STRING, CMD_EXIT, L"Exit (Revert to Hybrid)");
        
        SetForegroundWindow(hwnd);
        int cmd = TrackPopupMenu(m, TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, 0);
        if (cmd == CMD_TOGGLE) {
            SetMode(g_currentMode == GpuMode::DGPU ? GpuMode::Hybrid : GpuMode::DGPU);
            Sleep(1500); // Wait for EC
            UpdateTray(QueryMode(), true);
        } else if (cmd == CMD_EXIT) {
            PostQuitMessage(0);
        }
        DestroyMenu(m);
    } else if (msg == WM_TIMER && wp == ID_POLL) {
        UpdateTray(QueryMode());
    } else if (msg == WM_DPICHANGED) {
        for(auto& i : g_icons) DestroyIcon(i);
        CreateIcons();
        UpdateTray(g_currentMode);
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR, int) {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"LenovoGpuTrayMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    if (!WmiInit()) {
        MessageBoxW(nullptr, L"Lenovo WMI interface not found.", L"Error", MB_ICONERROR);
        return 1;
    }

    DetectGpus();
    CreateIcons();

    WNDCLASSW wc = { 0, WndProc, 0, 0, hi, 0, 0, 0, 0, L"GpuTray" };
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, 0, hi, 0);

    SetMode(GpuMode::DGPU);
    Sleep(1500);
    UpdateTray(QueryMode(), true);
    SetTimer(g_hwnd, ID_POLL, 5000, nullptr);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }

    SetMode(GpuMode::Hybrid);
    Sleep(1500);
    
    NOTIFYICONDATA nid = { sizeof(nid), g_hwnd, 1 };
    Shell_NotifyIconW(NIM_DELETE, &nid);
    WmiShutdown();
    return 0;
}

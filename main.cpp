/*
 * gpu_tray — Lenovo Legion dGPU Tray Switcher
 * Targets: Intel iGPU + NVIDIA dGPU on Lenovo Legion / IdeaPad Gaming laptops
 * Uses:    LENOVO_GAMEZONE_DATA WMI (methods 5/6: Get/SetGpuGpsState)
 *
 * GPU Working Mode values:
 *   1 = Hybrid      (iGPU display, dGPU on-demand — best battery)
 *   4 = dGPU        (dGPU drives display directly — best performance)
 *
 * Behaviour:
 *   Launch  -> set mode 4 (dGPU); icon always reflects live state
 *   Exit    -> set mode 1 (Hybrid); exits immediately, no animation
 *   Right-click -> live status label, manual revert, Exit
 */

#include <Windows.h>
#include <shellapi.h>
#include <wbemidl.h>
#include <comdef.h>
#include <string>
#include <strsafe.h>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// ── WMI / LENOVO_GAMEZONE_DATA ───────────────────────────────────────────────

#define GAMEZONE_WMI_NAMESPACE  L"ROOT\\WMI"
#define GAMEZONE_WMI_CLASS      L"LENOVO_GAMEZONE_DATA"
#define METHOD_GET_GPU_GPS      L"GetGpuGpsState"   // WmiMethodId 5
#define METHOD_SET_GPU_GPS      L"SetGpuGpsState"   // WmiMethodId 6

#define GPU_MODE_HYBRID  1u   // Optimus: iGPU display, dGPU on-demand
#define GPU_MODE_DGPU    4u   // dGPU drives display directly

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
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) { CoUninitialize(); return false; }

    hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IWbemLocator, (void**)&g_wmi.pLocator);
    if (FAILED(hr)) { CoUninitialize(); return false; }

    hr = g_wmi.pLocator->ConnectServer(
            _bstr_t(GAMEZONE_WMI_NAMESPACE), nullptr, nullptr, nullptr,
            0, nullptr, nullptr, &g_wmi.pServices);
    if (FAILED(hr)) { g_wmi.pLocator->Release(); CoUninitialize(); return false; }

    hr = CoSetProxyBlanket(g_wmi.pServices,
                           RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                           RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
                           nullptr, EOAC_NONE);
    if (FAILED(hr)) {
        g_wmi.pServices->Release();
        g_wmi.pLocator->Release();
        CoUninitialize();
        return false;
    }

    g_wmi.ok = true;
    return true;
}

static void WmiShutdown()
{
    if (g_wmi.pServices) { g_wmi.pServices->Release(); g_wmi.pServices = nullptr; }
    if (g_wmi.pLocator)  { g_wmi.pLocator->Release();  g_wmi.pLocator  = nullptr; }
    if (g_wmi.ok) CoUninitialize();
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
    if (FAILED(hr)) { pClass->Release(); return hr; }

    IWbemClassObject* pInstance = nullptr;
    ULONG uReturned = 0;
    hr = pEnum->Next(WBEM_INFINITE, 1, &pInstance, &uReturned);
    pEnum->Release();
    if (FAILED(hr) || uReturned == 0) { pClass->Release(); return E_NOINTERFACE; }

    VARIANT vPath;
    VariantInit(&vPath);
    hr = pInstance->Get(L"__PATH", 0, &vPath, nullptr, nullptr);
    pInstance->Release();
    if (FAILED(hr)) { pClass->Release(); return hr; }

    // Build input parameters if needed
    if (inParamName)
    {
        hr = pClass->GetMethod(method, 0, &pInClass, nullptr);
        if (SUCCEEDED(hr) && pInClass)
        {
            pInClass->SpawnInstance(0, &pInInst);
            VARIANT vIn;
            vIn.vt   = VT_I4;
            vIn.lVal = (LONG)inValue;
            pInInst->Put(inParamName, 0, &vIn, 0);
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

// ── Tray icon rendering ──────────────────────────────────────────────────────
// 16×16 DIB: filled circle + centred 13 px bold letter.
// Soft muted palette: steel blue for iGPU, sage green for dGPU, grey for unknown.

static HICON MakeLetterIcon(WCHAR letter, COLORREF bg, COLORREF fg)
{
    const int SZ = 16;

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
    FillRect(hdcMem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

    HBRUSH hBrush = CreateSolidBrush(bg);
    HPEN   hPen   = CreatePen(PS_SOLID, 1, bg);
    HPEN   hOldP  = (HPEN)SelectObject(hdcMem, hPen);
    HBRUSH hOldB  = (HBRUSH)SelectObject(hdcMem, hBrush);
    Ellipse(hdcMem, 1, 1, SZ - 1, SZ - 1);
    SelectObject(hdcMem, hOldP);
    SelectObject(hdcMem, hOldB);
    DeleteObject(hBrush);
    DeleteObject(hPen);

    HFONT hFont = CreateFontW(
        13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
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

static HICON g_hIconHybrid  = nullptr;   // blue  "I"
static HICON g_hIconDGPU    = nullptr;   // green "N"
static HICON g_hIconUnknown = nullptr;   // grey  "?"

static void CreateIcons()
{
    g_hIconHybrid  = MakeLetterIcon(L'I', RGB( 91, 141, 184), RGB(255, 255, 255));
    g_hIconDGPU    = MakeLetterIcon(L'N', RGB( 90, 160, 106), RGB(255, 255, 255));
    g_hIconUnknown = MakeLetterIcon(L'?', RGB(136, 136, 136), RGB(255, 255, 255));
}

static void DestroyIcons()
{
    if (g_hIconHybrid)  { DestroyIcon(g_hIconHybrid);  g_hIconHybrid  = nullptr; }
    if (g_hIconDGPU)    { DestroyIcon(g_hIconDGPU);    g_hIconDGPU    = nullptr; }
    if (g_hIconUnknown) { DestroyIcon(g_hIconUnknown); g_hIconUnknown = nullptr; }
}

// ── Tray management ──────────────────────────────────────────────────────────

#define WM_TRAY       (WM_USER + 1)
#define WM_POLL_GPU   (WM_USER + 2)   // posted by timer to refresh icon
#define TRAY_ID       1
#define POLL_TIMER_ID 1
#define POLL_INTERVAL_MS 3000         // re-query GPU mode every 3 seconds

static HWND      g_hwnd  = nullptr;
static HINSTANCE g_hInst = nullptr;

static HICON ModeIcon(GpuMode m)
{
    if (m == GpuMode::DGPU)   return g_hIconDGPU;
    if (m == GpuMode::Hybrid) return g_hIconHybrid;
    return g_hIconUnknown;
}

static std::wstring ModeTooltip(GpuMode m)
{
    if (m == GpuMode::DGPU)   return L"dGPU active \u2014 NVIDIA RTX (Discrete mode)";
    if (m == GpuMode::Hybrid) return L"Hybrid active \u2014 Intel iGPU (Optimus mode)";
    return L"GPU mode unknown \u2014 WMI unavailable";
}

static void AddTrayIcon(GpuMode m)
{
    std::wstring tip = ModeTooltip(m);
    NOTIFYICONDATA nid   = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = g_hwnd;
    nid.uID              = TRAY_ID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon            = ModeIcon(m);
    StringCchCopyW(nid.szTip, ARRAYSIZE(nid.szTip), tip.c_str());
    Shell_NotifyIconW(NIM_ADD, &nid);
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

#define CMD_REVERT 1
#define CMD_EXIT   2

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Periodic live poll: re-query mode and update icon if it changed
    if (msg == WM_TIMER && wParam == POLL_TIMER_ID)
    {
        GpuMode live = QueryMode();
        RefreshTrayIcon(live);
        return 0;
    }

    if (msg == WM_TRAY && lParam == WM_RBUTTONUP)
    {
        // Always show the real current state in the menu label
        GpuMode mode = QueryMode();
        RefreshTrayIcon(mode);

        std::wstring label =
            (mode == GpuMode::DGPU)   ? L"Active: dGPU (NVIDIA Discrete)" :
            (mode == GpuMode::Hybrid) ? L"Active: Hybrid (Intel iGPU)"    :
                                        L"Active: Unknown";

        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, label.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CMD_REVERT, L"Revert to Hybrid (iGPU)");
        AppendMenuW(menu, MF_STRING, CMD_EXIT,   L"Exit (revert to Hybrid)");

        SetForegroundWindow(hwnd);
        POINT pt;
        GetCursorPos(&pt);
        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                 pt.x, pt.y, 0, hwnd, nullptr);
        PostMessage(hwnd, WM_NULL, 0, 0);
        DestroyMenu(menu);

        if (cmd == CMD_REVERT)
        {
            SetMode(GpuMode::Hybrid);
            Sleep(1500);
            RefreshTrayIcon(QueryMode());
        }
        else if (cmd == CMD_EXIT)
        {
            PostQuitMessage(0);
        }
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

    CreateIcons();

    bool wmiOk = WmiInit();
    if (!wmiOk)
    {
        AddTrayIcon(GpuMode::Unknown);
        MessageBoxW(nullptr,
            L"WMI initialisation failed.\n\n"
            L"This tool requires the LENOVO_GAMEZONE_DATA WMI class\n"
            L"present on Lenovo Legion / IdeaPad Gaming laptops.\n"
            L"GPU switching is unavailable.",
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
                L"SetGpuGpsState returned an error.\n\n"
                L"Make sure you are running as Administrator and that\n"
                L"Lenovo Vantage / Legion Zone services are not conflicting.",
                L"GPU Tray Switcher \u2014 Switch Error",
                MB_OK | MB_ICONWARNING);
        }

        // Start live-poll timer: refreshes icon every 3 s without any overhead
        SetTimer(g_hwnd, POLL_TIMER_ID, POLL_INTERVAL_MS, nullptr);
    }

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup: revert to Hybrid and exit immediately (no animation)
    KillTimer(g_hwnd, POLL_TIMER_ID);
    if (wmiOk) SetMode(GpuMode::Hybrid);
    RemoveTrayIcon();
    WmiShutdown();
    DestroyIcons();

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return 0;
}

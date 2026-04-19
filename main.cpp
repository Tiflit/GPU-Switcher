#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include "resource.h" // shared with .rc

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#define WM_TRAY (WM_USER + 1)
#define TRAY_ID 1
#define ID_RUN_AT_STARTUP 1001
#define ID_EXIT 1002

// GPU vendor hints (NVIDIA Optimus + AMD Dynamic Switchable Graphics)
extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 1;
    __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1;
}

static ID3D11Device* g_d3dDevice = nullptr;
static wchar_t g_gpuName[128] = L"GPU Tray Hook";
static UINT g_gpuVendorId = 0;
static HINSTANCE g_hInst = nullptr;
static HICON g_currentIcon = nullptr;   // resource icons must NOT be destroyed
static bool g_iconIsOwned = false;      // always false for resource icons

// ───────────────────────────────────────────────────────────────
// Startup registration
// ───────────────────────────────────────────────────────────────
bool IsStartupEnabled()
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;

    wchar_t regPath[MAX_PATH], exePath[MAX_PATH];
    DWORD size = sizeof(regPath);
    bool match = false;

    if (RegQueryValueExW(key, L"GPUSwitcher", nullptr, nullptr,
        (BYTE*)regPath, &size) == ERROR_SUCCESS)
    {
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        match = (_wcsicmp(regPath, exePath) == 0);
    }

    RegCloseKey(key);
    return match;
}

void SetStartup(bool enable)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;

    if (enable)
    {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        RegSetValueExW(key, L"GPUSwitcher", 0, REG_SZ,
            (BYTE*)exePath, (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(key, L"GPUSwitcher");
    }

    RegCloseKey(key);
}

// ───────────────────────────────────────────────────────────────
// Icon selection based on GPU vendor
// ───────────────────────────────────────────────────────────────
HICON LoadVendorIcon()
{
    HICON icon = nullptr;

    switch (g_gpuVendorId)
    {
    case 0x10DE: icon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_ICON_NVIDIA)); break;
    case 0x1002: icon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_ICON_AMD));    break;
    case 0x8086: icon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_ICON_INTEL));  break;
    default:     icon = LoadIconW(g_hInst, MAKEINTRESOURCEW(IDI_ICON_GENERIC)); break;
    }

    // Last‑resort fallback (should never happen)
    if (!icon)
        icon = LoadIconW(nullptr, IDI_APPLICATION);

    g_iconIsOwned = false; // resource icons must NOT be destroyed
    return icon;
}

// ───────────────────────────────────────────────────────────────
// Tray icon update helper
// ───────────────────────────────────────────────────────────────
void UpdateTrayIcon(HWND hwnd, HICON newIcon)
{
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.hIcon = newIcon;

    wchar_t tip[128];
    swprintf_s(tip, L"%s [%s]", g_gpuName,
               g_d3dDevice ? L"dGPU active" : L"iGPU");
    wcsncpy_s(nid.szTip, tip, _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid);

    // Only destroy icons we created manually (none in this version)
    if (g_iconIsOwned && g_currentIcon)
        DestroyIcon(g_currentIcon);

    g_currentIcon = newIcon;
    g_iconIsOwned = false;
}

// ───────────────────────────────────────────────────────────────
// GPU selection (highest VRAM)
// ───────────────────────────────────────────────────────────────
IDXGIAdapter1* PickBestAdapter(IDXGIFactory1* factory)
{
    IDXGIAdapter1* best = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    SIZE_T bestMem = 0;

    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)) &&
            !(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            desc.DedicatedVideoMemory > bestMem)
        {
            if (best) best->Release();
            best = adapter;
            bestMem = desc.DedicatedVideoMemory;
        }
        else
        {
            adapter->Release();
        }
    }
    return best;
}

bool InitD3D()
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
        return false;

    IDXGIAdapter1* adapter = PickBestAdapter(factory);
    factory->Release();

    if (!adapter)
        return false;

    HRESULT hr = D3D11CreateDevice(
        adapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        0,
        nullptr, 0,
        D3D11_SDK_VERSION,
        &g_d3dDevice,
        nullptr,
        nullptr
    );

    if (SUCCEEDED(hr))
    {
        DXGI_ADAPTER_DESC1 desc;
        if (SUCCEEDED(adapter->GetDesc1(&desc)))
        {
            wcsncpy_s(g_gpuName, desc.Description, _TRUNCATE);
            g_gpuVendorId = desc.VendorId;
        }
    }

    adapter->Release();
    return SUCCEEDED(hr);
}

void ShutdownD3D()
{
    if (g_d3dDevice)
    {
        g_d3dDevice->Release();
        g_d3dDevice = nullptr;
    }
}

// ───────────────────────────────────────────────────────────────
// Window procedure
// ───────────────────────────────────────────────────────────────
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAY:
        if (LOWORD(lParam) == WM_RBUTTONUP)
        {
            HMENU menu = CreatePopupMenu();

            bool startup = IsStartupEnabled();
            AppendMenuW(menu, MF_STRING | (startup ? MF_CHECKED : 0),
                        ID_RUN_AT_STARTUP, L"Run at startup");
            AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");

            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);

            int cmd = TrackPopupMenu(menu,
                TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                pt.x, pt.y, 0, hwnd, nullptr);

            DestroyMenu(menu);

            if (cmd == ID_RUN_AT_STARTUP)
                SetStartup(!startup);

            if (cmd == ID_EXIT)
                PostQuitMessage(0);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ───────────────────────────────────────────────────────────────
// Entry point
// ───────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_hInst = hInst;

    // Single-instance guard
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"GPUSwitcherMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(hMutex);
        return 0;
    }

    // Register hidden window
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"TrayHookClass";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"GPU-Switcher",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInst, nullptr
    );

    // Initialize GPU device (and vendor/name)
    InitD3D();

    // Add tray icon
    g_currentIcon = LoadVendorIcon();

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = g_currentIcon;

    wchar_t tip[128];
    swprintf_s(tip, L"%s [%s]", g_gpuName,
               g_d3dDevice ? L"dGPU active" : L"iGPU");
    wcsncpy_s(nid.szTip, tip, _TRUNCATE);

    Shell_NotifyIconW(NIM_ADD, &nid);

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
    ShutdownD3D();
    CloseHandle(hMutex);
    return 0;
}

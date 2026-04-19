#include <windows.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <fstream>
#include <sstream>
#include <vector>
#include "resource.h"

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
static wchar_t g_renderGpuName[128] = L"Unknown GPU";
static wchar_t g_displayGpuName[128] = L"Unknown GPU";
static UINT g_renderVendor = 0;
static UINT g_displayVendor = 0;
static HINSTANCE g_hInst = nullptr;
static HICON g_currentIcon = nullptr;
static bool g_iconIsOwned = false;

// ───────────────────────────────────────────────────────────────
// Logging (errors only, capped at ~10 KB)
// ───────────────────────────────────────────────────────────────
void LogError(const std::wstring& msg)
{
    const wchar_t* logFile = L"gpu_switcher.log";

    // Read existing log
    std::wifstream in(logFile);
    std::wstringstream buffer;
    buffer << in.rdbuf();
    in.close();

    std::wstring existing = buffer.str();

    // Append new entry
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t timestamp[64];
    swprintf_s(timestamp, L"[%04d-%02d-%02d %02d:%02d:%02d] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    existing += timestamp;
    existing += msg + L"\n";

    // Trim if > 10 KB
    const size_t MAX_SIZE = 10 * 1024;
    if (existing.size() > MAX_SIZE)
    {
        // Keep last half
        existing = existing.substr(existing.size() - MAX_SIZE / 2);
    }

    // Write back
    std::wofstream out(logFile, std::ios::trunc);
    out << existing;
}

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
// Detect display GPU (adapter driving the primary output)
// ───────────────────────────────────────────────────────────────
bool DetectDisplayGPU()
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
    {
        LogError(L"Failed to create DXGI factory for display GPU detection.");
        return false;
    }

    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
    {
        IDXGIOutput* output = nullptr;
        if (adapter->EnumOutputs(0, &output) == S_OK)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            wcsncpy_s(g_displayGpuName, desc.Description, _TRUNCATE);
            g_displayVendor = desc.VendorId;

            output->Release();
            adapter->Release();
            factory->Release();
            return true;
        }
        adapter->Release();
    }

    factory->Release();
    LogError(L"No display GPU detected.");
    return false;
}

// ───────────────────────────────────────────────────────────────
// Detect rendering GPU (highest VRAM)
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
    {
        LogError(L"Failed to create DXGI factory for D3D init.");
        return false;
    }

    IDXGIAdapter1* adapter = PickBestAdapter(factory);
    factory->Release();

    if (!adapter)
    {
        LogError(L"No suitable GPU adapter found.");
        return false;
    }

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
        adapter->GetDesc1(&desc);

        wcsncpy_s(g_renderGpuName, desc.Description, _TRUNCATE);
        g_renderVendor = desc.VendorId;
    }
    else
    {
        LogError(L"D3D11CreateDevice failed.");
    }

    adapter->Release();
    return SUCCEEDED(hr);
}

// ───────────────────────────────────────────────────────────────
// Icon selection (dual-layer combinations)
// ───────────────────────────────────────────────────────────────
HICON LoadDualIcon(UINT displayVendor, UINT renderVendor)
{
    int index = 0;

    auto vendorIndex = [](UINT v) {
        switch (v)
        {
        case 0x8086: return 0; // Intel
        case 0x10DE: return 1; // NVIDIA
        case 0x1002: return 2; // AMD
        default:     return 3; // Unknown
        }
    };

    int d = vendorIndex(displayVendor);
    int r = vendorIndex(renderVendor);

    index = d * 4 + r; // 16 combinations

    HICON icon = LoadIconW(g_hInst, MAKEINTRESOURCEW(3000 + index));
    if (!icon)
    {
        LogError(L"Failed to load icon for GPU combination.");
        icon = LoadIconW(nullptr, IDI_APPLICATION);
    }

    g_iconIsOwned = false;
    return icon;
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

            // Error info
            if (g_displayVendor == 0 || g_renderVendor == 0)
                AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, L"⚠ GPU detection error");

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

    // Detect GPUs
    DetectDisplayGPU();
    InitD3D();

    // Load icon
    g_currentIcon = LoadDualIcon(g_displayVendor, g_renderVendor);

    // Add tray icon
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = g_currentIcon;

    wchar_t tip[256];
    swprintf_s(tip,
        L"Display: %s\nRendering: %s [%s]",
        g_displayGpuName,
        g_renderGpuName,
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

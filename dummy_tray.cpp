#include <windows.h>
#include <shellapi.h>
#include <strsafe.h>

#define WM_TRAY (WM_USER + 1)
#define TRAY_ID 1

static HICON g_icon = nullptr;
static HWND  g_hwnd = nullptr;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_TRAY)
    {
        if (LOWORD(lParam) == WM_RBUTTONUP)
        {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"Exit");

            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);

            int cmd = TrackPopupMenu(
                menu,
                TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                pt.x, pt.y, 0, hwnd, nullptr
            );

            DestroyMenu(menu);

            if (cmd == 1)
                PostQuitMessage(0);
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_icon = LoadIconW(nullptr, IDI_APPLICATION);

    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"DummyTrayClass";
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

    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = g_icon;
    StringCchCopyW(nid.szTip, ARRAYSIZE(nid.szTip), L"Dummy Tray App");

    Shell_NotifyIconW(NIM_ADD, &nid);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
    return 0;
}

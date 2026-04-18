#include <windows.h>
#include <shellapi.h>

#define WM_TRAY (WM_USER + 1)
#define TRAY_ID 1

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
    // Register a tiny hidden window
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"TrayHookClass";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        wc.lpszClassName,
        L"",
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInst, nullptr
    );

    // Add tray icon
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"GPU Tray Hook");

    Shell_NotifyIconW(NIM_ADD, &nid);

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shell_NotifyIconW(NIM_DELETE, &nid);
    return 0;
}

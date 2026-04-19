#include "tray_menu.h"

#include "../win/win_helpers.h"

int ShowTrayMenu(HWND hwnd, bool startupEnabled, bool gpuError)
{
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return 0;

    AppendMenuW(menu,
                MF_STRING | (startupEnabled ? MF_CHECKED : 0),
                ID_RUN_AT_STARTUP,
                L"Run at startup");

    if (gpuError)
    {
        AppendMenuW(menu,
                    MF_STRING | MF_DISABLED,
                    0,
                    L"⚠ GPU detection error");
    }

    AppendMenuW(menu,
                MF_STRING,
                ID_EXIT,
                L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);

    int cmd = TrackPopupMenu(menu,
                             TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                             pt.x, pt.y,
                             0,
                             hwnd,
                             nullptr);

    DestroyMenu(menu);
    return cmd;
}

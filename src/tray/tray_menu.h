#pragma once

#include <Windows.h>

// Shows the tray context menu and returns the selected command ID,
// or 0 if nothing was selected.
int ShowTrayMenu(HWND hwnd, bool startupEnabled, bool gpuError);

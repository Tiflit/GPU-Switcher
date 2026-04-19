#pragma once

#include <Windows.h>

// Tray-related constants
constexpr UINT WM_TRAY          = WM_USER + 1;
constexpr UINT TRAY_ID          = 1;
constexpr UINT ID_RUN_AT_STARTUP = 1001;
constexpr UINT ID_EXIT           = 1002;

// Window procedure for the hidden tray window.
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

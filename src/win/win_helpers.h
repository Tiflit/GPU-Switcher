#pragma once

#include <Windows.h>
#include "gpu/gpu_state.h"

// Tray constants
constexpr UINT WM_TRAY = WM_USER + 1;
constexpr UINT TRAY_ID = 1;

constexpr UINT ID_RUN_AT_STARTUP     = 1001;
constexpr UINT ID_EXIT               = 1002;
constexpr UINT ID_TOGGLE_RENDER_GPU  = 2001;
constexpr UINT ID_RESTART_GPU_DRIVER = 2002;

// Globals from main.cpp
extern GpuState g_displayGpuState;
extern GpuState g_renderGpuState;
extern bool     g_forceRenderGpu;

// Refresh function from main.cpp
void RefreshGpuState(HWND hwnd);

// Window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

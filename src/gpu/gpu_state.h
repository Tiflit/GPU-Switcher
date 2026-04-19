#pragma once
#include <string>
#include <Windows.h>   // <-- REQUIRED for UINT

struct GpuState {
    std::wstring name;
    UINT vendor = 0;
};

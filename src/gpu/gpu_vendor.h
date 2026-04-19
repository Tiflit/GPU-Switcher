#pragma once
#include <Windows.h>

enum class GpuVendor : UINT
{
    Unknown = 0,
    Intel   = 0x8086,
    Nvidia  = 0x10DE,
    AMD     = 0x1002
};

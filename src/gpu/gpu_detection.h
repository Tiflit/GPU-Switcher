#pragma once

#include <string>
#include "gpu_state.h"

bool DetectDisplayGPU(GpuState& outState);
bool DetectRenderGPU(GpuState& outState);

std::wstring BuildGpuTooltip(
    const GpuState& display,
    const GpuState& render
);

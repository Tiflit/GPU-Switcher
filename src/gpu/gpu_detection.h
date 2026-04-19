#pragma once

#include <string>
#include "gpu_state.h"

bool DetectDisplayGPU(GpuState& outState);
bool DetectRenderGPU(GpuState& outState);

// Creates a dummy D3D11 device to wake/activate the dGPU.
// Returns true on success.
bool ActivateRenderGPU();

std::wstring BuildGpuTooltip(
    const GpuState& display,
    const GpuState& render
);

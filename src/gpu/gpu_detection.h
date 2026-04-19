#pragma once

#include <string>
#include "gpu_state.h"

// Detects the adapter driving the primary display.
// Returns true on success and fills outState.
bool DetectDisplayGPU(GpuState& outState);

// Detects the GPU used for rendering by creating a D3D11 device.
// Returns true on success and fills outState.
bool DetectRenderGPU(GpuState& outState);

// Creates a dummy D3D11 device to wake/activate the dGPU.
// Returns true on success.
bool ActivateRenderGPU();

// Builds a tooltip string summarizing display + render GPUs.
std::wstring BuildGpuTooltip(
    const GpuState& display,
    const GpuState& render
);

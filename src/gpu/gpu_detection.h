#pragma once

#include <string>
#include "gpu_state.h"

// Detects the adapter driving the primary display.
// Returns true on success and fills outState.
bool DetectDisplayGPU(GpuState& outState);

// Creates a dummy D3D11 device on the dGPU (adapter with most VRAM).
// Returns true on success.
bool ActivateRenderGPU();

// Builds a tooltip string summarizing the display GPU only.
std::wstring BuildGpuTooltip(const GpuState& display);

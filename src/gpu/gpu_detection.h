#pragma once

#include <string>
#include "gpu_state.h"

// Detects the adapter driving the primary display.
// Returns true on success and fills outState.
bool DetectDisplayGPU(GpuState& outState);

// Detects the GPU that is actively used for rendering (via D3D11 device).
// Returns true on success and fills outState.
bool DetectRenderGPU(GpuState& outState);

// Builds a tooltip string summarizing display + render GPUs.
std::wstring BuildGpuTooltip(const GpuState& display, const GpuState& render);

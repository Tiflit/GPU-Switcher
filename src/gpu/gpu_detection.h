#pragma once

#include "gpu_state.h"

// Detects the display GPU (adapter driving the primary output).
// Returns true on success and fills outState; false on failure.
bool DetectDisplayGPU(GpuState& outState);

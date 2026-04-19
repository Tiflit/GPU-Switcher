#pragma once
#include <Windows.h>
#include "gpu/gpu_vendor.h"

// Loads the correct icon based on GPU vendor ID.
HICON LoadDisplayIcon(UINT vendor, HINSTANCE hInst);

#pragma once

#include <Windows.h>

// Returns an icon handle for the given GPU vendor.
// If vendor == 0, a warning icon is returned.
HICON LoadDisplayIcon(UINT vendor, HINSTANCE hInst);

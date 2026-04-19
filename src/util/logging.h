#pragma once

#include <string>

// Logs an error message to gpu_switcher.log (capped at ~10 KB).
void LogError(const std::wstring& msg);

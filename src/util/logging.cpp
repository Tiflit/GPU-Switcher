#include "logging.h"

#include <Windows.h>   // <-- REQUIRED for SYSTEMTIME and GetLocalTime
#include <fstream>
#include <sstream>
#include <filesystem>

void LogError(const std::wstring& msg)
{
    const wchar_t* logFile = L"gpu_switcher.log";

    // Append new entry
    {
        std::wofstream out(logFile, std::ios::app);
        if (!out.is_open())
            return;

        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t timestamp[64];
        swprintf_s(timestamp, L"[%04d-%02d-%02d %02d:%02d:%02d] ",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

        out << timestamp << msg << L"\n";
    }

    // Truncate if file grows too large
    const std::uintmax_t MAX_SIZE = 10 * 1024; // ~10 KB
    std::error_code ec;
    auto size = std::filesystem::file_size(logFile, ec);
    if (ec || size <= MAX_SIZE)
        return;

    // Keep the last half
    std::wifstream in(logFile);
    if (!in.is_open())
        return;

    std::wstring content((std::istreambuf_iterator<wchar_t>(in)),
                         std::istreambuf_iterator<wchar_t>());

    if (content.size() > MAX_SIZE / 2)
        content = content.substr(content.size() - MAX_SIZE / 2);

    std::wofstream out(logFile, std::ios::trunc);
    if (!out.is_open())
        return;

    out << content;
}

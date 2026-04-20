#include "logging.h"

#include <windows.h>
#include <fstream>
#include <filesystem>

static void WriteLog(const wchar_t* level, const std::wstring& msg)
{
    const wchar_t* logFile = L"gpu_switcher.log";

    {
        std::wofstream out(logFile, std::ios::app);
        if (!out.is_open()) return;

        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t ts[64];
        swprintf_s(ts, L"[%04d-%02d-%02d %02d:%02d:%02d] [%s] ",
            st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute, st.wSecond,
            level);

        out << ts << msg << L"\n";
    }

    const std::uintmax_t MAX_SIZE = 10 * 1024;
    std::error_code ec;
    auto size = std::filesystem::file_size(logFile, ec);
    if (ec || size <= MAX_SIZE) return;

    std::wifstream in(logFile);
    if (!in.is_open()) return;
    std::wstring content((std::istreambuf_iterator<wchar_t>(in)),
                          std::istreambuf_iterator<wchar_t>());
    in.close();

    if (content.size() > MAX_SIZE / 2)
        content = content.substr(content.size() - MAX_SIZE / 2);

    std::wofstream out(logFile, std::ios::trunc);
    if (out.is_open()) out << content;
}

void LogError(const std::wstring& msg) { WriteLog(L"ERROR", msg); }
void LogInfo (const std::wstring& msg) { WriteLog(L"INFO",  msg); }

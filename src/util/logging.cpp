#include "logging.h"

#include <windows.h>
#include <fstream>
#include <filesystem>

static bool g_loggingEnabled  = false;
static bool g_firstError      = false;

static std::wstring GetLogPath()
{
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
        return L"gpu_switcher.log";

    std::filesystem::path p(exePath);
    p = p.parent_path() / L"gpu_switcher.log";
    return p.wstring();
}

static void WriteLog(const wchar_t* level, const std::wstring& msg)
{
    const std::wstring logPath = GetLogPath();

    {
        std::wofstream out(logPath, std::ios::app);
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
    auto size = std::filesystem::file_size(logPath, ec);
    if (ec || size <= MAX_SIZE) return;

    std::wifstream in(logPath);
    if (!in.is_open()) return;
    std::wstring content((std::istreambuf_iterator<wchar_t>(in)),
                          std::istreambuf_iterator<wchar_t>());
    in.close();

    if (content.size() > MAX_SIZE / 2)
        content = content.substr(content.size() - MAX_SIZE / 2);

    std::wofstream out(logPath, std::ios::trunc);
    if (out.is_open()) out << content;
}

void LogError(const std::wstring& msg)
{
    if (!g_firstError)
    {
        g_firstError      = true;
        g_loggingEnabled  = true;
        WriteLog(L"ERROR", L"--- Logging activated due to first error ---");
    }

    WriteLog(L"ERROR", msg);
}

void LogInfo(const std::wstring& msg)
{
    if (!g_loggingEnabled) return;
    WriteLog(L"INFO", msg);
}

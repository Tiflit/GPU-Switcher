#include "logging.h"

#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <filesystem>
#include <string>

static std::wstring GetLogPath()
{
    static std::wstring s_cachedPath;
    if (!s_cachedPath.empty())
        return s_cachedPath;

    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)) && localAppData)
    {
        std::filesystem::path dir = std::filesystem::path(localAppData) / L"GPU-Switcher";
        CoTaskMemFree(localAppData);

        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!ec)
        {
            s_cachedPath = (dir / L"gpu_switcher.log").wstring();
            return s_cachedPath;
        }
    }

    // Fallback to directory of executable
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0)
    {
        std::filesystem::path p(exePath);
        s_cachedPath = (p.parent_path() / L"gpu_switcher.log").wstring();
        return s_cachedPath;
    }

    s_cachedPath = L"gpu_switcher.log";
    return s_cachedPath;
}

static void WriteLog(const wchar_t* level, const std::wstring& msg)
{
    const std::wstring logPath = GetLogPath();

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t ts[64];
    swprintf_s(ts, L"[%04d-%02d-%02d %02d:%02d:%02d] [%s] ",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond,
        level);

    std::wstring line = std::wstring(ts) + msg + L"\n";

    // Write as UTF-8 for clean Unicode encoding across all platforms/viewers
    int utf8Len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Len <= 1) return;

    std::string utf8Str(utf8Len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, &utf8Str[0], utf8Len - 1, nullptr, nullptr);

    {
        std::ofstream out(logPath, std::ios::app | std::ios::binary);
        if (!out.is_open()) return;
        out.write(utf8Str.data(), utf8Str.size());
    }

    const std::uintmax_t MAX_SIZE = 16 * 1024; // 16 KB
    std::error_code ec;
    auto size = std::filesystem::file_size(logPath, ec);
    if (ec || size <= MAX_SIZE) return;

    // Truncate from front at clean newline boundary
    std::ifstream in(logPath, std::ios::binary);
    if (!in.is_open()) return;
    std::string content((std::istreambuf_iterator<char>(in)),
                         std::istreambuf_iterator<char>());
    in.close();

    if (content.size() > MAX_SIZE / 2)
    {
        size_t cutPos = content.size() - MAX_SIZE / 2;
        size_t nextNl = content.find('\n', cutPos);
        if (nextNl != std::string::npos && nextNl + 1 < content.size())
            content = content.substr(nextNl + 1);
        else
            content = content.substr(cutPos);
    }

    std::ofstream out(logPath, std::ios::trunc | std::ios::binary);
    if (out.is_open())
        out.write(content.data(), content.size());
}

void LogError(const std::wstring& msg)
{
    WriteLog(L"ERROR", msg);
}

void LogInfo(const std::wstring& msg)
{
    WriteLog(L"INFO", msg);
}

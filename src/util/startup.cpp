#include "startup.h"
#include <windows.h>
#include <string>

static const wchar_t* kRunKey  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kValName = L"GPUSwitcher";

static std::wstring StripQuotes(const std::wstring& s)
{
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"')
        return s.substr(1, s.size() - 2);
    return s;
}

bool IsStartupEnabled()
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;

    wchar_t regPath[MAX_PATH + 2] = {};
    wchar_t exePath[MAX_PATH] = {};
    DWORD size = sizeof(regPath) - sizeof(wchar_t);
    bool match = false;

    if (RegQueryValueExW(key, kValName, nullptr, nullptr,
                         reinterpret_cast<BYTE*>(regPath), &size) == ERROR_SUCCESS)
    {
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0)
        {
            std::wstring cleanReg = StripQuotes(regPath);
            match = (_wcsicmp(cleanReg.c_str(), exePath) == 0);
        }
    }

    RegCloseKey(key);
    return match;
}

void SetStartup(bool enable)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;

    if (enable)
    {
        wchar_t exePath[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
        {
            RegCloseKey(key);
            return;
        }

        std::wstring quoted = L"\"" + std::wstring(exePath) + L"\"";
        RegSetValueExW(key, kValName, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(quoted.c_str()),
            static_cast<DWORD>((quoted.length() + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(key, kValName);
    }

    RegCloseKey(key);
}

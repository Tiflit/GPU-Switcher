#include "startup.h"

#include <Windows.h>

bool IsStartupEnabled()
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;

    wchar_t regPath[MAX_PATH], exePath[MAX_PATH];
    DWORD size = sizeof(regPath);
    bool match = false;

    if (RegQueryValueExW(key, L"GPUSwitcher", nullptr, nullptr,
        (BYTE*)regPath, &size) == ERROR_SUCCESS)
    {
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        match = (_wcsicmp(regPath, exePath) == 0);
    }

    RegCloseKey(key);
    return match;
}

void SetStartup(bool enable)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return;

    if (enable)
    {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        RegSetValueExW(key, L"GPUSwitcher", 0, REG_SZ,
            (BYTE*)exePath, (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(key, L"GPUSwitcher");
    }

    RegCloseKey(key);
}

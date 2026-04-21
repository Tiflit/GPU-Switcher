#include "startup.h"
#include <windows.h>

static const wchar_t* kRunKey  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kValName = L"GPUSwitcher";

bool IsStartupEnabled()
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;

    wchar_t regPath[MAX_PATH] = {};
    wchar_t exePath[MAX_PATH] = {};
    DWORD size = sizeof(regPath);
    bool match = false;

    if (RegQueryValueExW(key, kValName, nullptr, nullptr,
                         (BYTE*)regPath, &size) == ERROR_SUCCESS)
    {
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0)
            match = (_wcsicmp(regPath, exePath) == 0);
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

        RegSetValueExW(key, kValName, 0, REG_SZ,
            (BYTE*)exePath, (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t)));
    }
    else
    {
        RegDeleteValueW(key, kValName);
    }

    RegCloseKey(key);
}

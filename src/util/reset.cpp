#include "reset.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <vector>
#include <string>

#include "logging.h"

#pragma comment(lib, "cfgmgr32.lib")

struct AdapterTarget
{
    DEVINST      inst = 0;
    std::wstring name;
    bool         disabledSuccessfully = false;
};

static std::wstring ConfigRetToString(CONFIGRET cr)
{
    switch (cr)
    {
    case CR_SUCCESS:         return L"CR_SUCCESS";
    case CR_NEED_RESTART:    return L"CR_NEED_RESTART";
    case CR_ACCESS_DENIED:   return L"CR_ACCESS_DENIED";
    case CR_INVALID_DEVNODE: return L"CR_INVALID_DEVNODE";
    case CR_REMOVE_VETOED:   return L"CR_REMOVE_VETOED";
    default:
    {
        wchar_t buf[32];
        swprintf_s(buf, L"0x%08X", cr);
        return buf;
    }
    }
}

void CycleAllDisplayAdapters()
{
    std::vector<AdapterTarget> adapters;

    // 1. Query required buffer size for all present devices
    ULONG len = 0;
    if (CM_Get_Device_ID_List_SizeW(
            &len,
            nullptr,
            CM_GETIDLIST_FILTER_PRESENT
        ) != CR_SUCCESS || len == 0)
    {
        LogError(L"CM_Get_Device_ID_List_Size failed");
        return;
    }

    // 2. Retrieve multi-string list of device instance IDs
    std::vector<wchar_t> buf(len);
    if (CM_Get_Device_ID_ListW(
            nullptr,
            buf.data(),
            len,
            CM_GETIDLIST_FILTER_PRESENT
        ) != CR_SUCCESS)
    {
        LogError(L"CM_Get_Device_ID_List failed");
        return;
    }

    // 3. Filter for display class: GUID_DEVCLASS_DISPLAY {4d36e968-e325-11ce-bfc1-08002be10318}
    for (wchar_t* p = buf.data(); *p; p += wcslen(p) + 1)
    {
        DEVINST inst = 0;
        if (CM_Locate_DevNodeW(&inst, p, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
            continue;

        wchar_t classGuid[64] = {};
        ULONG classLen = sizeof(classGuid);

        if (CM_Get_DevNode_Registry_PropertyW(
                inst,
                CM_DRP_CLASSGUID,
                nullptr,
                classGuid,
                &classLen,
                0
            ) != CR_SUCCESS)
        {
            continue;
        }

        if (_wcsicmp(classGuid, L"{4d36e968-e325-11ce-bfc1-08002be10318}") == 0)
        {
            wchar_t devName[256] = {};
            ULONG nameLen = sizeof(devName);
            if (CM_Get_DevNode_Registry_PropertyW(inst, CM_DRP_FRIENDLYNAME, nullptr, devName, &nameLen, 0) != CR_SUCCESS)
            {
                nameLen = sizeof(devName);
                if (CM_Get_DevNode_Registry_PropertyW(inst, CM_DRP_DEVICEDESC, nullptr, devName, &nameLen, 0) != CR_SUCCESS)
                {
                    wcsncpy_s(devName, p, _TRUNCATE);
                }
            }

            adapters.push_back({ inst, devName, false });
        }
    }

    if (adapters.empty())
    {
        LogError(L"No display adapters found for reset");
        return;
    }

    LogInfo(std::wstring(L"Beginning reset of ") + std::to_wstring(adapters.size()) + L" display adapter(s):");
    for (const auto& a : adapters)
        LogInfo(L"  - Target: " + a.name);

    // 4. Disable display adapters and verify status
    for (auto& a : adapters)
    {
        LogInfo(L"Disabling: " + a.name);
        CONFIGRET cr = CM_Disable_DevNode(a.inst, 0);
        if (cr == CR_SUCCESS)
        {
            a.disabledSuccessfully = true;
            LogInfo(L"Successfully disabled: " + a.name);
        }
        else
        {
            LogError(L"Failed to disable " + a.name + L" — " + ConfigRetToString(cr));
        }
    }

    Sleep(2000); // Allow driver unload

    // 5. Re-enable display adapters with retry logic
    LogInfo(L"Re-enabling display adapters...");
    for (auto& a : adapters)
    {
        LogInfo(L"Re-enabling: " + a.name);
        CONFIGRET cr = CM_Enable_DevNode(a.inst, 0);

        int retries = 3;
        while (cr != CR_SUCCESS && retries-- > 0)
        {
            Sleep(1000);
            LogInfo(L"Retrying enable for: " + a.name);
            cr = CM_Enable_DevNode(a.inst, 0);
        }

        if (cr == CR_SUCCESS)
        {
            LogInfo(L"Successfully re-enabled: " + a.name);
        }
        else
        {
            LogError(L"CRITICAL: Failed to re-enable " + a.name + L" — " + ConfigRetToString(cr));
        }
    }

    Sleep(1500); // Allow driver re-initialization
    LogInfo(L"Adapter cycle completed");
}

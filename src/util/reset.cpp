// reset.cpp — modern, SDK‑compatible implementation
// --------------------------------------------------
// This version works on ALL Windows 10/11 SDKs,
// including the newest GitHub Actions SDK (10.0.26100+).
//
// The GUID parameter was removed from CM_Get_Device_ID_List*,
// so we enumerate ALL present devices and filter manually
// by class GUID (GUID_DEVCLASS_DISPLAY).

#include "reset.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <devguid.h>        // GUID_DEVCLASS_DISPLAY
#include <vector>

#include "logging.h"

#pragma comment(lib, "cfgmgr32.lib")

void CycleAllDisplayAdapters()
{
    std::vector<DEVINST> adapters;

    // ------------------------------------------------------------
    // 1. Query required buffer size for ALL PRESENT devices
    // ------------------------------------------------------------
    ULONG len = 0;
    if (CM_Get_Device_ID_List_SizeW(
            &len,
            nullptr,                        // no filter string
            CM_GETIDLIST_FILTER_PRESENT     // enumerate all present devices
        ) != CR_SUCCESS || len == 0)
    {
        LogError(L"CM_Get_Device_ID_List_Size failed");
        return;
    }

    // ------------------------------------------------------------
    // 2. Retrieve the full multi‑string list of device instance IDs
    // ------------------------------------------------------------
    std::vector<wchar_t> buf(len);

    if (CM_Get_Device_ID_ListW(
            buf.data(),
            len,
            nullptr,                        // no filter string
            CM_GETIDLIST_FILTER_PRESENT
        ) != CR_SUCCESS)
    {
        LogError(L"CM_Get_Device_ID_List failed");
        return;
    }

    // ------------------------------------------------------------
    // 3. Filter manually: keep only devices whose CLASSGUID matches
    //    GUID_DEVCLASS_DISPLAY = {4d36e968-e325-11ce-bfc1-08002be10318}
    // ------------------------------------------------------------
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

        // Compare against GUID_DEVCLASS_DISPLAY
        if (_wcsicmp(classGuid, L"{4d36e968-e325-11ce-bfc1-08002be10318}") == 0)
            adapters.push_back(inst);
    }

    if (adapters.empty())
    {
        LogError(L"No display adapters found for reset");
        return;
    }

    LogInfo(std::wstring(L"Disabling ") +
            std::to_wstring(adapters.size()) +
            L" display adapter(s)");

    // ------------------------------------------------------------
    // 4. Disable all display adapters
    // ------------------------------------------------------------
    for (DEVINST inst : adapters)
        CM_Disable_DevNode(inst, 0);

    Sleep(2000); // allow driver unload

    LogInfo(L"Re-enabling display adapters");

    // ------------------------------------------------------------
    // 5. Re-enable all display adapters
    // ------------------------------------------------------------
    for (DEVINST inst : adapters)
        CM_Enable_DevNode(inst, 0);

    Sleep(3000); // allow driver re-init

    LogInfo(L"Adapter cycle complete");
}

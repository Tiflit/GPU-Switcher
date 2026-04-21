#include "reset.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <vector>

#include "logging.h"

#pragma comment(lib, "cfgmgr32.lib")

void CycleAllDisplayAdapters()
{
    // Build a list of all display adapter DEVINSTs
    std::vector<DEVINST> adapters;

    ULONG len = 0;
    if (CM_Get_Device_ID_List_SizeW(&len, nullptr, CM_GETIDLIST_FILTER_CLASS |
                                                    CM_GETIDLIST_FILTER_PRESENT,
                                    &GUID_DEVCLASS_DISPLAY) != CR_SUCCESS || len == 0)
    {
        LogError(L"CM_Get_Device_ID_List_Size failed");
        return;
    }

    std::vector<wchar_t> buf(len);
    if (CM_Get_Device_ID_ListW(nullptr, buf.data(), len,
                               CM_GETIDLIST_FILTER_CLASS |
                               CM_GETIDLIST_FILTER_PRESENT,
                               &GUID_DEVCLASS_DISPLAY) != CR_SUCCESS)
    {
        LogError(L"CM_Get_Device_ID_List failed");
        return;
    }

    // The list is a double-null-terminated sequence of strings
    for (wchar_t* p = buf.data(); *p; p += wcslen(p) + 1)
    {
        DEVINST inst = 0;
        if (CM_Locate_DevNodeW(&inst, p, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS)
            adapters.push_back(inst);
    }

    if (adapters.empty())
    {
        LogError(L"No display adapters found for reset");
        return;
    }

    LogInfo(std::wstring(L"Disabling ") + std::to_wstring(adapters.size()) +
            L" display adapter(s)");

    // Disable all
    for (DEVINST inst : adapters)
        CM_Disable_DevNode(inst, 0);

    // Brief pause to let the driver unload
    Sleep(2000);

    LogInfo(L"Re-enabling display adapters");

    // Re-enable all
    for (DEVINST inst : adapters)
        CM_Enable_DevNode(inst, 0);

    // Give the driver time to fully re-initialize before we exit
    Sleep(3000);

    LogInfo(L"Adapter cycle complete");
}

#include "reset.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <vector>
#include <string>

#include "logging.h"

#pragma comment(lib, "cfgmgr32.lib")

#ifndef DN_STARTED
#define DN_STARTED 0x00000008
#endif

struct AdapterTarget
{
    DEVINST      inst = 0;
    std::wstring name;
    std::wstring instanceId;
    bool         disabledSuccessfully = false;

    AdapterTarget(DEVINST i, const std::wstring& n, const std::wstring& id, bool d = false)
        : inst(i), name(n), instanceId(id), disabledSuccessfully(d) {}
};

const wchar_t* ResetResultToString(ResetResult res)
{
    switch (res)
    {
    case ResetResult::Success:           return L"Success";
    case ResetResult::EnumerationFailed: return L"EnumerationFailed";
    case ResetResult::NoAdaptersFound:   return L"NoAdaptersFound";
    case ResetResult::PartialDisable:    return L"PartialDisable";
    case ResetResult::EnableFailed:      return L"EnableFailed";
    case ResetResult::VerifyFailed:      return L"VerifyFailed";
    default:                             return L"Unknown";
    }
}

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

ResetResult CycleAllDisplayAdapters()
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
        return ResetResult::EnumerationFailed;
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
        return ResetResult::EnumerationFailed;
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

            // Query hardware ID to distinguish PCI graphics cards from virtual/software adapters
            wchar_t hwId[512] = {};
            ULONG hwIdLen = sizeof(hwId);
            CM_Get_DevNode_Registry_PropertyW(inst, CM_DRP_HARDWAREID, nullptr, hwId, &hwIdLen, 0);

            wchar_t logBuf[512];
            swprintf_s(logBuf, L"Discovered display device: %s [Instance: %s, HardwareID: %s]",
                devName, p, hwId[0] ? hwId : L"(none)");
            LogInfo(logBuf);

            // Target PCI display adapters (e.g. PCI\VEN_10DE, PCI\VEN_1002, PCI\VEN_8086)
            // Skip non-PCI devices (ROOT\, SWD\, USB\) and Microsoft Basic Display Adapter (VEN_1414)
            if (_wcsnicmp(p, L"PCI\\", 4) != 0 && _wcsnicmp(hwId, L"PCI\\", 4) != 0)
            {
                LogInfo(std::wstring(L"Skipping non-PCI display device: ") + devName);
                continue;
            }

            if (wcsstr(hwId, L"VEN_1414") != nullptr || wcsstr(p, L"VEN_1414") != nullptr)
            {
                LogInfo(std::wstring(L"Skipping Microsoft Basic/Virtual display adapter: ") + devName);
                continue;
            }

            adapters.push_back({ inst, devName, p, false });
        }
    }

    if (adapters.empty())
    {
        LogError(L"No physical PCI display adapters found for reset");
        return ResetResult::NoAdaptersFound;
    }

    LogInfo(std::wstring(L"Beginning reset of ") + std::to_wstring(adapters.size()) + L" display adapter(s):");
    for (const auto& a : adapters)
        LogInfo(L"  - Target: " + a.name + L" (" + a.instanceId + L")");

    // 4. Disable display adapters and track results
    bool hadDisableFailure = false;
    for (auto& a : adapters)
    {
        LogInfo(L"Disabling: " + a.name + L" (" + a.instanceId + L")");
        CONFIGRET cr = CM_Disable_DevNode(a.inst, 0);
        if (cr == CR_SUCCESS)
        {
            a.disabledSuccessfully = true;
            LogInfo(L"Successfully disabled: " + a.name);
        }
        else
        {
            hadDisableFailure = true;
            LogError(L"Failed to disable " + a.name + L" — " + ConfigRetToString(cr));
        }
    }

    Sleep(2000); // Allow driver unload

    // 5. Re-enable display adapters with retry logic
    bool hadEnableFailure = false;
    LogInfo(L"Re-enabling display adapters...");
    for (auto& a : adapters)
    {
        if (!a.disabledSuccessfully)
        {
            LogInfo(L"Skipping re-enable for " + a.name + L" (was not disabled)");
            continue;
        }

        LogInfo(L"Re-enabling: " + a.name + L" (" + a.instanceId + L")");
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
            LogInfo(L"Successfully requested enable for: " + a.name);
        }
        else
        {
            hadEnableFailure = true;
            LogError(L"CRITICAL: Failed to re-enable " + a.name + L" — " + ConfigRetToString(cr));
        }
    }

    // 6. Deterministic verification: poll CM_Get_DevNode_Status until DN_STARTED with problem == 0
    bool hadVerifyFailure = false;
    LogInfo(L"Verifying device startup status with CM_Get_DevNode_Status...");
    for (auto& a : adapters)
    {
        if (!a.disabledSuccessfully)
            continue;

        bool started = false;
        ULONG status = 0;
        ULONG problem = 0;

        // Poll for up to 3000ms (15 iterations x 200ms) for driver initialization
        for (int attempt = 0; attempt < 15; ++attempt)
        {
            Sleep(200);
            CONFIGRET cr = CM_Get_DevNode_Status(&status, &problem, a.inst, 0);
            if (cr == CR_SUCCESS)
            {
                if ((status & DN_STARTED) != 0 && problem == 0)
                {
                    started = true;
                    break;
                }
            }
        }

        if (started)
        {
            wchar_t logBuf[256];
            swprintf_s(logBuf, L"Verified %s started successfully (Status: 0x%08X, Problem: %u)",
                a.name.c_str(), status, problem);
            LogInfo(logBuf);
        }
        else
        {
            hadVerifyFailure = true;
            wchar_t errBuf[256];
            swprintf_s(errBuf, L"CRITICAL: Verification failed for %s (Status: 0x%08X, Problem: %u)",
                a.name.c_str(), status, problem);
            LogError(errBuf);
        }
    }

    if (hadEnableFailure)
    {
        LogError(L"Adapter cycle completed with errors: one or more adapters failed to re-enable");
        return ResetResult::EnableFailed;
    }
    if (hadVerifyFailure)
    {
        LogError(L"Adapter cycle completed with errors: one or more adapters failed verification");
        return ResetResult::VerifyFailed;
    }
    if (hadDisableFailure)
    {
        LogError(L"Adapter cycle completed with warning: one or more adapters could not be disabled; only successfully disabled adapters were cycled and verified");
        return ResetResult::PartialDisable;
    }

    LogInfo(L"Adapter cycle completed successfully: all display adapters restarted and verified");
    return ResetResult::Success;
}

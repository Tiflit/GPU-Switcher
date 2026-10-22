#include "reset.h"

#include <windows.h>
#include <cfgmgr32.h>
#include <cfg.h>
#include "reset/reset_lock.h"
#include "reset/reset_policy.h"
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
    reset::InitialState initial;
    CONFIGRET lastLocate = CR_SUCCESS;
    CONFIGRET lastStatus = CR_SUCCESS;
    CONFIGRET lastEnable = CR_SUCCESS;
    ULONG status = 0;
    ULONG problem = 0;
    unsigned enableAttempts = 0;

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
    case ResetResult::AlreadyRunning:    return L"AlreadyRunning";
    case ResetResult::LockFailed:        return L"LockFailed";
    case ResetResult::NoEligibleAdapters: return L"NoEligibleAdapters";
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

static bool ReadStatus(AdapterTarget& adapter)
{
    // Devnode handles can change while drivers are cycled. Never reuse a stale one.
    adapter.lastLocate = CM_Locate_DevNodeW(&adapter.inst, adapter.instanceId.data(), CM_LOCATE_DEVNODE_NORMAL);
    adapter.status = 0;
    adapter.problem = 0;
    if (adapter.lastLocate != CR_SUCCESS)
    {
        adapter.lastStatus = adapter.lastLocate;
        return false;
    }
    adapter.lastStatus = CM_Get_DevNode_Status(&adapter.status, &adapter.problem, adapter.inst, 0);
    if (adapter.lastStatus == CR_SUCCESS && !(adapter.status & DN_HAS_PROBLEM))
        adapter.problem = 0;
    return adapter.lastStatus == CR_SUCCESS;
}

static bool VerifyStarted(AdapterTarget& adapter)
{
    for (int attempt = 0; attempt < 15; ++attempt)
    {
        Sleep(200);
        if (ReadStatus(adapter) && (adapter.status & DN_STARTED) && adapter.problem == 0)
            return true;
    }
    return false;
}

static void LogOutcome(const AdapterTarget& adapter, const wchar_t* phase, ULONGLONG started)
{
    LogInfo(std::wstring(phase) + L": " + adapter.instanceId +
        L"; initialKnown=" + std::to_wstring(adapter.initial.known) +
        L"; initialDisabled=" + std::to_wstring(adapter.initial.disabled) +
        L"; initialStarted=" + std::to_wstring(adapter.initial.started) +
        L"; disabledByUs=" + std::to_wstring(adapter.disabledSuccessfully) +
        L"; lastLocate=" + ConfigRetToString(adapter.lastLocate) +
        L"; lastStatus=" + ConfigRetToString(adapter.lastStatus) +
        L"; status=" + std::to_wstring(adapter.status) +
        L"; problem=" + std::to_wstring(adapter.problem) +
        L"; enableAttempts=" + std::to_wstring(adapter.enableAttempts) +
        L"; lastEnable=" + ConfigRetToString(adapter.lastEnable) +
        L"; elapsedMs=" + std::to_wstring(GetTickCount64() - started));
}

ResetResult CycleAllDisplayAdapters()
{
    reset::Lock lock;
    if (lock.Status() != reset::LockStatus::Acquired)
    {
        LogError(L"Reset excluded: another helper owns the lock, or the lock is inaccessible");
        return lock.Status() == reset::LockStatus::Busy ? ResetResult::AlreadyRunning : ResetResult::LockFailed;
    }
    const ULONGLONG started = GetTickCount64();
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

    // Snapshot every target before changing any device. Unknown, stopped, or
    // pre-disabled devices are not touched and will never be enabled by us.
    unsigned eligible = 0;
    bool hadDisableFailure = false;
    for (auto& adapter : adapters)
    {
        adapter.initial.known = ReadStatus(adapter);
        adapter.initial.disabled = adapter.initial.known && adapter.problem == CM_PROB_DISABLED;
        adapter.initial.started = adapter.initial.known &&
            (adapter.status & DN_STARTED) && adapter.problem == 0;
        if (reset::Eligible(adapter.initial))
            ++eligible;
        else if (!adapter.initial.known || !adapter.initial.disabled)
            hadDisableFailure = true;
        LogOutcome(adapter, L"Initial state", started);
    }
    if (!eligible)
        return ResetResult::NoEligibleAdapters;

    unsigned changed = 0;
    for (auto& adapter : adapters)
    {
        if (!reset::Eligible(adapter.initial))
        {
            LogOutcome(adapter, L"Skipped initial state", started);
            continue;
        }
        // Refresh immediately before the request as well as before recovery.
        if (!ReadStatus(adapter) || !(adapter.status & DN_STARTED) || adapter.problem != 0)
        {
            hadDisableFailure = true;
            LogOutcome(adapter, L"Skipped changed/unreadable state", started);
            continue;
        }
        const CONFIGRET cr = CM_Disable_DevNode(adapter.inst, 0);
        adapter.disabledSuccessfully = cr == CR_SUCCESS;
        LogInfo(L"Disable request: " + adapter.instanceId + L"; result=" + ConfigRetToString(cr));
        if (adapter.disabledSuccessfully)
        {
            ++changed;
            ReadStatus(adapter);
            LogOutcome(adapter, L"After disable request", started);
        }
        else
            hadDisableFailure = true;
    }
    if (!changed)
        return ResetResult::PartialDisable;

    Sleep(2000);
    bool hadRecoveryFailure = false;
    bool hadEnableFailure = false;
    for (auto& adapter : adapters)
    {
        if (!adapter.disabledSuccessfully) continue;
        const bool recovered = reset::Recover(adapter.disabledSuccessfully,
            [&]
            {
                ++adapter.enableAttempts;
                adapter.lastLocate = CM_Locate_DevNodeW(&adapter.inst,
                    adapter.instanceId.data(), CM_LOCATE_DEVNODE_NORMAL);
                adapter.lastEnable = adapter.lastLocate == CR_SUCCESS ?
                    CM_Enable_DevNode(adapter.inst, 0) : adapter.lastLocate;
                LogInfo(L"Enable request: " + adapter.instanceId +
                    L"; attempt=" + std::to_wstring(adapter.enableAttempts) +
                    L"; result=" + ConfigRetToString(adapter.lastEnable));
                return adapter.lastEnable == CR_SUCCESS;
            },
            [&] { return VerifyStarted(adapter); },
            [](unsigned milliseconds) { Sleep(milliseconds); });
        LogOutcome(adapter, recovered ? L"Verified recovered" : L"UNRESOLVED recovery", started);
        if (!recovered)
        {
            hadRecoveryFailure = true;
            if (adapter.lastEnable != CR_SUCCESS) hadEnableFailure = true;
        }
    }
    if (hadRecoveryFailure)
    {
        LogError(L"Reset incomplete: one or more devices remain unresolved; see per-device outcomes");
        return hadEnableFailure ? ResetResult::EnableFailed : ResetResult::VerifyFailed;
    }
    LogInfo(L"Reset finished; elapsedMs=" + std::to_wstring(GetTickCount64() - started));
    return hadDisableFailure ? ResetResult::PartialDisable : ResetResult::Success;
}

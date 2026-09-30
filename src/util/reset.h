#pragma once

enum class ResetResult : int
{
    Success           = 0,
    EnumerationFailed = 1,
    NoAdaptersFound   = 2,
    PartialDisable    = 3,
    EnableFailed      = 4,
    VerifyFailed      = 5
};

// Returns a human-readable string representation of a ResetResult code.
const wchar_t* ResetResultToString(ResetResult res);

// Performs a full disable/enable cycle on targeted PCI display adapters,
// skipping known virtual and software display devices.
// Must be called from an elevated process.
// Verifies via CM_Get_DevNode_Status that successfully cycled adapters are started with no problem code.
// Blocks until all target adapters are processed and verified.
ResetResult CycleAllDisplayAdapters();

#include "reset_lock.h"

namespace reset
{
Lock::Lock(const wchar_t* name)
{
    handle = CreateMutexW(nullptr, FALSE, name);
    if (!handle) return;
    const DWORD result = WaitForSingleObject(handle, 0);
    if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED)
        status = LockStatus::Acquired;
    else
    {
        status = result == WAIT_TIMEOUT ? LockStatus::Busy : LockStatus::Unavailable;
        CloseHandle(handle);
        handle = nullptr;
    }
}

Lock::~Lock()
{
    if (handle)
    {
        ReleaseMutex(handle);
        CloseHandle(handle);
    }
}

bool Active(const wchar_t* name)
{
    HANDLE handle = OpenMutexW(SYNCHRONIZE, FALSE, name);
    if (handle)
    {
        CloseHandle(handle);
        return true;
    }
    return GetLastError() != ERROR_FILE_NOT_FOUND;
}
}

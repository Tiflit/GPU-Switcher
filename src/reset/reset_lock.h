#pragma once

#include <windows.h>

namespace reset
{
inline constexpr wchar_t kLockName[] = L"Global\\Tiflit.GPUSwitcher.DisplayReset.v1";
enum class LockStatus { Acquired, Busy, Unavailable };

class Lock
{
public:
    explicit Lock(const wchar_t* name = kLockName);
    ~Lock();
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    LockStatus Status() const { return status; }
private:
    HANDLE handle = nullptr;
    LockStatus status = LockStatus::Unavailable;
};

// A helper holds the object for its whole operation. Failure to inspect an
// existing object is treated conservatively as busy, including cross-account UAC.
bool Active(const wchar_t* name = kLockName);
}

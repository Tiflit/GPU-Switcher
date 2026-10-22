#include "reset/reset_lock.h"
#include <iostream>
#include <string>
#include <cstdlib>

static void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

static DWORD ChildResult(const std::wstring& name)
{
    wchar_t executable[32768] = {};
    const DWORD length = GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    Require(length && length < ARRAYSIZE(executable), "resolve test executable");
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --try-lock \"" + name + L"\"";
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    Require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, 0,
        nullptr, nullptr, &startup, &process) != FALSE, "launch competing process");
    CloseHandle(process.hThread);
    const DWORD wait = WaitForSingleObject(process.hProcess, 5000);
    if (wait != WAIT_OBJECT_0)
    {
        TerminateProcess(process.hProcess, 99); // Test child only; never used on a reset helper.
        CloseHandle(process.hProcess);
        Require(false, "competing test process did not finish");
    }
    DWORD result = 99;
    Require(GetExitCodeProcess(process.hProcess, &result) != FALSE, "read competing process result");
    CloseHandle(process.hProcess);
    return result;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc == 3 && std::wstring(argv[1]) == L"--try-lock")
    {
        reset::Lock lock(argv[2]);
        return lock.Status() == reset::LockStatus::Acquired ? 0 :
            (lock.Status() == reset::LockStatus::Busy ? 42 : 43);
    }
    const std::wstring name = L"Global\\Tiflit.GPUSwitcher.TestReset." + std::to_wstring(GetCurrentProcessId());
    Require(!reset::Active(name.c_str()), "test lock starts absent");
    {
        reset::Lock lock(name.c_str());
        Require(lock.Status() == reset::LockStatus::Acquired, "first process obtains lock");
        Require(reset::Active(name.c_str()), "running helper can be detected");
        Require(ChildResult(name) == 42, "second process cannot enter device reset");
    }
    Require(!reset::Active(name.c_str()), "lock disappears on helper completion");
    Require(ChildResult(name) == 0, "subsequent helper can acquire after completion");

    HANDLE collision = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
    Require(collision != nullptr, "create wrong-type object for failure test");
    Require(reset::Active(name.c_str()), "inspection error conservatively blocks acquisition");
    Require(ChildResult(name) == 43, "lock failure cannot enter device reset");
    CloseHandle(collision);
    std::cout << "Cross-process reset exclusion passed\n";
    return 0;
}

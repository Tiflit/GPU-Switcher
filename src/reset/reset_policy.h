#pragma once

#include <cstdint>

namespace reset
{
enum class WaitState { Running, Complete, Failed };
enum class WatchEvent { None, TimedOut, WaitFailed, Completed };

class Watch
{
public:
    void Start(std::uint64_t now)
    {
        started = now;
        active = true;
        timedOut = false;
        waitFailed = false;
    }
    bool Active() const { return active; }
    bool TimedOut() const { return timedOut; }
    WatchEvent Observe(WaitState state, std::uint64_t now)
    {
        if (!active) return WatchEvent::None;
        if (state == WaitState::Complete)
        {
            active = false;
            return WatchEvent::Completed;
        }
        if (now - started >= 60000 && !timedOut)
        {
            timedOut = true;
            return WatchEvent::TimedOut;
        }
        if (state == WaitState::Failed && !waitFailed)
        {
            waitFailed = true;
            return WatchEvent::WaitFailed;
        }
        return WatchEvent::None;
    }
private:
    std::uint64_t started = 0;
    bool active = false;
    bool timedOut = false;
    bool waitFailed = false;
};

struct InitialState
{
    bool known = false;
    bool disabled = false;
    bool started = false;
};

inline bool Eligible(const InitialState& state)
{
    return state.known && !state.disabled && state.started;
}

// Only a device successfully disabled by this operation may be re-enabled.
// Each enable/status callback resolves the current device node independently.
template <typename Enable, typename Verify, typename Delay>
bool Recover(bool disabledByUs, Enable enable, Verify verify, Delay delay)
{
    if (!disabledByUs) return false;
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        if (attempt != 0) delay(1000);
        if (enable()) break;
    }
    if (verify()) return true;
    // A final bounded recovery pass also covers an enable request that succeeded
    // but never produced a started device during the first verification window.
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        delay(1000);
        enable();
        if (verify()) return true;
    }
    return false;
}
}

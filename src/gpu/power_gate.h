#pragma once

#include <cstdint>

namespace gpu
{
// Power transitions are independent of device/reset state. A health scan must
// not interpret the intentionally released device as a recovery opportunity.
class PowerGate
{
public:
    void Suspend() { phase = Phase::Suspended; }
    bool Resume(std::uint64_t now)
    {
        if (phase == Phase::ResumeDelay) return false;
        phase = Phase::ResumeDelay;
        resumedAt = now;
        return true;
    }
    bool CanAcquire() const { return phase == Phase::Ready; }
    bool AwaitingResume() const { return phase == Phase::ResumeDelay; }
    unsigned RemainingDelay(std::uint64_t now) const
    {
        const auto elapsed = now - resumedAt;
        return elapsed >= 1500 ? 1 : static_cast<unsigned>(1500 - elapsed);
    }
    bool CompleteResume(std::uint64_t now)
    {
        if (!AwaitingResume() || now - resumedAt < 1500) return false;
        phase = Phase::Ready;
        return true;
    }
private:
    enum class Phase { Ready, Suspended, ResumeDelay };
    Phase phase = Phase::Ready;
    std::uint64_t resumedAt = 0;
};
}

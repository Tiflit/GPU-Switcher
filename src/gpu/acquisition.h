#pragma once

#include <utility>

namespace gpu
{
struct CandidatePriority
{
    bool preferred;
    bool current;
    int score;
};

inline bool HigherPriority(const CandidatePriority& left, const CandidatePriority& right)
{
    const int leftTier = left.preferred ? 0 : (left.current ? 1 : 2);
    const int rightTier = right.preferred ? 0 : (right.current ? 1 : 2);
    return leftTier != rightTier ? leftTier < rightTier : left.score > right.score;
}

enum class AcquisitionResult { Replaced, Retained, Failed };

// Creation returns an optional pending resource. Only a successful creation may
// commit it; a healthy current device remains usable throughout failed attempts.
template <typename Candidates, typename IsHealthy, typename IsCurrent, typename Create, typename Commit>
AcquisitionResult AcquireInOrder(const Candidates& candidates, IsHealthy currentHealthy,
    IsCurrent isCurrent, Create create, Commit commit)
{
    for (const auto& candidate : candidates)
    {
        if (currentHealthy() && isCurrent(candidate))
            return AcquisitionResult::Retained;

        auto pending = create(candidate);
        if (pending)
        {
            commit(candidate, std::move(*pending));
            return AcquisitionResult::Replaced;
        }
    }
    return currentHealthy() ? AcquisitionResult::Retained : AcquisitionResult::Failed;
}
}

#include "gpu/acquisition.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

static void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct Candidate
{
    int id;
    gpu::CandidatePriority priority;
};

static void TestOrdering()
{
    std::vector<Candidate> candidates{
        { 1, { false, false, 900 } }, { 2, { false, true, 10 } },
        { 3, { true, false, 1 } }, { 4, { false, false, 900 } },
        { 5, { false, false, 100 } } };
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const Candidate& left, const Candidate& right)
        { return gpu::HigherPriority(left.priority, right.priority); });
    std::vector<int> order;
    for (const auto& candidate : candidates)
        order.push_back(candidate.id);
    Require(order == std::vector<int>({ 3, 2, 1, 4, 5 }),
        "preferred, current, score, then stable tie order");
}

struct Scenario
{
    std::unique_ptr<int> active;
    bool healthy = false;
    bool loseCurrentDuringAttempt = false;
    std::vector<int> attempted;
    std::vector<int> succeeding;
    int commits = 0;

    gpu::AcquisitionResult Run(const std::vector<int>& candidates)
    {
        const int* original = active.get();
        return gpu::AcquireInOrder(candidates,
            [&] { return active && healthy; },
            [&](int candidate) { return active && candidate == *active; },
            [&](int candidate) -> std::optional<std::unique_ptr<int>>
            {
                // Every creation attempt must see the original resource intact.
                Require(active.get() == original, "active resource released before creation succeeded");
                attempted.push_back(candidate);
                if (loseCurrentDuringAttempt)
                    healthy = false;
                if (std::find(succeeding.begin(), succeeding.end(), candidate) == succeeding.end())
                    return std::nullopt;
                return std::optional<std::unique_ptr<int>>{ std::make_unique<int>(candidate) };
            },
            [&](int candidate, std::unique_ptr<int>&& pending)
            {
                Require(pending && *pending == candidate, "commit must receive created resource");
                Require(active.get() == original, "old resource must survive until commit");
                active = std::move(pending);
                healthy = true;
                ++commits;
            });
    }
};

int main()
{
    using Result = gpu::AcquisitionResult;
    TestOrdering();

    Scenario replacement;
    replacement.active = std::make_unique<int>(2);
    replacement.healthy = true;
    replacement.succeeding = { 1 };
    Require(replacement.Run({ 1, 2, 3 }) == Result::Replaced, "preferred replacement succeeds");
    Require(*replacement.active == 1 && replacement.commits == 1,
        "successful replacement commits once");

    Scenario retained;
    retained.active = std::make_unique<int>(2);
    retained.healthy = true;
    retained.succeeding = { 3 };
    Require(retained.Run({ 1, 2, 3 }) == Result::Retained, "failed preferred preserves fallback");
    Require(*retained.active == 2 && retained.commits == 0 && retained.attempted == std::vector<int>({ 1 }),
        "healthy fallback is neither recreated nor displaced by a lower candidate");

    Scenario next;
    next.succeeding = { 2 };
    Require(next.Run({ 1, 2, 3 }) == Result::Replaced, "next candidate succeeds without a current device");
    Require(*next.active == 2 && next.attempted == std::vector<int>({ 1, 2 }), "candidate retry order");

    Scenario allFailed;
    Require(allFailed.Run({ 1, 2 }) == Result::Failed, "all candidates fail without current device");
    Require(!allFailed.active && allFailed.commits == 0, "failures never commit");

    Scenario absent;
    absent.active = std::make_unique<int>(7);
    absent.healthy = true;
    Require(absent.Run({ 1, 2 }) == Result::Retained, "healthy device survives enumeration omission");
    Require(*absent.active == 7 && absent.commits == 0, "all failed attempts preserve old device");

    Scenario lost;
    lost.active = std::make_unique<int>(2);
    lost.succeeding = { 3 };
    Require(lost.Run({ 1, 2, 3 }) == Result::Replaced, "unhealthy current device does not stop retries");
    Require(*lost.active == 3 && lost.attempted == std::vector<int>({ 1, 2, 3 }), "lost-device retry sequence");

    Scenario becameLost;
    becameLost.active = std::make_unique<int>(2);
    becameLost.healthy = true;
    becameLost.loseCurrentDuringAttempt = true;
    becameLost.succeeding = { 3 };
    Require(becameLost.Run({ 1, 2, 3 }) == Result::Replaced,
        "health is rechecked after failed preferred creation");
    Require(*becameLost.active == 3, "device lost during creation is not retained");

    Scenario empty;
    Require(empty.Run({}) == Result::Failed, "empty candidate list without device fails");
    empty.active = std::make_unique<int>(2);
    empty.healthy = true;
    Require(empty.Run({}) == Result::Retained, "empty list retains healthy device");
    Require(empty.Run({ 2 }) == Result::Retained && empty.attempted.empty(),
        "already active preferred device needs no creation");

    std::cout << "Acquisition regression scenarios passed\n";
    return 0;
}

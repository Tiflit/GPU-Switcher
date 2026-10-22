#include "reset/reset_policy.h"
#include <cstdlib>
#include <iostream>

static void Require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    using namespace reset;
    Watch watch;
    Require(!watch.Active(), "initially inactive");
    watch.Start(100);
    Require(watch.Observe(WaitState::Running, 60099) == WatchEvent::None, "deadline not reached");
    Require(watch.Observe(WaitState::Running, 60100) == WatchEvent::TimedOut, "timeout notification");
    Require(watch.Active(), "timeout cannot release reset exclusion");
    Require(watch.Observe(WaitState::Running, 70100) == WatchEvent::None, "timeout reported once");
    Require(watch.Observe(WaitState::Complete, 70101) == WatchEvent::Completed, "eventual completion after timeout");
    Require(!watch.Active(), "only completion releases supervision");
    watch.Start(200);
    Require(!watch.TimedOut(), "new operation clears previous timeout");
    Require(watch.Observe(WaitState::Failed, 201) == WatchEvent::WaitFailed, "wait failure notification");
    Require(watch.Active(), "wait failure cannot permit acquisition or a new reset");
    Require(watch.Observe(WaitState::Failed, 202) == WatchEvent::None, "wait failure notification is bounded");
    Require(watch.Observe(WaitState::Complete, 60300) == WatchEvent::Completed,
        "confirmed completion wins over an overdue deadline");

    Require(Eligible({ true, false, true }), "known started device eligible");
    Require(!Eligible({ true, true, false }), "pre-disabled adapter excluded");
    Require(!Eligible({ false, false, false }), "unreadable initial state excluded");
    Require(!Eligible({ true, false, false }), "initially stopped device excluded");

    int enables = 0, verifies = 0, delays = 0;
    const auto delay = [&](unsigned ms) { Require(ms == 1000, "bounded retry delay"); ++delays; };
    Require(!Recover(false, [&] { ++enables; return true; }, [&] { ++verifies; return true; }, delay),
        "untouched device cannot enter recovery");
    Require(enables == 0 && verifies == 0 && delays == 0, "untouched device produces no enable calls");

    Require(Recover(true, [&] { ++enables; return true; }, [&] { ++verifies; return true; }, delay),
        "ordinary enable and verification succeed");
    Require(enables == 1 && verifies == 1, "ordinary success needs one enable");

    enables = verifies = delays = 0;
    Require(Recover(true, [&] { return ++enables >= 3; }, [&] { ++verifies; return true; }, delay),
        "transient enable failure recovers");
    Require(enables == 3 && verifies == 1 && delays == 2, "transient retry sequence");

    enables = verifies = delays = 0;
    Require(Recover(true, [&] { ++enables; return true; }, [&] { return ++verifies >= 2; }, delay),
        "failed startup verification triggers a fresh enable recovery pass");
    Require(enables == 2 && verifies == 2, "enable success alone does not establish recovery");

    enables = verifies = delays = 0;
    Require(!Recover(true, [&] { ++enables; return false; }, [&] { ++verifies; return false; }, delay),
        "permanent enable/status failure stays unresolved");
    Require(enables == 6 && verifies == 3 && delays == 5, "all retries and recovery are bounded");

    enables = verifies = delays = 0;
    Require(Recover(true, [&] { ++enables; return false; }, [&] { ++verifies; return true; }, delay),
        "verified started state can establish recovery after failed request returns");
    std::cout << "Reset supervision and recovery regression scenarios passed\n";
}

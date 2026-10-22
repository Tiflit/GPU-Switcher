#include "gpu/power_gate.h"
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
    gpu::PowerGate gate;
    Require(gate.CanAcquire(), "ordinary startup permits acquisition");
    gate.Suspend();
    Require(!gate.CanAcquire(), "health scan after suspend cannot acquire");
    Require(!gate.CompleteResume(100000), "stale resume timer cannot end suspend");
    Require(gate.Resume(100000), "resume starts one delay");
    Require(!gate.CanAcquire(), "health/devnode recovery stays blocked during delay");
    Require(gate.RemainingDelay(100500) == 1000, "rearming retains original deadline");
    Require(!gate.Resume(101000), "second Windows resume notification is coalesced");
    Require(!gate.CompleteResume(101499), "early queued timer cannot acquire");
    Require(gate.CompleteResume(101500), "delay completes at original deadline");
    Require(gate.CanAcquire(), "timer completion permits recovery");
    Require(!gate.CompleteResume(101501), "duplicate timer cannot complete twice");

    gate.Suspend();
    gate.Resume(200000);
    gate.Suspend();
    Require(!gate.CompleteResume(202000) && !gate.CanAcquire(),
        "second suspend invalidates a queued resume timer");
    gate.Resume(203000);
    Require(gate.CompleteResume(204500), "next real resume can recover");

    gate.Suspend();
    gate.Resume(300000);
    // Reset may cancel the UI timer. A cancellation/failure recovery attempt
    // must rearm it, not bypass the power gate or restart the delay.
    Require(!gate.CanAcquire() && gate.AwaitingResume(), "cancelled timer still blocks acquisition");
    Require(gate.RemainingDelay(302000) == 1, "overdue timer can be rearmed promptly");
    Require(gate.CompleteResume(302001), "rearmed timer unblocks recovery");

    gpu::PowerGate timerFailure;
    timerFailure.Suspend();
    timerFailure.Resume(400000);
    // No resume timer is delivered. The health check uses the same deadline.
    Require(!timerFailure.CompleteResume(401000) && !timerFailure.CanAcquire(),
        "health fallback cannot acquire before the resume deadline");
    Require(timerFailure.CompleteResume(430000) && timerFailure.CanAcquire(),
        "health fallback recovers when the resume timer was never delivered");
    Require(!timerFailure.CompleteResume(430001), "late resume timer cannot repeat recovery");
    timerFailure.Suspend();
    Require(!timerFailure.CompleteResume(460000), "health fallback cannot end a new suspend");

    gpu::PowerGate missingSuspend;
    Require(missingSuspend.Resume(5000) && !missingSuspend.CanAcquire(),
        "resume without a preceding suspend notification still delays acquisition");
    std::cout << "Power transition regression scenarios passed\n";
}

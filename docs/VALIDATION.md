# 1.2 validation

## Automated checks

The Windows build workflow compiles the application and runs acquisition, reset
policy, power-transition, and cross-process reset-lock regressions with CTest. The tests use the same ordering and replacement helper
as the application, with simulated resources and creation failures.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The acquisition, reset-policy, and power-transition tests are independent of Windows headers:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc tests/acquisition_tests.cpp -o acquisition_tests
./acquisition_tests
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc tests/reset_policy_tests.cpp -o reset_policy_tests
./reset_policy_tests
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc tests/power_gate_tests.cpp -o power_gate_tests
./power_gate_tests
```

Coverage includes preferred/current/score ordering, equal-score ties, successful
replacement, failed preferred creation retaining the current device, subsequent
candidate attempts when no healthy device exists, all candidates failing, a
healthy device omitted from enumeration, and loss of the current device during
a preferred acquisition attempt. These tests do not exercise DXGI, driver calls,
MUX routing, suspend/resume delivery, or physical adapter resets.

The vendor and memory classification thresholds remain heuristics. Passing the
acquisition tests does not establish that those thresholds classify all GPUs
correctly.

Reset-policy tests cover timeout followed by completion, wait failure retaining
exclusion, initial-state eligibility, refusing to enable untouched devices,
transient enable failure, failed startup verification followed by recovery, and
bounded permanent failure. Windows lock tests launch separate test processes to
check rejection while locked, admission after release, and conservative failure
when the mutex name is occupied by a different object type. No test calls the
physical device-reset APIs. Cross-account UAC and cross-session behavior still
require manual validation.

Power-transition tests cover acquisition exclusion while suspended, the resume
delay, duplicate resume notifications, stale/early timers, another suspend during
a pending wake, and rearming a resume timer cancelled by reset. The gate is used
by the actual acquisition path; the tests do not simulate Windows message delivery.

## Recorded hardware baseline

See [Legion results for build e147067](LEGION-VALIDATION-2026-10-22.md). The report separately records the successful 1893b60 power-gate retest and an
unresolved report of LockApp retaining NVIDIA activity after Exit. Panel return
to Intel and complete dGPU inactivity are distinct results.

## Legion hardware checks

Use the Windows CI artifact for the exact commit under test. Record the commit,
Windows build, graphics driver versions, power source, and display connections.
Use **View Log** and save the log after each independent test from
`%LOCALAPPDATA%\GPU-Switcher\gpu_switcher.log` before its rolling limit discards it.

1. **Cold launch:** with the documented NVIDIA profile configured, launch the
   utility. On the Legion with Intel integrated graphics and an RTX 4060, expect
   the NVIDIA icon and model. Check actual panel routing separately in NVIDIA's
   controls; the tray icon reports the held device. Verify readable UTF-8 log
   output, including non-ASCII characters when present.
2. **Duplicate launch and exit:** a second launch should notify the existing
   instance. Exit should remove the icon and release the held device.
3. **Sleep/resume:** test independently of reset. After resume, confirm device
   reacquisition, icon state, and actual routing. Save the log.
4. **Cancel reset:** select Restart Display Adapters and cancel UAC. Confirm the
   utility reacquires its device and remains usable.
5. **Display reset:** with work saved and graphics workloads closed, request a
   reset. Success should verify every successfully cycled target and relaunch
   the utility. Partial/failure status should keep the tray instance alive and
   report the incomplete reset. Save the log; do not infer success from the icon
   alone.
6. **Resume followed by reset:** verify from the log that a resume timer does not
   create a D3D11 device while the helper is resetting adapters. A timer already
   queued when reset starts must also be ignored.

Real multi-dGPU/eGPU fallback validation requires additional hardware. The
automated regressions simulate failed preferred creation; a single-dGPU Legion
cannot validate the complete physical fallback scenario.

7. **Slow helper / reopening:** do not manufacture a hardware hang. The automated
   policy tests cover the 60-second warning and eventual completion. If a real
   operation runs long, verify that the warning appears once, the tooltip stays
   in the waiting state, Exit and another reset remain blocked, and completion
   is still handled. If the tray was forcibly terminated and reopened, it should
   detect the active helper lock and wait; the external helper's final result is
   unknown to that new instance and must not be reported as verified success.
8. **Recovery diagnostics:** check the per-device initial state, disable result,
   enable attempts, final locate/status CONFIGRET, problem code, and elapsed time.
   Preserve deliberately disabled devices when testing on a suitable setup; do
   not disable the Legion's panel-driving adapter merely to exercise this case.

## Remaining limits

The helper can still fail to restore an adapter, or be forcibly terminated. There
is no persistent recovery journal for a terminated helper. Recovery is bounded;
an unresolved device is reported, not guaranteed repaired. Logging across two
processes or different elevation accounts remains a follow-up (View Log opens
only the current account's log).

A default mutex security descriptor is used; lock access/inspection errors block
operations rather than bypassing exclusion. This is conservative handling, not
proof of compatibility across every account/session arrangement. Device startup
verification does not guarantee application-level rendering or display routing
has recovered.

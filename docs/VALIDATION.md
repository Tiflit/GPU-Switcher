# 1.2 validation

## Automated checks

The Windows build workflow compiles the application and runs the acquisition
regressions with CTest. The tests use the same ordering and replacement helper
as the application, with simulated resources and creation failures.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The acquisition tests are also independent of Windows headers:

```sh
g++ -std=c++17 -Wall -Wextra -Werror -pedantic -Isrc tests/acquisition_tests.cpp -o acquisition_tests
./acquisition_tests
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

## Legion hardware checks

Use the Windows CI artifact for the exact commit under test. Record the commit,
Windows build, graphics driver versions, power source, and display connections.
Save the log after each independent test from
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

## Remaining limits

Helper timeouts, concurrent reset-helper prevention, recovery after failed
re-enable, and cross-process/cross-account logging remain separate follow-ups.
Device startup verification does not guarantee application-level rendering or
display routing has recovered.

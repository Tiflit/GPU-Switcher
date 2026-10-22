# Legion validation — 2026-10-22

## Tested configuration and evidence

- Lenovo Legion Slim 7 16IRH8, Intel Iris Xe integrated graphics and NVIDIA
  GeForce RTX 4060 Laptop GPU.
- Executable identified by the supplied logs as version 1.2.0, build
  `e1470674e237` (full commit `e1470674e2375df3bc483c99c8b5d813f4ca1c7e`).
- Windows build, graphics driver versions, power source, and external display
  connections were not recorded in the report; do not infer them.
- Evidence: user reports and successive rolling-log snapshots named
  `gpu_switcher.log`, `gpu_switcher (2).log`, `gpu_switcher (3).log`, and
  `gpu_switcher(1).log` through `gpu_switcher(4).log` supplied in the development
  conversation. Raw logs are not included here. Times below are copied from the
  logs without timezone conversion.

The logs establish D3D11 acquisition, lifecycle transitions, and Configuration
Manager outcomes. Panel routing and observed GPU activity are user observations;
the application does not independently measure the panel MUX route.

## Results

| Scenario | Evidence and result |
| --- | --- |
| Adapter selection | NVIDIA selected; Intel 128 MB adapter excluded from discrete candidates. |
| Duplicate launch | Existing tray instance notified; user saw its status notification. |
| Ordinary exit | Clean exit logged; user observed NVIDIA become inactive. |
| Sleep/resume | Device released and reacquired; tray survived. |
| Cancel UAC | Acquisition recovered after cancellation; panel routing stayed unchanged. |
| Accepted reset, 12:19 | Both adapters enabled on first requests and verified started with problem 0; helper elapsed 7171 ms, then tray relaunched. |
| Accepted reset, 12:33 | Both adapters enabled on first requests and verified started with problem 0; helper elapsed 6875 ms, then tray relaunched. User observed NVIDIA panel routing after relaunch and Intel routing after Exit. |
| Final launch/resume/exit | 12:38:17 process start; 12:38:25 device active; 12:38:31 suspend release; 12:38:42 device active after resume; 12:38:59 clean exit. User reported the complete sequence working. |

## NVIDIA profile clarification

Initially, the new executable activated the NVIDIA GPU without switching the
panel. The user removed and recreated its NVIDIA App program configuration and
set **Automatic Display Switching** to **On**.

The user subsequently clarified that the first ordinary launch after this change
already switched the panel successfully, before the accepted reset test. The
adapter reset must therefore **not** be documented as a prerequisite for applying
the profile. The evidence points to profile configuration as the explanation for
the initially absent switching; it does not establish the driver's internal cause
or a universal remedy for other systems.

## Additional issue found in the logs

In the earlier resume at 12:32:57–12:33:00, a periodic scan acquired the device
before resume notifications were processed. Those notifications released it,
and the resume timer acquired it again. This unnecessary churn did not recur
in the final 12:38 test, but the observed ordering motivates a power-transition
guard: ordinary acquisition is blocked from suspend notification until the
resume delay completes. Duplicate resume notifications share one delay.

That guard is a **later change**, not part of the hardware-validated build above.
Its automated regressions do not replace another Legion sleep/resume check.

## Scope and remaining validation

These results cover successful paths on one laptop and the tested profile. They
do not establish universal compatibility or failure recovery. Timeout UI,
concurrent production reset helpers, cross-account/cross-session elevation,
pre-disabled adapter preservation, permanent enable failure, and real multi-GPU
fallback were not manually exercised. Relevant policy and lock regression tests
run in CI without touching physical devices.

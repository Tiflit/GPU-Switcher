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
  `gpu_switcher(1).log` through `gpu_switcher(8).log` supplied in the development
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

That guard is a **later change**, not part of the e147067 baseline above.
The subsequent 1893b60 hardware check is recorded below.

## Power-gate retest: build 1893b6083278

The user tested the new artifact without performing a reset afterward. The
additional snapshot `gpu_switcher(5).log` identifies the executable as
`1893b6083278` (full commit `1893b6083278b1b3857bb4b21eb6bfbee813fe6d`).

| Time | Log evidence |
| --- | --- |
| 12:49:37 | New build started. |
| 12:49:45 | NVIDIA D3D11 device active. |
| 12:50:21 | Suspend notification; device released. |
| 12:50:31 | One resume scheduling entry. |
| 12:50:32 | One delayed reacquisition; device active again. |
| 12:50:50 | Release transition and clean exit. |

The user reported successful launch, sleep/resume, and panel return to Intel on
Exit. There is no periodic-scan acquisition before the resume timer in this
cycle. This validates that successful path on this laptop, not every possible
Windows event ordering.

## LockApp and intended reset semantics — user clarification

The user observed `lockapp.exe` using NVIDIA after sleep/resume and ordinary
Exit, while the panel returned to Intel. This happened on both tested builds.
The pre-update reset at 12:41 succeeded in 6953 ms, with both adapters verified
after their first enable requests. Resetting then exiting cleared NVIDIA activity
in the user's test.

The user clarified that this is an expected Windows client behavior when NVIDIA
has driven the display, unrelated to a GPU-Switcher defect. Clearing such clients
was the original purpose of the explicit **Restart Display Adapters** command.
Ordinary Exit releases only GPU-Switcher's own context. No further independent
LockApp investigation is required for this task.

The intended reset sequence is: release GPU-Switcher's device, run the elevated
adapter cycle, verify recovery, then exit GPU-Switcher **without relaunching**.
The earlier tested builds instead relaunched automatically after successful
reset; those historical observations above remain accurate, but they do not
match the original intended behavior. Automatic relaunch entered the repository
in commit `65f60ab` (before the current development session), and was carried into
this draft. The subsequent correction removes it on verified success.

Partial or failed resets retain supervision/recovery and a visible warning;
UAC cancellation retains the previous reacquisition behavior. Clearing existing
contexts cannot prevent another application from acquiring the GPU later.

## Reset-to-exit retest: build 8fe69f0d61d2

The user tested the corrected artifact and reported it working as intended.
The supplied `gpu_switcher(6).log` identifies build `8fe69f0d61d2` (full commit
`8fe69f0d61d280e8e2ffe7b9f12f32a496172b38`).

| Time | Log evidence |
| --- | --- |
| 13:11:18–13:11:26 | Process started and acquired the NVIDIA D3D11 device. |
| 13:11:35 | Entered Resetting and requested Restart Display Adapters. |
| 13:11:37 | Elevated helper started; both adapters were initially started and enabled. |
| 13:11:41 | Intel recovered after its first enable request, with problem 0. |
| 13:11:44 | NVIDIA recovered after its first enable request, with problem 0; helper reported Success after 6547 ms. |
| 13:11:46 | Parent explicitly logged exit without relaunch, followed by clean exit. |

No subsequent startup appears in this snapshot. Together with the user's
confirmation, this passes the corrected successful reset-to-exit scenario on
this laptop. The log establishes reset recovery and process exit; final display
routing and GPU inactivity remain hardware observations.

## Final candidate smoke checks: build fe1e2a49187a

The final tested executable came from PR #5's merge build `fe1e2a49187a`.
Its tree is identical to source commit `772d591597ef12d1f985c1f1a454772bdf7a6278`.
PR #5 was subsequently merged as `be7c45d38602fe498347d62a9a9a7948a1755ef3`.
The main-branch Windows build and all four CTest suites passed for that merge
([run 39832853555](https://github.com/Tiflit/GPU-Switcher/actions/runs/39832853555)).

| Scenario | Evidence and result |
| --- | --- |
| Startup | `gpu_switcher(7).log`: started 14:29:58, NVIDIA device active 14:30:06; user reported successful switching. |
| Sleep/resume | Suspend released the device at 14:30:27; resume scheduled at 14:30:37; one delayed acquisition completed at 14:30:39. |
| Reset-to-exit | Reset requested 14:30:57; both adapters recovered after first enable requests with problem 0; Success in 7094 ms; clean exit without relaunch at 14:31:09. |
| Windows Explorer restart | User separately restarted Windows Explorer and confirmed that the tray icon survived. This observation is not explicitly logged. |
| Ordinary Exit after Explorer restart | `gpu_switcher(8).log`: started 14:32:59, device active 14:33:05, device release and clean exit 14:33:37. |
| Icon transitions | User visually confirmed brief yellow during startup/display switching, followed by NVIDIA green. Yellow denotes a non-Active application state; green denotes an active held NVIDIA device, not a measured panel route. |

These smoke checks cover the updated startup order and normal tray restoration.
They do not inject tray registration or timer allocation failures. The missing
resume-timer deadline fallback is covered by portable policy regressions; its
Win32 integration was reviewed in source.

## Scope and remaining validation

These results cover successful paths on one laptop and the tested profile. They
do not establish universal compatibility or failure recovery. Timeout UI,
concurrent production reset helpers, cross-account/cross-session elevation,
pre-disabled adapter preservation, permanent enable failure, and real multi-GPU
fallback were not manually exercised. Relevant policy and lock regression tests
run in CI without touching physical devices.

cmake_minimum_required(VERSION 3.20)
project(gpu_tray LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# ── Sources ──────────────────────────────────────────────────────────────────
add_executable(gpu_tray WIN32
    main.cpp
)

# ── Windows subsystem + manifest for high-DPI tray icons ────────────────────
target_compile_definitions(gpu_tray PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# ── Link only what we use ────────────────────────────────────────────────────
# nvapi64.lib is NOT linked here — we load nvapi64.dll at runtime.
target_link_libraries(gpu_tray PRIVATE
    shell32      # Shell_NotifyIcon
    user32       # CreateWindow, GetMessage, etc.
    gdi32        # CreateDIBSection, Ellipse, etc.
    d3d11        # Not strictly required now, kept for future DXGI GPU enumeration
)

# ── MSVC flags ───────────────────────────────────────────────────────────────
if(MSVC)
    # /W4 warnings, treat as errors in CI; /subsystem:windows already set by WIN32
    target_compile_options(gpu_tray PRIVATE /W4 /WX /permissive-)
    # Embed a manifest so Windows knows this is a per-monitor DPI-aware app
    # (higher DPI → sharper 16-px tray icon on HiDPI displays)
    set_target_properties(gpu_tray PROPERTIES
        LINK_FLAGS "/MANIFEST:EMBED /MANIFESTINPUT:\"${CMAKE_CURRENT_SOURCE_DIR}/gpu_tray.manifest\""
    )
endif()

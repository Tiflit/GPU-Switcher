cmake_minimum_required(VERSION 3.20)
project(gpu_tray LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# ── Sources ───────────────────────────────────────────────────────────────────
add_executable(gpu_tray WIN32
    main.cpp
)

# ── Preprocessor definitions ──────────────────────────────────────────────────
target_compile_definitions(gpu_tray PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# ── Link libraries ────────────────────────────────────────────────────────────
# nvapi64.lib is NOT linked — we load nvapi64.dll at runtime.
target_link_libraries(gpu_tray PRIVATE
    shell32   # Shell_NotifyIcon
    user32    # CreateWindow, GetMessage, etc.
    gdi32     # CreateDIBSection, Ellipse, etc.
)

# ── MSVC flags + manifest ─────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(gpu_tray PRIVATE /W4 /WX /permissive-)

    # Embed manifest post-build.
    # The semicolon in -outputresource:<file>;1 must be escaped as \; in CMake
    # otherwise CMake splits it into two separate arguments and mt.exe errors.
    add_custom_command(TARGET gpu_tray POST_BUILD
        COMMAND mt.exe -nologo
            -manifest "${CMAKE_CURRENT_SOURCE_DIR}/gpu_tray.manifest"
            "-outputresource:$<TARGET_FILE:gpu_tray>\\;1"
        COMMENT "Embedding manifest into gpu_tray.exe"
    )
endif()

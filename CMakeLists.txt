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
# WMI requires wbemuuid + ole32 + oleaut32.
# No nvapi64.lib — NVAPI approach is no longer used.
target_link_libraries(gpu_tray PRIVATE
    shell32
    user32
    gdi32
    ole32
    oleaut32
    wbemuuid
)

# ── MSVC flags + manifest ─────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(gpu_tray PRIVATE /W4 /WX /permissive-)

    add_custom_command(TARGET gpu_tray POST_BUILD
        COMMAND powershell -NoProfile -NonInteractive -Command
            "mt.exe -nologo -manifest '${CMAKE_CURRENT_SOURCE_DIR}/gpu_tray.manifest' \"-outputresource:$<TARGET_FILE:gpu_tray>;1\""
        COMMENT "Embedding manifest into gpu_tray.exe"
    )
endif()

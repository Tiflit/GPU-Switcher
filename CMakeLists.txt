cmake_minimum_required(VERSION 3.20)
project(gpu_tray LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# ───────────────────────────────────────────────────────────────
# GPU-Switcher main executable
# ───────────────────────────────────────────────────────────────
add_executable(gpu_tray WIN32
    main.cpp
)

set_target_properties(gpu_tray PROPERTIES OUTPUT_NAME "GPU-Switcher")

target_compile_definitions(gpu_tray PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(gpu_tray PRIVATE
    shell32
    user32
    gdi32
    ole32
    oleaut32
    wbemuuid
    dxgi
)

# Embed manifest into GPU-Switcher.exe
if(MSVC)
    target_compile_options(gpu_tray PRIVATE /W4 /WX /permissive-)

    add_custom_command(TARGET gpu_tray POST_BUILD
        COMMAND powershell -NoProfile -NonInteractive -Command
            "mt.exe -nologo -manifest '${CMAKE_CURRENT_SOURCE_DIR}/GPU-Switcher.manifest' \"-outputresource:$<TARGET_FILE:gpu_tray>;1\""
        COMMENT "Embedding manifest into GPU-Switcher.exe"
    )
endif()

# ───────────────────────────────────────────────────────────────
# Dummy tray executable (for NVIDIA detection testing)
# ───────────────────────────────────────────────────────────────
add_executable(dummy_tray WIN32
    dummy_tray.cpp
)

target_compile_definitions(dummy_tray PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(dummy_tray PRIVATE
    shell32
    user32
    gdi32
)

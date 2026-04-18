cmake_minimum_required(VERSION 3.20)
project(gpu_tray LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# ───────────────────────────────────────────────────────────────
# GPU-Switcher main executable (tray hook)
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
)

# ───────────────────────────────────────────────────────────────
# Dummy tray executable (optional)
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

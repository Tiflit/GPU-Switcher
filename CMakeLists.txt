cmake_minimum_required(VERSION 3.20)

project(GPU-Switcher LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC warnings + strict mode
add_compile_options(
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

# Windows 10 target
add_definitions(-DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00)

add_executable(GPU-Switcher WIN32
    src/main.cpp
    src/resource.rc

    # GPU
    src/gpu/gpu_detection.cpp
    src/gpu/gpu_vendor.h
    src/gpu/gpu_state.h

    # Tray icon loader
    src/tray/tray_icon.cpp
    src/tray/tray_icon.h

    # Utilities
    src/util/logging.cpp
    src/util/startup.cpp

    # Windows helpers
    src/win/win_helpers.cpp
    src/win/win_helpers.h
)

set_target_properties(GPU-Switcher PROPERTIES
    OUTPUT_NAME "GPU-Switcher"
)

# Add src/ as include root
target_include_directories(GPU-Switcher PRIVATE src)

# Preprocessor definitions
target_compile_definitions(GPU-Switcher PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# Required Windows + DXGI + D3D11 libs
target_link_libraries(GPU-Switcher PRIVATE
    shell32
    user32
    gdi32
    advapi32
    d3d11
    dxgi
)

cmake_minimum_required(VERSION 3.20)

project(gpu_switcher LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# High warning level and strict mode
add_compile_options(
    $<$<C_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /utf-8>
)

# Windows 10+ target
add_definitions(-DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00)

add_executable(gpu_switcher WIN32
    main.cpp
    resource.rc
)

set_target_properties(gpu_switcher PROPERTIES
    OUTPUT_NAME "GPU-Switcher"
)

target_compile_definitions(gpu_switcher PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(gpu_switcher PRIVATE
    shell32
    user32
    gdi32
    advapi32
    d3d11
    dxgi
)

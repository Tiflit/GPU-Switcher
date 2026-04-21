cmake_minimum_required(VERSION 3.20)
project(GPU-Switcher LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

add_definitions(-DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00)

# ---------------------------------------------------------
# Add ALL source files, including the new reset.cpp
# ---------------------------------------------------------
add_executable(GPU-Switcher WIN32
    src/main.cpp
    src/resource.rc
    src/util/logging.cpp
    src/util/startup.cpp
    src/util/reset.cpp        # <-- REQUIRED for CycleAllDisplayAdapters()
)

set_target_properties(GPU-Switcher PROPERTIES OUTPUT_NAME "GPU-Switcher")

target_include_directories(GPU-Switcher PRIVATE src)

target_compile_definitions(GPU-Switcher PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

# ---------------------------------------------------------
# Link required system libraries
# Added: setupapi, cfgmgr32
# ---------------------------------------------------------
target_link_libraries(GPU-Switcher PRIVATE
    shell32
    user32
    gdi32
    advapi32
    d3d11
    dxgi
    setupapi
    cfgmgr32
)

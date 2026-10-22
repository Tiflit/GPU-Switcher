cmake_minimum_required(VERSION 3.20)
project(GPU-Switcher LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)

add_definitions(-DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00)

# ---------------------------------------------------------
# Add source files
# ---------------------------------------------------------
add_executable(GPU-Switcher WIN32
    src/main.cpp
    src/resource.rc
    src/app.manifest
    src/util/logging.cpp
    src/util/startup.cpp
    src/util/reset.cpp
    src/reset/reset_lock.cpp
)

set_target_properties(GPU-Switcher PROPERTIES OUTPUT_NAME "GPU-Switcher")

execute_process(COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE GPU_SWITCHER_BUILD_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT GPU_SWITCHER_BUILD_REVISION)
    set(GPU_SWITCHER_BUILD_REVISION "unknown")
endif()
configure_file(src/build_info.h.in generated/build_info.h @ONLY)
target_include_directories(GPU-Switcher PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")

target_compile_definitions(GPU-Switcher PRIVATE
    UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
)

# ---------------------------------------------------------
# Link required system libraries
# ---------------------------------------------------------
target_link_libraries(GPU-Switcher PRIVATE
    shell32
    user32
    gdi32
    advapi32
    ole32
    d3d11
    dxgi
    cfgmgr32
)

include(CTest)
if(BUILD_TESTING)
    add_executable(acquisition_tests tests/acquisition_tests.cpp)
    target_include_directories(acquisition_tests PRIVATE src)
    add_test(NAME acquisition_regressions COMMAND acquisition_tests)
    add_executable(power_gate_tests tests/power_gate_tests.cpp)
    target_include_directories(power_gate_tests PRIVATE src)
    add_test(NAME power_transition_regressions COMMAND power_gate_tests)
    add_executable(reset_policy_tests tests/reset_policy_tests.cpp)
    target_include_directories(reset_policy_tests PRIVATE src)
    add_test(NAME reset_policy_regressions COMMAND reset_policy_tests)
    add_executable(reset_lock_tests tests/reset_lock_tests.cpp src/reset/reset_lock.cpp)
    target_include_directories(reset_lock_tests PRIVATE src)
    target_compile_definitions(reset_lock_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    add_test(NAME reset_lock_process_exclusion COMMAND reset_lock_tests)
    set_tests_properties(reset_lock_process_exclusion PROPERTIES TIMEOUT 15)
endif()

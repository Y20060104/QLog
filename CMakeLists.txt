cmake_minimum_required(VERSION 3.22)

project(
    QLog
    VERSION 0.1.0
    DESCRIPTION "High-performance, industrial-grade logging system with lock-free ring buffers"
    HOMEPAGE_URL "https://github.com/user/qlog"
    LANGUAGES CXX C
)

# ============================================================================
# Global CMake Configuration
# ============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/lib)

# ============================================================================
# Options
# ============================================================================

option(QLOG_BUILD_TESTS "Build unit tests" ON)
option(QLOG_BUILD_BENCH "Build benchmarks" ON)
option(QLOG_BUILD_DEMO "Build demo applications" ON)
option(QLOG_ENABLE_ASAN "Enable Address Sanitizer" OFF)
option(QLOG_ENABLE_TSAN "Enable Thread Sanitizer" OFF)
option(QLOG_ENABLE_UBSAN "Enable Undefined Behavior Sanitizer" OFF)
option(QLOG_ENABLE_LTO "Enable Link-Time Optimization" ON)

# ============================================================================
# Platform Detection
# ============================================================================

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(QLOG_PLATFORM "linux")
    add_compile_definitions(QLOG_PLATFORM_LINUX=1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(QLOG_PLATFORM "macos")
    add_compile_definitions(QLOG_PLATFORM_MACOS=1)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(QLOG_PLATFORM "windows")
    add_compile_definitions(QLOG_PLATFORM_WINDOWS=1)
elseif(CMAKE_SYSTEM_NAME MATCHES "ARM")
    set(QLOG_PLATFORM "arm")
    add_compile_definitions(QLOG_PLATFORM_ARM=1)
else()
    set(QLOG_PLATFORM "unix")
    add_compile_definitions(QLOG_PLATFORM_UNIX=1)
endif()

message(STATUS "QLog: Detected platform: ${QLOG_PLATFORM}")

# ============================================================================
# Compiler Flags
# ============================================================================

# Common flags for all compilers
set(QLOG_COMMON_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Werror=format
    -Wno-pragmas  # Suppress warning about platform-specific pragmas
)

# C++ specific flags
set(QLOG_CXX_FLAGS
    -fno-exceptions
    -fno-rtti
    -fvisibility=hidden
    -fvisibility-inlines-hidden
)

# Compiler-specific flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(${QLOG_COMMON_FLAGS} ${QLOG_CXX_FLAGS})

    # Optimization flags
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native)
        if(QLOG_ENABLE_LTO)
            add_compile_options(-flto)
            if(QLOG_PLATFORM STREQUAL "linux")
                add_link_options(-flto=auto)
            endif()
        endif()
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-O0 -g3)
    endif()

    # Sanitizers
    if(QLOG_ENABLE_ASAN)
        add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address)
        message(STATUS "QLog: Address Sanitizer enabled")
    endif()

    if(QLOG_ENABLE_TSAN)
        add_compile_options(-fsanitize=thread)
        add_link_options(-fsanitize=thread)
        message(STATUS "QLog: Thread Sanitizer enabled")
    endif()

    if(QLOG_ENABLE_UBSAN)
        add_compile_options(-fsanitize=undefined)
        add_link_options(-fsanitize=undefined)
        message(STATUS "QLog: UB Sanitizer enabled")
    endif()

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/W4 /WX)
    add_compile_options(/EHsc)
    add_compile_definitions(
        _HAS_EXCEPTIONS=0
        NOMINMAX
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /Oi)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/Od /Zi)
    endif()

endif()

# ============================================================================
# Build Type Configuration
# ============================================================================

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

message(STATUS "QLog: Build type: ${CMAKE_BUILD_TYPE}")

# ============================================================================
# Dependencies
# ============================================================================

find_package(Threads REQUIRED)

# Optional: Find compression libraries for appenders
# find_package(zstd QUIET)
# find_package(ZLIB QUIET)

# ============================================================================
# Main Library Subdirectories
# ============================================================================

add_subdirectory(src)

# ============================================================================
# Optional Subdirectories
# ============================================================================

if(QLOG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

if(QLOG_BUILD_BENCH)
    add_subdirectory(benchmark)
endif()

if(QLOG_BUILD_DEMO)
    add_subdirectory(demo)
endif()

# ============================================================================
# Installation Configuration
# ============================================================================

install(
    DIRECTORY include/qlog
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

install(
    FILES RULES.md STATE.md
    DESTINATION share/qlog/doc
)

# ============================================================================
# Print Configuration Summary
# ============================================================================

message(STATUS "")
message(STATUS "========== QLog Configuration Summary ==========")
message(STATUS "  Project:              ${PROJECT_NAME}")
message(STATUS "  Version:              ${PROJECT_VERSION}")
message(STATUS "  Platform:             ${QLOG_PLATFORM}")
message(STATUS "  C++ Standard:         ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:           ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler:             ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "  Build Tests:          ${QLOG_BUILD_TESTS}")
message(STATUS "  Build Benchmarks:     ${QLOG_BUILD_BENCH}")
message(STATUS "  Build Demos:          ${QLOG_BUILD_DEMO}")
message(STATUS "  Address Sanitizer:    ${QLOG_ENABLE_ASAN}")
message(STATUS "  Thread Sanitizer:     ${QLOG_ENABLE_TSAN}")
message(STATUS "  UB Sanitizer:         ${QLOG_ENABLE_UBSAN}")
message(STATUS "  Link-Time Optim:      ${QLOG_ENABLE_LTO}")
message(STATUS "=============================================")
message(STATUS "")

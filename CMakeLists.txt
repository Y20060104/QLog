cmake_minimum_required(VERSION 3.20)

project(QLog VERSION 0.1.0 LANGUAGES CXX)


function(qlog_enable_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /utf-8
            /W4
            /permissive-
        )
    else()
        target_compile_options(${target_name} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
        )
    endif()
endfunction()

add_library(qlog STATIC
    src/version.cpp
    src/buffer/spsc_ring_buffer.cpp
    src/buffer/log_buffer_config.cpp
    src/buffer/normal_buffer.cpp
    src/buffer/miso_ring_buffer.cpp
    src/buffer/block_list.cpp
    src/buffer/group_list.cpp
    src/buffer/oversize_buffer.cpp
    src/buffer/log_buffer.cpp
    src/config/property.cpp
    src/config/property_value.cpp
    src/config/filter_config.cpp
    src/config/buffer_config.cpp
    src/runtime/log_level_bitmap.cpp
    src/record/log_entry_handle.cpp
    src/layout/time_zone.cpp
    src/layout/layout.cpp
    src/utility/string_utils.cpp
    src/utility/utf_conversion.cpp
    src/utility/hash.cpp
    src/utility/hash_software.cpp
    src/utility/hash_x86_crc32c.cpp
    src/global/global_vars.cpp
    src/global/common_vars.cpp
    src/global/log_vars.cpp
    src/io/file_manager.cpp
    src/platform/file_defs.cpp
    src/platform/file_path.cpp
    src/utility/diagnostics.cpp
)

# Keep hardware CRC instructions in their own translation unit.
if(NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND
   CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64|i[3-6]86|x86|X86)$")
    set_source_files_properties(src/utility/hash_x86_crc32c.cpp PROPERTIES
        COMPILE_OPTIONS "-msse4.2"
    )
endif()

if(WIN32)
    target_sources(qlog PRIVATE
        src/platform/windows/aligned_memory.cpp
        src/platform/windows/file_io.cpp
        src/platform/windows/file_path.cpp
        src/platform/windows/thread_ops.cpp
        src/platform/windows/time_ops.cpp
        src/platform/windows/mutex_win64.cpp
        src/platform/windows/system_api.cpp
    )
else()
    target_sources(qlog PRIVATE
        src/platform/posix/aligned_memory.cpp
        src/platform/posix/file_io.cpp
        src/platform/posix/file_path.cpp
        src/platform/posix/thread_ops.cpp
        src/platform/posix/time_ops.cpp
        src/platform/posix/mutex_posix.cpp
    )
    find_package(Threads REQUIRED)
    target_link_libraries(qlog PUBLIC Threads::Threads)
endif()

target_sources(qlog PRIVATE
    src/platform/memory_map.cpp
)

if(WIN32)
    target_sources(qlog PRIVATE
        src/platform/windows/memory_map.cpp
    )
else()
    target_sources(qlog PRIVATE
        src/platform/posix/memory_map.cpp
    )
endif()
# Block-SISO and the replacement Record view are built above.
# Retired byte-cursor Ring/Record fuzzers and benchmarks remain disabled.
if(QLOG_BUILD_FUZZERS OR QLOG_BUILD_RECORD_BENCHMARK OR QLOG_BUILD_BENCHMARKS)
    message(FATAL_ERROR "Old Record/Ring fuzz and benchmark targets were retired; see R0_BQLOG_BLOCK_RING_GUIDE_CHS.md")
endif()

add_library(QLog::qlog ALIAS qlog)

target_compile_features(qlog PUBLIC cxx_std_20)

# This macro changes public Ring layouts; consumers must inherit the same value.
# Leave it undefined outside Debug: the headers use #ifdef, not #if.
target_compile_definitions(qlog PUBLIC
    $<$<CONFIG:Debug>:QLOG_DEBUG>
)

set_target_properties(qlog PROPERTIES
    CXX_EXTENSIONS OFF
)

target_include_directories(qlog
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

qlog_enable_warnings(qlog)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

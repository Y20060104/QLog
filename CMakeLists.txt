cmake_minimum_required(VERSION 3.20)

project(QLog VERSION 0.1.0 LANGUAGES CXX)

option(QLOG_BUILD_FUZZERS "Build I1 libFuzzer targets (instrumented build required)" OFF)

option(QLOG_BUILD_BENCHMARKS "Build QLog benchmarks" OFF)
option(QLOG_ENABLE_X86_CRC32C "Compile the optional x86 CRC32C backend" ON)

set(
    QLOG_ENABLE_RING_VALIDATION
    "AUTO"
    CACHE STRING
    "Ring validation mode: AUTO, ON or OFF"
)

set_property(
    CACHE QLOG_ENABLE_RING_VALIDATION
    PROPERTY STRINGS AUTO ON OFF
)

function(qlog_enable_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
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
    src/spsc_ring_buffer.cpp
    src/format_hash.cpp
    src/format_hash_software.cpp
    src/record_decoder.cpp
)

set(QLOG_HASH_X86_VALUE 0)
if(QLOG_ENABLE_X86_CRC32C
   AND CMAKE_SYSTEM_NAME STREQUAL "Linux"
   AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$"
   AND CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    set(QLOG_HASH_X86_VALUE 1)
    target_sources(qlog PRIVATE src/format_hash_x86_crc32c.cpp)
    set_source_files_properties(src/format_hash_x86_crc32c.cpp
        PROPERTIES COMPILE_OPTIONS "-msse4.2")
endif()
target_compile_definitions(qlog PRIVATE
    QLOG_HAS_X86_CRC32C=${QLOG_HASH_X86_VALUE})

if(QLOG_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "Fuzz builds require Clang")
    endif()
    target_compile_options(qlog PRIVATE -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(qlog INTERFACE -fsanitize=address,undefined)
endif()

add_library(QLog::qlog ALIAS qlog)

target_compile_features(qlog PUBLIC cxx_std_20)

set_target_properties(qlog PROPERTIES
    CXX_EXTENSIONS OFF
)

target_include_directories(qlog
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

qlog_enable_warnings(qlog)

if(QLOG_ENABLE_RING_VALIDATION STREQUAL "AUTO")
    set(QLOG_RING_VALIDATION_VALUE
        "$<IF:$<CONFIG:Debug>,1,0>")
elseif(QLOG_ENABLE_RING_VALIDATION STREQUAL "ON")
    set(QLOG_RING_VALIDATION_VALUE 1)
elseif(QLOG_ENABLE_RING_VALIDATION STREQUAL "OFF")
    set(QLOG_RING_VALIDATION_VALUE 0)
else()
    message(FATAL_ERROR
        "QLOG_ENABLE_RING_VALIDATION must be AUTO, ON or OFF")
endif()

target_compile_definitions(qlog PRIVATE
    QLOG_ENABLE_RING_VALIDATION=${QLOG_RING_VALIDATION_VALUE}
)

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(QLOG_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

option(QLOG_BUILD_RECORD_BENCHMARK "Build standalone Record Core baseline" OFF)
if(QLOG_BUILD_RECORD_BENCHMARK)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" OR NOT CMAKE_BUILD_TYPE STREQUAL "Release")
        message(FATAL_ERROR "Record Core baseline needs GCC/Clang Release")
    endif()
    add_executable(qlog_record_core_benchmark benchmarks/record_core_benchmark.cpp)
    target_link_libraries(qlog_record_core_benchmark PRIVATE QLog::qlog)
    target_include_directories(qlog_record_core_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(qlog_record_core_benchmark PRIVATE QLOG_TEST_HAS_X86_CRC32C=${QLOG_HASH_X86_VALUE})
    qlog_enable_warnings(qlog_record_core_benchmark)
endif()

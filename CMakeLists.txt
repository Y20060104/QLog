cmake_minimum_required(VERSION 3.20)

project(QLog VERSION 0.1.0 LANGUAGES CXX)

option(QLOG_BUILD_BENCHMARKS "Build QLog benchmarks" OFF)

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
)

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

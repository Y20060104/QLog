cmake_minimum_required(VERSION 3.20)

project(QLog VERSION 0.1.0 LANGUAGES CXX)

option(QLOG_BUILD_BENCHMARKS "Build QLog benchmarks" OFF)

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

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

if(QLOG_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

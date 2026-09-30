cmake_minimum_required(VERSION 3.20)

project(QLog VERSION 0.1.0 LANGUAGES CXX)


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
)

# Replacement modules will be added in subsequent commits.
# Retired byte-cursor Ring/Record fuzzers and benchmarks remain disabled.
if(QLOG_BUILD_FUZZERS OR QLOG_BUILD_RECORD_BENCHMARK OR QLOG_BUILD_BENCHMARKS)
    message(FATAL_ERROR "Old Record/Ring fuzz and benchmark targets were retired; see R0_BQLOG_BLOCK_RING_GUIDE_CHS.md")
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

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

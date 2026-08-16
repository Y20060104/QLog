cmake_minimum_required(VERSION 3.20)

project(QLog VERSION 0.1.0 LANGUAGES CXX)

add_library(qlog STATIC
    src/version.cpp
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

if(MSVC)
    target_compile_options(qlog PRIVATE /W4 /permissive-)
else()
    target_compile_options(qlog PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
    )
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(qlog_smoke_test
        tests/smoke_test.cpp
    )

    target_link_libraries(qlog_smoke_test PRIVATE QLog::qlog)

    add_test(
        NAME qlog.smoke
        COMMAND qlog_smoke_test
    )
endif()
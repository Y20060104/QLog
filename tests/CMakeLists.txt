if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

include(FetchContent)

FetchContent_Declare(
    googletest
    URL
        https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
    URL_HASH
        SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
)

set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

if(MSVC)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(googletest)

add_executable(qlog_smoke_test
    smoke_test.cpp
)

target_link_libraries(qlog_smoke_test
    PRIVATE
        QLog::qlog
)

qlog_enable_warnings(qlog_smoke_test)

add_test(
    NAME qlog.smoke
    COMMAND qlog_smoke_test
)

set_tests_properties(qlog.smoke PROPERTIES
    LABELS "smoke"
)

add_executable(qlog_geometry_test
    geometry_test.cpp
)

target_link_libraries(qlog_geometry_test
    PRIVATE
        QLog::qlog
        GTest::gtest_main
)

qlog_enable_warnings(qlog_geometry_test)

add_executable(qlog_spsc_ring_buffer_test
    spsc_ring_buffer_header_self_contained.cpp
    spsc_ring_buffer_storage_test.cpp
    spsc_ring_buffer_test.cpp
)

target_link_libraries(qlog_spsc_ring_buffer_test
    PRIVATE
        QLog::qlog
        GTest::gtest_main
)

qlog_enable_warnings(qlog_spsc_ring_buffer_test)


include(GoogleTest)

gtest_discover_tests(qlog_geometry_test
    TEST_PREFIX "qlog."
    PROPERTIES LABELS geometry
)

gtest_discover_tests(qlog_spsc_ring_buffer_test
    TEST_PREFIX "qlog."
    PROPERTIES LABELS spsc_ring_buffer
)
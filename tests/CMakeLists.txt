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
    spsc_ring_buffer_layout_test.cpp
    spsc_ring_buffer_operation_test.cpp
    spsc_ring_buffer_payload_test.cpp
    spsc_ring_buffer_storage_test.cpp
    spsc_ring_buffer_test.cpp
    spsc_ring_buffer_wrap_concurrency_test.cpp
    spsc_ring_buffer_test_access.hpp
)

target_link_libraries(qlog_spsc_ring_buffer_test
    PRIVATE
        QLog::qlog
        GTest::gtest_main
)

target_compile_definitions(qlog_spsc_ring_buffer_test
    PRIVATE
        QLOG_TEST_RING_VALIDATION=${QLOG_RING_VALIDATION_VALUE}
)

qlog_enable_warnings(qlog_spsc_ring_buffer_test)

add_executable(qlog_record_header_test
    argument_tag_self_contained.cpp
    argument_tag_test.cpp
    record_header_self_contained.cpp
    record_header_layout_test.cpp
)

target_link_libraries(qlog_record_header_test
    PRIVATE
        QLog::qlog
        GTest::gtest_main
)

qlog_enable_warnings(qlog_record_header_test)

add_executable(qlog_argument_model_test
    arguments_self_contained.cpp
    argument_traits_self_contained.cpp
    argument_model_test.cpp
    record_measure_self_contained.cpp
)

target_link_libraries(qlog_argument_model_test
    PRIVATE
        QLog::qlog
        GTest::gtest_main
)

qlog_enable_warnings(qlog_argument_model_test)

include(GoogleTest)

gtest_discover_tests(qlog_geometry_test
    TEST_PREFIX "qlog."
    PROPERTIES LABELS geometry
)

gtest_discover_tests(qlog_spsc_ring_buffer_test
    TEST_PREFIX "qlog."
    PROPERTIES LABELS spsc_ring_buffer
)

gtest_discover_tests(qlog_record_header_test
    TEST_PREFIX "qlog."
    PROPERTIES LABELS record_header
)

gtest_discover_tests(qlog_argument_model_test
    TEST_PREFIX "qlog."
    PROPERTIES LABELS record_core
)

# I1-B hash and I1-C codec validation.
add_executable(qlog_format_hash_test
    format_hash_self_contained.cpp
    format_hash_reference_self_contained.cpp
    format_hash_test.cpp
)
add_executable(qlog_record_codec_test
    record_types_self_contained.cpp
    record_encoder_self_contained.cpp
    record_types_test.cpp
    record_encoder_test.cpp
    record_decoder_self_contained.cpp
    record_decoder_test.cpp
    record_decoder_boundary_test.cpp
)
foreach(target_name IN ITEMS qlog_format_hash_test qlog_record_codec_test)
    target_link_libraries(${target_name} PRIVATE QLog::qlog GTest::gtest_main)
    target_include_directories(${target_name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(${target_name} PRIVATE
        QLOG_TEST_HAS_X86_CRC32C=${QLOG_HASH_X86_VALUE})
    qlog_enable_warnings(${target_name})
endforeach()
gtest_discover_tests(qlog_format_hash_test
    TEST_PREFIX "qlog."
    PROPERTIES LABELS record_core
)
gtest_discover_tests(qlog_record_codec_test
    TEST_PREFIX "qlog."
    PROPERTIES LABELS record_core
)

# Apply list-valued labels after GoogleTest discovery without flattening semicolons.
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/record_core_labels.cmake")

include(i1d.cmake)

# Constructor/reset value preparation share the production implementation.
add_executable(qlog_logger_config_test logger_config_test.cpp)
target_link_libraries(qlog_logger_config_test PRIVATE QLog::qlog)
qlog_enable_warnings(qlog_logger_config_test)
add_test(NAME qlog.logger_config COMMAND qlog_logger_config_test)
set_tests_properties(qlog.logger_config PROPERTIES
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    LABELS "config"
)

# Worker-side UTF-8 body formatter compatibility and bounded-output tests.
add_executable(qlog_text_formatter_test text_formatter_test.cpp)
target_link_libraries(qlog_text_formatter_test PRIVATE QLog::qlog GTest::gtest_main)
qlog_enable_warnings(qlog_text_formatter_test)
gtest_discover_tests(qlog_text_formatter_test
    TEST_PREFIX "qlog." PROPERTIES LABELS formatter)

# QLog V1 targets Linux; this probe also intercepts POSIX aligned allocation.
if(UNIX)
    add_executable(qlog_text_formatter_allocation_test text_formatter_allocation_test.cpp)
    target_link_libraries(qlog_text_formatter_allocation_test PRIVATE QLog::qlog)
    qlog_enable_warnings(qlog_text_formatter_allocation_test)
    add_test(NAME qlog.text_formatter_allocation COMMAND qlog_text_formatter_allocation_test)
    set_tests_properties(qlog.text_formatter_allocation PROPERTIES LABELS "formatter;allocation")
endif()

# Isolated producer diagnostics: no backend consumer is started by this fixture.
find_package(Threads REQUIRED)
add_executable(qlog_producer_diagnostics_test
    producer_diagnostics_test.cpp producer_diagnostics_consumer.cpp)
target_link_libraries(qlog_producer_diagnostics_test PRIVATE QLog::qlog Threads::Threads)
target_compile_definitions(qlog_producer_diagnostics_test PRIVATE
    QLOG_TEST_EXPECT_DIAGNOSTICS=${QLOG_DIAGNOSTICS_VALUE})
qlog_enable_warnings(qlog_producer_diagnostics_test)
add_test(NAME qlog.producer_diagnostics COMMAND qlog_producer_diagnostics_test)
set_tests_properties(qlog.producer_diagnostics PROPERTIES LABELS "diagnostics" TIMEOUT 30)

# Separately link the real qlog archive through the public API.
add_executable(qlog_producer_public_probe i2_runtime_public_probe.cpp)
target_link_libraries(qlog_producer_public_probe PRIVATE QLog::qlog Threads::Threads)
qlog_enable_warnings(qlog_producer_public_probe)
add_test(NAME qlog.producer_public_probe COMMAND qlog_producer_public_probe)
set_tests_properties(qlog.producer_public_probe PROPERTIES LABELS "diagnostics" TIMEOUT 30)

# Single-owner Appender state machine; scripted I/O, no worker threads.
add_executable(qlog_appender_test appender_test.cpp)
target_link_libraries(qlog_appender_test PRIVATE QLog::qlog GTest::gtest_main)
qlog_enable_warnings(qlog_appender_test)
gtest_discover_tests(qlog_appender_test TEST_PREFIX "qlog." PROPERTIES LABELS appender)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(qlog_appender_posix_test appender_posix_test.cpp)
    target_link_libraries(qlog_appender_posix_test PRIVATE QLog::qlog GTest::gtest_main Threads::Threads)
    target_link_options(qlog_appender_posix_test PRIVATE
        -Wl,--wrap=open -Wl,--wrap=write -Wl,--wrap=fdatasync -Wl,--wrap=fstat -Wl,--wrap=close)
    qlog_enable_warnings(qlog_appender_posix_test)
    gtest_discover_tests(qlog_appender_posix_test TEST_PREFIX "qlog." PROPERTIES LABELS "appender")
endif()

add_executable(qlog_compose_line_test compose_line_test.cpp)
target_link_libraries(qlog_compose_line_test PRIVATE QLog::qlog GTest::gtest_main Threads::Threads)
qlog_enable_warnings(qlog_compose_line_test)
gtest_discover_tests(qlog_compose_line_test TEST_PREFIX "qlog." PROPERTIES LABELS formatter)

add_executable(qlog_backend_session_test backend_session_test.cpp)
target_link_libraries(qlog_backend_session_test PRIVATE QLog::qlog GTest::gtest_main Threads::Threads)
target_link_options(qlog_backend_session_test PRIVATE -Wl,--wrap=write)
qlog_enable_warnings(qlog_backend_session_test)
gtest_discover_tests(qlog_backend_session_test TEST_PREFIX "qlog." PROPERTIES LABELS backend)

add_executable(qlog_worker_boundary_test worker_boundary_test.cpp)
target_link_libraries(qlog_worker_boundary_test PRIVATE QLog::qlog)
add_test(NAME qlog.worker_boundary COMMAND qlog_worker_boundary_test)
set_tests_properties(qlog.worker_boundary PROPERTIES LABELS backend)
add_executable(qlog_backend_allocation_test backend_allocation_test.cpp)
target_link_libraries(qlog_backend_allocation_test PRIVATE QLog::qlog)
qlog_enable_warnings(qlog_backend_allocation_test)
add_test(NAME qlog.backend_allocation COMMAND qlog_backend_allocation_test)
set_tests_properties(qlog.backend_allocation PROPERTIES LABELS "backend;allocation")

add_executable(qlog_worker_runtime_test worker_runtime_test.cpp)
target_link_libraries(qlog_worker_runtime_test PRIVATE QLog::qlog GTest::gtest_main)
target_link_options(qlog_worker_runtime_test PRIVATE -Wl,--wrap=write -Wl,--wrap=pthread_sigmask)
qlog_enable_warnings(qlog_worker_runtime_test)
gtest_discover_tests(qlog_worker_runtime_test TEST_PREFIX "qlog." PROPERTIES LABELS "worker" TIMEOUT 30)

add_executable(qlog_worker_allocation_test worker_allocation_test.cpp)
target_link_libraries(qlog_worker_allocation_test PRIVATE QLog::qlog)
qlog_enable_warnings(qlog_worker_allocation_test)
add_test(NAME qlog.worker_allocation COMMAND qlog_worker_allocation_test)
set_tests_properties(qlog.worker_allocation PROPERTIES LABELS "worker;allocation" TIMEOUT 30)

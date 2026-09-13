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

# Old byte-cursor Ring and Record tests were retired with their implementations.
# Add the new block-SISO tests together with the replacement implementation.
add_executable(qlog_smoke_test smoke_test.cpp)
target_link_libraries(qlog_smoke_test PRIVATE QLog::qlog)
qlog_enable_warnings(qlog_smoke_test)
add_test(NAME qlog.smoke COMMAND qlog_smoke_test)
set_tests_properties(qlog.smoke PROPERTIES LABELS "smoke")

# Public Layout API regressions using independent record-wire fixtures.
add_executable(qlog_layout_behavior_test layout_behavior_test.cpp)
target_link_libraries(qlog_layout_behavior_test PRIVATE QLog::qlog)
qlog_enable_warnings(qlog_layout_behavior_test)
foreach(group IN ITEMS prefix thread_cache scanner numeric arguments failure reuse
                       serialization_mixed serialization_strings serialization_custom)
    add_test(NAME qlog.layout.${group} COMMAND qlog_layout_behavior_test ${group})
    set_tests_properties(qlog.layout.${group} PROPERTIES LABELS "layout")
endforeach()

# Common UTF conversion must keep code-unit input separate from byte output.
add_executable(qlog_utf_conversion_test utf_conversion_test.cpp)
target_link_libraries(qlog_utf_conversion_test PRIVATE QLog::qlog)
qlog_enable_warnings(qlog_utf_conversion_test)
add_test(NAME qlog.utility.utf16_to_utf8 COMMAND qlog_utf_conversion_test)
set_tests_properties(qlog.utility.utf16_to_utf8 PROPERTIES LABELS "utility;utf")

# Atomic RMW return contracts, integer boundaries and concurrent ticket allocation.
add_executable(qlog_atomic_arithmetic_test atomic_arithmetic_test.cpp)
target_link_libraries(qlog_atomic_arithmetic_test PRIVATE QLog::qlog)
qlog_enable_warnings(qlog_atomic_arithmetic_test)
add_test(NAME qlog.platform.atomic_arithmetic COMMAND qlog_atomic_arithmetic_test)
set_tests_properties(qlog.platform.atomic_arithmetic PROPERTIES LABELS "platform;atomic")

# Original HashMap public behavior: collisions, growth, reuse and object lifetime.
add_executable(qlog_hash_map_test hash_map_test.cpp)
target_link_libraries(qlog_hash_map_test PRIVATE QLog::qlog)
qlog_enable_warnings(qlog_hash_map_test)
foreach(group IN ITEMS basic collision lifetime)
    add_test(NAME qlog.utility.hash_map.${group} COMMAND qlog_hash_map_test ${group})
    set_tests_properties(qlog.utility.hash_map.${group} PROPERTIES LABELS "utility;hash_map")
endforeach()

# Original HashMap integration in strings, configuration and native file registration.
add_executable(qlog_hash_map_integration_test hash_map_integration_test.cpp)
target_link_libraries(qlog_hash_map_integration_test PRIVATE QLog::qlog)
qlog_enable_warnings(qlog_hash_map_integration_test)
foreach(group IN ITEMS strings properties registry)
    add_test(NAME qlog.hash_map_integration.${group} COMMAND qlog_hash_map_integration_test ${group})
    set_tests_properties(qlog.hash_map_integration.${group} PROPERTIES LABELS "hash_map;integration")
endforeach()

# Original global Debug disable counter, nested RAII and Ring query integration.
add_executable(qlog_thread_check_test thread_check_test.cpp)
target_link_libraries(qlog_thread_check_test PRIVATE QLog::qlog)
qlog_enable_warnings(qlog_thread_check_test)
add_test(NAME qlog.buffer.thread_check_policy COMMAND qlog_thread_check_test)
set_tests_properties(qlog.buffer.thread_check_policy PROPERTIES LABELS "buffer;debug;global")

add_executable(qlog_thread_runtime_test thread_runtime_test.cpp)
target_link_libraries(qlog_thread_runtime_test PRIVATE QLog::qlog)
qlog_enable_warnings(qlog_thread_runtime_test)
add_test(NAME qlog.platform.thread_runtime COMMAND qlog_thread_runtime_test)
set_tests_properties(qlog.platform.thread_runtime PROPERTIES TIMEOUT 30 LABELS "platform;thread")
add_executable(qlog_runtime_integration_test runtime_integration_test.cpp)
target_link_libraries(qlog_runtime_integration_test PRIVATE QLog::qlog)
qlog_enable_warnings(qlog_runtime_integration_test)
foreach(group IN ITEMS sync async independent filters_reset concurrent text_file stack_console_buffer oversize queue_policies snapshot_resize)
 add_test(NAME qlog.runtime.${group} COMMAND qlog_runtime_integration_test ${group})
 set_tests_properties(qlog.runtime.${group} PROPERTIES TIMEOUT 60 LABELS "runtime;integration")
endforeach()

# BQLog compressed v10 container, caches, crypto and actual public runtime output.
add_executable(qlog_compressed_file_test compressed_file_test.cpp)
target_link_libraries(qlog_compressed_file_test PRIVATE QLog::qlog)
qlog_enable_warnings(qlog_compressed_file_test)
foreach(group IN ITEMS primitives scalars sync async independent cache_reset reuse rollover encrypted)
    add_test(NAME qlog.compressed.${group} COMMAND qlog_compressed_file_test ${group})
    set_tests_properties(qlog.compressed.${group} PROPERTIES TIMEOUT 90 LABELS "compressed;integration")
endforeach()

# Fused copy/hash bytes and original BQLog SW/HW vectors, then actual Record writing.
add_executable(qlog_copy_hash_test copy_hash_test.cpp)
target_link_libraries(qlog_copy_hash_test PRIVATE QLog::qlog)
target_include_directories(qlog_copy_hash_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
qlog_enable_warnings(qlog_copy_hash_test)
add_test(NAME qlog.utility.copy_hash COMMAND qlog_copy_hash_test utility)
set_tests_properties(qlog.utility.copy_hash PROPERTIES LABELS "utility;hash")
add_test(NAME qlog.runtime.format_copy_hash COMMAND qlog_copy_hash_test writer)
set_tests_properties(qlog.runtime.format_copy_hash PROPERTIES TIMEOUT 30 LABELS "runtime;hash")

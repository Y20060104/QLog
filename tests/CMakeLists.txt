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
foreach(group IN ITEMS prefix scanner numeric arguments failure reuse
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

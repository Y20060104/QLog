
# I1-D compile-time admission gates. Each has a successful adjacent control.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
add_test(NAME qlog.compile_fail.bare_const_char COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=bare_const_char
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/bare_const_char -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.bare_const_char PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.bare_mutable_char COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=bare_mutable_char
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/bare_mutable_char -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.bare_mutable_char PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.object_pointer COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=object_pointer
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/object_pointer -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.object_pointer PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.function_pointer COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=function_pointer
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/function_pointer -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.function_pointer PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.member_pointer COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=member_pointer
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/member_pointer -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.member_pointer PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.volatile COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=volatile
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/volatile -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.volatile PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.long_double COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=long_double
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/long_double -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.long_double PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.int128 COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=int128
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/int128 -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.int128 PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.uint128 COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=uint128
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/uint128 -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.uint128 PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.char8 COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=char8
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/char8 -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.char8 PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.char16 COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=char16
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/char16 -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.char16 PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.char32 COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=char32
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/char32 -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.char32 PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.wchar COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=wchar
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/wchar -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.wchar PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.wide_string COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=wide_string
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/wide_string -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.wide_string PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.u16_string COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=u16_string
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/u16_string -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.u16_string PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.u32_string COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=u32_string
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/u32_string -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.u32_string PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.byte COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=byte
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/byte -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.byte PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.enum_bool COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=enum_bool
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/enum_bool -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.enum_bool PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.container COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=container
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/container -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.container PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.chrono COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=chrono
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/chrono -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.chrono PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.implicit COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=implicit
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/implicit -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.implicit PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.format_as COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=format_as
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/format_as -DTOKEN=SupportedArgument
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.format_as PROPERTIES LABELS "record_core;compile_fail")
add_test(NAME qlog.compile_fail.arg_count COMMAND ${CMAKE_COMMAND}
 -DCOMPILER=${CMAKE_CXX_COMPILER} -DROOT=${PROJECT_SOURCE_DIR} -DCASE_NAME=arg_count
 -DCASE_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_cases/arg_count -DTOKEN=kMaxArgCount
 -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_compile_case.cmake)
set_tests_properties(qlog.compile_fail.arg_count PROPERTIES LABELS "record_core;compile_fail")
endif()

add_executable(qlog_record_property_test record_property_test.cpp i1d_boundaries.cpp i1d_error_names.cpp)
target_link_libraries(qlog_record_property_test PRIVATE QLog::qlog GTest::gtest_main)
target_include_directories(qlog_record_property_test PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(qlog_record_property_test PRIVATE QLOG_TEST_HAS_X86_CRC32C=${QLOG_HASH_X86_VALUE} QLOG_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/corpus/record_core")
qlog_enable_warnings(qlog_record_property_test)
add_test(NAME qlog.record_property COMMAND qlog_record_property_test)
set_tests_properties(qlog.record_property PROPERTIES LABELS "record_core;property")

if(QLOG_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "Fuzz targets require Clang")
    endif()
    foreach(name record_decode record_roundtrip format_hash_equivalence cstr_length_cache)
        add_executable(qlog_fuzz_${name} fuzz/${name}.cpp)
        target_link_libraries(qlog_fuzz_${name} PRIVATE QLog::qlog)
        target_include_directories(qlog_fuzz_${name} PRIVATE ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(qlog_fuzz_${name} PRIVATE QLOG_TEST_HAS_X86_CRC32C=${QLOG_HASH_X86_VALUE})
        target_compile_options(qlog_fuzz_${name} PRIVATE -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_link_options(qlog_fuzz_${name} PRIVATE -fsanitize=fuzzer,address,undefined)
    endforeach()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
add_executable(qlog_record_allocation_test record_allocation_test.cpp)
target_link_libraries(qlog_record_allocation_test PRIVATE QLog::qlog)
qlog_enable_warnings(qlog_record_allocation_test)
add_test(NAME qlog.record_allocation COMMAND qlog_record_allocation_test)
set_tests_properties(qlog.record_allocation PROPERTIES LABELS "record_core;allocation")
endif()

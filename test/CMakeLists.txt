# QLog Test CMakeLists.txt

# 启用测试
enable_testing()

# M0 单元测试 - Atomic
add_executable(test_atomic cpp/test_atomic.cpp)
target_link_libraries(test_atomic PRIVATE qlog Threads::Threads)
target_include_directories(test_atomic PRIVATE ${PROJECT_SOURCE_DIR})
set_target_properties(test_atomic PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME atomic_tests COMMAND test_atomic)

# M0 单元测试 - Spin Lock
add_executable(test_spin_lock cpp/test_spin_lock.cpp)
target_link_libraries(test_spin_lock PRIVATE qlog Threads::Threads)
target_include_directories(test_spin_lock PRIVATE ${PROJECT_SOURCE_DIR})
set_target_properties(test_spin_lock PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME spin_lock_tests COMMAND test_spin_lock)

# M0 单元测试 - Spin Lock RW
add_executable(test_spin_lock_rw cpp/test_spin_lock_rw.cpp)
target_link_libraries(test_spin_lock_rw PRIVATE qlog Threads::Threads)
target_include_directories(test_spin_lock_rw PRIVATE ${PROJECT_SOURCE_DIR})
set_target_properties(test_spin_lock_rw PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME spin_lock_rw_tests COMMAND test_spin_lock_rw)

# M0 单元测试 - Aligned Alloc
add_executable(test_aligned_alloc cpp/test_aligned_alloc.cpp)
target_link_libraries(test_aligned_alloc PRIVATE qlog Threads::Threads)
target_include_directories(test_aligned_alloc PRIVATE ${PROJECT_SOURCE_DIR})
set_target_properties(test_aligned_alloc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME aligned_alloc_tests COMMAND test_aligned_alloc)

# M0 单元测试 - Platform Thread
add_executable(test_platform_thread cpp/test_platform_thread.cpp)
target_link_libraries(test_platform_thread PRIVATE qlog Threads::Threads)
target_include_directories(test_platform_thread PRIVATE ${PROJECT_SOURCE_DIR})
set_target_properties(test_platform_thread PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME platform_thread_tests COMMAND test_platform_thread)

# M0 单元测试 - Condition Variable
add_executable(test_condition_variable cpp/test_condition_variable.cpp)
target_link_libraries(test_condition_variable PRIVATE qlog Threads::Threads)
target_include_directories(test_condition_variable PRIVATE ${PROJECT_SOURCE_DIR})
set_target_properties(test_condition_variable PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME condition_variable_tests COMMAND test_condition_variable)

# M1 单元测试 - SPSC Ring Buffer
add_executable(test_spsc_ring_buffer cpp/test_spsc_ring_buffer.cpp)
target_link_libraries(test_spsc_ring_buffer PRIVATE qlog Threads::Threads)
target_include_directories(test_spsc_ring_buffer PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}
)
set_target_properties(test_spsc_ring_buffer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME spsc_ring_buffer_tests COMMAND test_spsc_ring_buffer)

# M2 单元测试 - MPSC Ring Buffer
add_executable(test_mpsc_ring_buffer cpp/test_mpsc_ring_buffer.cpp)
target_link_libraries(test_mpsc_ring_buffer PRIVATE qlog Threads::Threads)
target_include_directories(test_mpsc_ring_buffer PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}
)
set_target_properties(test_mpsc_ring_buffer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME mpsc_ring_buffer_tests COMMAND test_mpsc_ring_buffer)


# M3 单元测试 - log_buffer
add_executable(test_log_buffer cpp/test_log_buffer.cpp)
target_link_libraries(test_log_buffer PRIVATE qlog Threads::Threads)
target_include_directories(test_log_buffer PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}
)
set_target_properties(test_log_buffer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME log_buffer_tests COMMAND test_log_buffer)


# M4 单元测试 - format_hash
add_executable(test_format_hash cpp/test_format_hash.cpp)
target_link_libraries(test_format_hash PRIVATE qlog Threads::Threads)
target_include_directories(test_format_hash PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}
)
set_target_properties(test_format_hash PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
add_test(NAME format_hash_tests COMMAND test_format_hash)



message(STATUS "QLog: M0 test targets configured (atomic, spin_lock, spin_lock_rw, aligned_alloc, platform_thread, condition_variable)")
message(STATUS "QLog: M1 test targets configured (spsc_ring_buffer)")
message(STATUS "QLog: M2 test targets configured (mpsc_ring_buffer)")
message(STATUS "QLog: M3 test targets configured (log_buffer)")
message(STATUS "QLog: M4 test targets configured (format_hash)")
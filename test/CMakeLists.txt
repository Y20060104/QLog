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

message(STATUS "QLog: M0 test targets configured (atomic, spin_lock)")

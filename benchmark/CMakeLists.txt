# QLog Benchmark CMakeLists.txt

# 基准测试目标
add_executable(bench_false_sharing cpp/bench_false_sharing.cpp)
target_link_libraries(bench_false_sharing PRIVATE qlog Threads::Threads)

# 设置输出目录
set_target_properties(bench_false_sharing PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "QLog: Benchmark targets configured")


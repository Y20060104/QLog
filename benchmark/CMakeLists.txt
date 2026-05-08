# QLog Benchmark CMakeLists.txt

# 基准测试目标
add_executable(bench_false_sharing cpp/bench_false_sharing.cpp)
target_link_libraries(bench_false_sharing PRIVATE qlog Threads::Threads)

# 设置输出目录
set_target_properties(bench_false_sharing PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_executable(bench_stress cpp/bench_stress.cpp)
target_link_libraries(bench_stress PRIVATE qlog Threads::Threads)
target_include_directories(bench_stress PRIVATE ${PROJECT_SOURCE_DIR}/src)
set_target_properties(bench_stress PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_executable(bench_bqlog_compare cpp/bench_bqlog_compare.cpp)
target_link_libraries(bench_bqlog_compare PRIVATE qlog Threads::Threads)
target_include_directories(bench_bqlog_compare PRIVATE ${PROJECT_SOURCE_DIR}/src)
set_target_properties(bench_bqlog_compare PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message(STATUS "QLog: Benchmark targets configured")


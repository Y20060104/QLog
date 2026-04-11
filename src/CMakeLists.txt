# QLog Source Code CMakeLists.txt

# 创建占位符源文件（M0 实现时替换）
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/qlog_placeholder.cpp"
    "// QLog placeholder - to be replaced during M0 implementation\n"
    "namespace qlog { }\n"
)

# 库源文件（M0 完成后会添加实际源文件）
add_library(qlog STATIC
    "${CMAKE_CURRENT_BINARY_DIR}/qlog_placeholder.cpp"
)

# 为库设置 C++ 标准和编译选项
target_compile_features(qlog PUBLIC cxx_std_20)
target_compile_options(qlog PRIVATE ${QLOG_COMMON_FLAGS} ${QLOG_CXX_FLAGS})

# 包含目录
target_include_directories(qlog
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 链接线程库
target_link_libraries(qlog PUBLIC Threads::Threads)

# 设置库的输出目录
set_target_properties(qlog PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 导出符号可见性
target_compile_options(qlog PRIVATE -fvisibility=hidden)

message(STATUS "QLog: Main library target configured")

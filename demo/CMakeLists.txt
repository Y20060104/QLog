# QLog Demo CMakeLists.txt

add_executable(qlog_demo_basic basic_example.cpp)
target_link_libraries(qlog_demo_basic PRIVATE qlog Threads::Threads)
target_include_directories(qlog_demo_basic PRIVATE ${PROJECT_SOURCE_DIR}/src)

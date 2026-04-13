# QLog Source Code CMakeLists.txt

# ============================================================================
# Source Files Organization
# ============================================================================

# M0 Primitives (lock-free, no-wait primitives)
set(QLOG_PRIMITIVES_SOURCES
    qlog/primitives/cpu_relax.h
    qlog/primitives/atomic.h
    qlog/primitives/spin_lock.h
    qlog/primitives/spin_lock_rw.h
    qlog/primitives/aligned_alloc.h
    qlog/primitives/aligned_alloc.cpp
    qlog/primitives/platform_thread.h
    qlog/primitives/platform_thread.cpp
    qlog/primitives/condition_variable.h
    qlog/primitives/condition_variable.cpp
)

# ============================================================================
# Main QLog Library
# ============================================================================

add_library(qlog STATIC
    ${QLOG_PRIMITIVES_SOURCES}
)

# Set C++ standard and compile options
target_compile_features(qlog PUBLIC cxx_std_20)
target_compile_options(qlog PRIVATE ${QLOG_COMMON_FLAGS} ${QLOG_CXX_FLAGS})

# Include directories
target_include_directories(qlog
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Link required libraries
target_link_libraries(qlog PUBLIC Threads::Threads)

# Set library output directories
set_target_properties(qlog PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Export symbol visibility
target_compile_options(qlog PRIVATE -fvisibility=hidden)

message(STATUS "QLog: Main library configured with:")
message(STATUS "  - Primitives: ${QLOG_PRIMITIVES_SOURCES}")


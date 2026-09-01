if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The SPSC Ring benchmark currently supports Linux only")
endif()

if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "QLog benchmarks must be configured with CMAKE_BUILD_TYPE=Release")
endif()

find_package(Threads REQUIRED)
find_package(Git QUIET)

option(QLOG_BENCH_WITH_BQLOG "Build the BQLog SISO comparison" ON)
set(QLOG_BENCH_BQLOG_ROOT "${PROJECT_SOURCE_DIR}/../BqLog" CACHE PATH
    "Path to a BQLog checkout used only by the benchmark")

function(qlog_read_git_metadata repository prefix)
    set(commit "unknown")
    set(dirty 1)

    if(Git_FOUND AND EXISTS "${repository}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${repository}" rev-parse HEAD
            RESULT_VARIABLE commit_result
            OUTPUT_VARIABLE commit_output
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(commit_result EQUAL 0)
            set(commit "${commit_output}")
        endif()

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" -C "${repository}" status --porcelain
            RESULT_VARIABLE status_result
            OUTPUT_VARIABLE status_output
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(status_result EQUAL 0 AND status_output STREQUAL "")
            set(dirty 0)
        endif()
    endif()

    set(${prefix}_COMMIT "${commit}" PARENT_SCOPE)
    set(${prefix}_DIRTY "${dirty}" PARENT_SCOPE)
endfunction()

qlog_read_git_metadata("${PROJECT_SOURCE_DIR}" QLOG_SOURCE)

set(benchmark_sources
    spsc_ring_benchmark.cpp
    qlog_benchmark.cpp
)

if(QLOG_BENCH_WITH_BQLOG)
    set(bqlog_siso_source
        "${QLOG_BENCH_BQLOG_ROOT}/src/bq_log/types/buffer/siso_ring_buffer.cpp")
    set(bqlog_siso_header
        "${QLOG_BENCH_BQLOG_ROOT}/src/bq_log/types/buffer/siso_ring_buffer.h")

    if(NOT EXISTS "${bqlog_siso_source}" OR NOT EXISTS "${bqlog_siso_header}")
        message(FATAL_ERROR
            "BQLog SISO sources were not found under QLOG_BENCH_BQLOG_ROOT="
            "${QLOG_BENCH_BQLOG_ROOT}")
    endif()

    qlog_read_git_metadata("${QLOG_BENCH_BQLOG_ROOT}" BQLOG_SOURCE)
    if(BQLOG_SOURCE_DIRTY)
        message(WARNING
            "BQLog checkout is dirty. Comparison output will be marked NON_REPRODUCIBLE.")
    endif()

    add_library(qlog_bqlog_siso STATIC "${bqlog_siso_source}")
    target_compile_features(qlog_bqlog_siso PRIVATE cxx_std_17)
    set_target_properties(qlog_bqlog_siso PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(qlog_bqlog_siso SYSTEM PRIVATE
        "${QLOG_BENCH_BQLOG_ROOT}/include"
        "${QLOG_BENCH_BQLOG_ROOT}/src"
        "${QLOG_BENCH_BQLOG_ROOT}/thirdparty"
    )

    list(APPEND benchmark_sources bqlog_benchmark.cpp)
endif()

add_executable(qlog_spsc_benchmark ${benchmark_sources})
target_compile_features(qlog_spsc_benchmark PRIVATE cxx_std_20)
set_target_properties(qlog_spsc_benchmark PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(qlog_spsc_benchmark PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(qlog_spsc_benchmark PRIVATE QLog::qlog Threads::Threads)
qlog_enable_warnings(qlog_spsc_benchmark)

target_compile_definitions(qlog_spsc_benchmark PRIVATE
    QLOG_BENCH_QLOG_COMMIT="${QLOG_SOURCE_COMMIT}"
    QLOG_BENCH_QLOG_DIRTY=${QLOG_SOURCE_DIRTY}
)

if(QLOG_BENCH_WITH_BQLOG)
    target_include_directories(qlog_spsc_benchmark SYSTEM PRIVATE
        "${QLOG_BENCH_BQLOG_ROOT}/include"
        "${QLOG_BENCH_BQLOG_ROOT}/src"
        "${QLOG_BENCH_BQLOG_ROOT}/thirdparty"
    )
    target_link_libraries(qlog_spsc_benchmark PRIVATE qlog_bqlog_siso)
    target_compile_definitions(qlog_spsc_benchmark PRIVATE
        QLOG_BENCH_WITH_BQLOG=1
        QLOG_BENCH_BQLOG_COMMIT="${BQLOG_SOURCE_COMMIT}"
        QLOG_BENCH_BQLOG_DIRTY=${BQLOG_SOURCE_DIRTY}
    )
else()
    target_compile_definitions(qlog_spsc_benchmark PRIVATE QLOG_BENCH_WITH_BQLOG=0)
endif()

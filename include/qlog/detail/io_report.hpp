#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "qlog/appender_config.hpp"
#include "qlog/management_result.hpp"

namespace qlog::detail {
struct IoErrorSlot final {
    bool present{false};
    IoFailure value;
};

class IoReport final {
   public:
    IoReport() = default;
    IoReport(const IoReport&) = delete;
    IoReport& operator=(const IoReport&) = delete;
    IoReport(IoReport&&) noexcept = default;
    IoReport& operator=(IoReport&&) noexcept = default;
    void record(std::size_t target, IoStage stage, int error, std::uint64_t unwritten,
                bool uncertain, const char* path, std::size_t path_size) noexcept;
    bool has_errors() const noexcept;
    std::vector<IoFailure> take_errors() noexcept;
    void clear() noexcept;
    // Identical cold-prepared target layout; copies observations, not OS events.
    void merge_from(const IoReport&) noexcept;

   private:
    friend IoReport prepare_io_report(const AppenderConfig* configs, std::size_t count);
    std::vector<std::array<IoErrorSlot, 4>> slots_;
    std::vector<IoFailure> export_;
    bool taken_{false};
};

struct IoHistory final {
    std::uint64_t event_count{0};
    std::uint64_t lost_bytes{0};
    bool first_present{false};
    IoFailure first;
};

IoReport prepare_io_report(const AppenderConfig* configs, std::size_t count);

}  // namespace qlog::detail
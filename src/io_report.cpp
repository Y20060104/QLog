#include "qlog/detail/io_report.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace qlog::detail {
namespace {
constexpr std::size_t kIoStageCount = 4U;
constexpr std::size_t kRecoveryPathExtra = sizeof(".recovery.") - 1U + 20U;
[[nodiscard]] std::size_t stage_index(IoStage stage) noexcept {
    switch (stage) {
        case IoStage::open:
            return 0U;

        case IoStage::write:
            return 1U;
        case IoStage::sync:
            return 2U;
        case IoStage::close:
            return 3U;
    }
    std::terminate();
}

constexpr IoStage kStages[kIoStageCount] = {
    IoStage::open,
    IoStage::write,
    IoStage::sync,
    IoStage::close,
};

void copy_prepared_path(std::string& destination, const char* source, std::size_t size) noexcept {
    if (source == nullptr && size != 0U) {
        std::terminate();
    }

    if (size > destination.capacity()) {
        std::terminate();
    }
    destination.resize(size);

    if (size != 0U) {
        std::memcpy(destination.data(), source, size);
    }
}
}  // namespace

IoReport prepare_io_report(const AppenderConfig* configs, std::size_t count) {
    if (configs == nullptr && count != 0U) {
        throw std::invalid_argument("null appender configs with nonzero count");
    }

    IoReport report;

    if (count > report.slots_.max_size() || count > report.export_.max_size() / kIoStageCount) {
        throw std::length_error("too many I/O report targets");
    }

    report.slots_.resize(count);
    report.export_.reserve(count * kIoStageCount);

    for (std::size_t target = 0U; target < count; ++target) {
        const auto& config = configs[target];

        for (std::size_t index = 0U; index < kIoStageCount; ++index) {
            auto& slot = report.slots_[target][index];
            slot.present = false;
            auto& failure = slot.value;

            failure.appender_name = config.name;
            failure.stage = kStages[index];
            failure.system_error = 0;
            failure.unwritten_bytes = 0U;
            failure.durability_uncertain = false;

            if (config.type == AppenderType::TextFile) {
                const auto base_size = config.file.path.size();
                const auto limit = failure.output_path.max_size();

                if (limit < kRecoveryPathExtra || base_size > limit - kRecoveryPathExtra) {
                    throw std::length_error("I/O report path is too long");
                }

                failure.output_path.reserve(base_size + kRecoveryPathExtra);
            }
        }
    }
    return report;
}

void IoReport::record(std::size_t target, IoStage stage, int error, std::uint64_t unwritten,
                      bool uncertain, const char* path, std::size_t path_size) noexcept {
    if (taken_ || target >= slots_.size()) {
        std::terminate();
    }

    if (path == nullptr && path_size != 0U) {
        std::terminate();
    }

    auto& slot = slots_[target][stage_index(stage)];
    auto& failure = slot.value;

    if (!slot.present) {
        copy_prepared_path(failure.output_path, path, path_size);

        failure.stage = stage;
        failure.system_error = error;
        failure.unwritten_bytes = unwritten;
        failure.durability_uncertain = uncertain;

        slot.present = true;
        return;
    }

    failure.unwritten_bytes = std::max(failure.unwritten_bytes, unwritten);
    failure.durability_uncertain = failure.durability_uncertain || uncertain;
}

bool IoReport::has_errors() const noexcept {
    if (taken_) {
        std::terminate();
    }
    for (const auto& target_slots : slots_) {
        for (const auto& slot : target_slots) {
            if (slot.present) {
                return true;
            }
        }
    }
    return false;
}

void IoReport::clear() noexcept {
    if (taken_) {
        std::terminate();
    }

    for (auto& target_slots : slots_) {
        for (auto& slot : target_slots) {
            slot.present = false;

            auto& failure = slot.value;
            failure.system_error = 0;
            failure.unwritten_bytes = 0U;
            failure.durability_uncertain = false;

            failure.output_path.clear();
        }
    }
    export_.clear();
}

std::vector<IoFailure> IoReport::take_errors() noexcept {
    if (taken_) {
        std::terminate();
    }

    for (auto& target_slots : slots_) {
        for (auto& slot : target_slots) {
            if (!slot.present) {
                continue;
            }
            if (export_.size() == export_.capacity()) {
                std::terminate();
            }

            export_.push_back(std::move(slot.value));
        }
    }
    taken_ = true;
    return std::move(export_);
}

void IoReport::merge_from(const IoReport& other) noexcept {
    if (taken_ || other.taken_ || slots_.size() != other.slots_.size()) std::terminate();
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        for (const auto& slot : other.slots_[i]) {
            if (!slot.present) continue;
            const auto& e = slot.value;
            record(i, e.stage, e.system_error, e.unwritten_bytes, e.durability_uncertain,
                   e.output_path.data(), e.output_path.size());
        }
    }
}
}  // namespace qlog::detail
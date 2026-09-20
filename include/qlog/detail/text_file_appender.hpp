#pragma once
#include <stdexcept>
#include <utility>

#include "qlog/detail/appender.hpp"

namespace qlog::detail {
// Cold construction only. Management maps this to SubmitStatus::open_failed.
class AppenderOpenError final : public std::runtime_error {
   public:
    explicit AppenderOpenError(IoFailure value)
        : std::runtime_error("QLog could not open a regular output file"),
          failure(std::move(value)) {}
    IoFailure failure;
};
class TextFileAppender final : public Appender {
   public:
    explicit TextFileAppender(AppenderConfig);
    ~TextFileAppender() noexcept override;

   private:
    int fd_{-1};
    std::string base_path_;
    std::string current_path_;
    std::string recovery_path_;
    std::uint64_t recovery_index_{0U};
    bool index_exhausted_{false};
    IoWriteResult write(const std::byte*, std::size_t) noexcept override;
    IoCallResult sync_output() noexcept override;
    IoCallResult close_output() noexcept override;
    IoCallResult reopen_output(IoReport&, std::size_t) noexcept override;
    std::string_view output_path() const noexcept override;
    void format_recovery_path() noexcept;
    void advance_recovery_index() noexcept;
};
}  // namespace qlog::detail

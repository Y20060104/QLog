#include "qlog/detail/text_file_appender.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cstring>
#include <exception>
#include <limits>

namespace qlog::detail {
namespace {
constexpr std::string_view kRecoverySuffix = ".recovery.";
constexpr std::size_t kExtra = kRecoverySuffix.size() + 20U;
// Own an unvalidated descriptor, including on throwing cold-path error creation.
struct OwnedFd final {
    int value;
    ~OwnedFd() {
        if (value >= 0) (void)::close(value);
    }
};
int validate_regular_file(int fd) noexcept {
    struct stat st {};
    if (::fstat(fd, &st) != 0) return errno;
    return S_ISREG(st.st_mode) ? 0 : EINVAL;
}
}  // namespace

TextFileAppender::TextFileAppender(AppenderConfig prepared)
    : Appender(std::move(prepared)), base_path_(config().file.path), current_path_(base_path_) {
    const auto size = base_path_.size();
    for (auto* path : {&current_path_, &recovery_path_}) {
        if (path->max_size() < kExtra || size > path->max_size() - kExtra)
            throw std::length_error("recovery path too long");
        path->reserve(size + kExtra);
    }
    // O_NONBLOCK prevents a configured FIFO from hanging before fstat rejects it.
    // It has no effect on regular-file I/O, which is the only accepted target.
    OwnedFd opened{
        ::open(base_path_.c_str(), O_CREAT | O_APPEND | O_WRONLY | O_CLOEXEC | O_NONBLOCK, 0644)};
    const int error = opened.value < 0 ? errno : validate_regular_file(opened.value);
    if (error != 0)
        throw AppenderOpenError({config().name, IoStage::open, error, 0U, false, base_path_});
    fd_ = std::exchange(opened.value, -1);
}

TextFileAppender::~TextFileAppender() noexcept {
    (void)close_output();
}
std::string_view TextFileAppender::output_path() const noexcept {
    return current_path_;
}
IoWriteResult TextFileAppender::write(const std::byte* data, std::size_t size) noexcept {
    const auto n = ::write(fd_, data, size);
    if (n < 0) return {0U, errno};
    return {static_cast<std::size_t>(n), 0};
}
IoCallResult TextFileAppender::sync_output() noexcept {
    if (::fdatasync(fd_) == 0) return {0};
    return {errno};
}
IoCallResult TextFileAppender::close_output() noexcept {
    const int old_fd = std::exchange(fd_, -1);
    if (old_fd < 0 || ::close(old_fd) == 0) return {0};
    return {errno};  // Linux: never retry close, even on EINTR.
}
void TextFileAppender::advance_recovery_index() noexcept {
    if (recovery_index_ == std::numeric_limits<std::uint64_t>::max())
        index_exhausted_ = true;
    else
        ++recovery_index_;
}
void TextFileAppender::format_recovery_path() noexcept {
    const auto prefix = base_path_.size() + kRecoverySuffix.size();
    recovery_path_.resize(prefix + 20U);  // capacity prepared before publication
    std::memcpy(recovery_path_.data(), base_path_.data(), base_path_.size());
    std::memcpy(recovery_path_.data() + base_path_.size(), kRecoverySuffix.data(),
                kRecoverySuffix.size());
    const auto result =
        std::to_chars(recovery_path_.data() + prefix, recovery_path_.data() + recovery_path_.size(),
                      recovery_index_);
    if (result.ec != std::errc{}) std::terminate();
    recovery_path_.resize(static_cast<std::size_t>(result.ptr - recovery_path_.data()));
}
IoCallResult TextFileAppender::reopen_output(IoReport& report, std::size_t index) noexcept {
    if (fd_ >= 0) std::terminate();
    // The final EEXIST was already reported/stored; no new syscall event here.
    if (index_exhausted_) return {EEXIST};
    for (std::uint32_t attempt = 0; attempt < kRecoveryNamesPerVisit; ++attempt) {
        format_recovery_path();
        const int candidate =
            ::open(recovery_path_.c_str(),
                   O_CREAT | O_EXCL | O_APPEND | O_WRONLY | O_CLOEXEC | O_NONBLOCK, 0644);
        if (candidate < 0) {
            const int error = errno;
            note_io_failure(report, index, IoStage::open, error, 0U, false, recovery_path_, true);
            if (error != EEXIST) return {error};
            advance_recovery_index();
            if (index_exhausted_) return {error};
            continue;
        }
        const int error = validate_regular_file(candidate);
        if (error != 0) {
            note_io_failure(report, index, IoStage::open, error, 0U, false, recovery_path_, true);
            if (::close(candidate) != 0) {
                const int close_error = errno;
                note_io_failure(report, index, IoStage::close, close_error, 0U, false,
                                recovery_path_, false);
            }
            // O_EXCL created this name. Never unlink it; move to the next name.
            advance_recovery_index();
            return {error};
        }
        current_path_.resize(recovery_path_.size());
        std::memcpy(current_path_.data(), recovery_path_.data(), recovery_path_.size());
        fd_ = candidate;
        advance_recovery_index();
        return {0};
    }
    return {EEXIST};  // at most 16 name attempts in this visit
}
}  // namespace qlog::detail

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace qlog::detail {

inline constexpr std::size_t kCacheLineSize = 64U;

struct SpscRingBufferTestAccess;

enum class ReserveStatus : std::uint8_t {
    ok,
    full,
    payload_too_large,
    reservation_pending,
};

enum class ReadStatus : std::uint8_t {
    ok,
    empty,
    corrupted,
    read_pending,
};

struct SpscRingBufferConfig {
    std::size_t capacity_bytes{64U * 1024U};
    std::size_t max_payload_bytes{8U * 1024U};
};

class SpscRingBuffer;
class WriteHandle;
class ReadHandle;

class WriteHandle final {
   public:
    WriteHandle(const WriteHandle&) noexcept = default;
    WriteHandle& operator=(const WriteHandle&) noexcept = default;
    WriteHandle(WriteHandle&&) noexcept = default;
    WriteHandle& operator=(WriteHandle&&) noexcept = default;
    ~WriteHandle() noexcept = default;

    [[nodiscard]] explicit operator bool() const noexcept {
        return frame_bytes() != 0U;
    }

    [[nodiscard]] ReserveStatus status() const noexcept {
        return static_cast<ReserveStatus>(frame_and_status_ & kStatusMask);
    }

    [[nodiscard]] std::byte* data() noexcept {
        return payload_;
    }

    [[nodiscard]] std::uint32_t size() const noexcept {
        return payload_bytes_;
    }

   private:
    friend class SpscRingBuffer;

    static constexpr std::uint32_t kStatusMask = 0x7U;
    static_assert(static_cast<std::uint32_t>(ReserveStatus::reservation_pending) <= kStatusMask);

    WriteHandle() noexcept = default;

    WriteHandle(std::byte* payload, std::uint32_t payload_bytes, std::uint32_t frame_bytes) noexcept
        : payload_(payload), frame_and_status_(frame_bytes), payload_bytes_(payload_bytes) {}

    explicit WriteHandle(ReserveStatus status) noexcept
        : frame_and_status_(static_cast<std::uint32_t>(status)) {}

    [[nodiscard]] std::uint32_t frame_bytes() const noexcept {
        return frame_and_status_ & ~kStatusMask;
    }

    std::byte* payload_{};
    std::uint32_t frame_and_status_{static_cast<std::uint32_t>(ReserveStatus::full)};
    std::uint32_t payload_bytes_{};
};

static_assert(sizeof(WriteHandle) == 16U);
static_assert(std::is_trivially_copyable_v<WriteHandle>);
static_assert(std::is_trivially_destructible_v<WriteHandle>);

class ReadHandle final {
   public:
    ReadHandle(const ReadHandle&) noexcept = default;
    ReadHandle& operator=(const ReadHandle&) noexcept = default;
    ReadHandle(ReadHandle&&) noexcept = default;
    ReadHandle& operator=(ReadHandle&&) noexcept = default;
    ~ReadHandle() noexcept = default;

    [[nodiscard]] explicit operator bool() const noexcept {
        return frame_bytes() != 0U;
    }

    [[nodiscard]] ReadStatus status() const noexcept {
        return static_cast<ReadStatus>(frame_and_status_ & kStatusMask);
    }

    [[nodiscard]] const std::byte* data() const noexcept {
        return payload_;
    }

    [[nodiscard]] std::uint32_t size() const noexcept {
        return payload_bytes_;
    }

   private:
    friend class SpscRingBuffer;

    static constexpr std::uint32_t kStatusMask = 0x7U;
    static_assert(static_cast<std::uint32_t>(ReadStatus::read_pending) <= kStatusMask);

    ReadHandle() noexcept = default;

    ReadHandle(const std::byte* payload, std::uint32_t payload_bytes,
               std::uint32_t frame_bytes) noexcept
        : payload_(payload), frame_and_status_(frame_bytes), payload_bytes_(payload_bytes) {}

    explicit ReadHandle(ReadStatus status) noexcept
        : frame_and_status_(static_cast<std::uint32_t>(status)) {}

    [[nodiscard]] std::uint32_t frame_bytes() const noexcept {
        return frame_and_status_ & ~kStatusMask;
    }

    const std::byte* payload_{};
    std::uint32_t frame_and_status_{static_cast<std::uint32_t>(ReadStatus::empty)};
    std::uint32_t payload_bytes_{};
};

static_assert(sizeof(ReadHandle) == 16U);
static_assert(std::is_trivially_copyable_v<ReadHandle>);
static_assert(std::is_trivially_destructible_v<ReadHandle>);

class SpscRingBuffer final {
   public:
    // 避免隐式转换
    explicit SpscRingBuffer(SpscRingBufferConfig config = {});
    ~SpscRingBuffer() noexcept;

    // 禁止拷贝构造
    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    // 禁止移动
    SpscRingBuffer(SpscRingBuffer&&) = delete;
    SpscRingBuffer& operator=(SpscRingBuffer&&) = delete;

    [[nodiscard]] WriteHandle try_reserve(std::size_t exact_payload_bytes) noexcept;
    void commit(const WriteHandle& write_handle) noexcept;
    void abort(const WriteHandle& write_handle) noexcept;
    [[nodiscard]] ReadHandle try_read() noexcept;
    void release(const ReadHandle& read_handle) noexcept;
    void abandon(const ReadHandle& read_handle) noexcept;
    void publish_reclaimed() noexcept;
    // Producer-only pressure hint. Cached read cursor may overestimate occupancy.
    [[nodiscard]] bool writer_at_least_half_full() const noexcept {
        return writer_state_.current_write_cursor_ - writer_state_.cached_read_cursor_ >=
               cold_state_.config_.capacity_bytes / 2U;
    }

   private:
    static constexpr std::size_t kStorageAlignment = 64;

    struct ValidatedConfig {
        std::size_t capacity_bytes{};
        std::size_t capacity_mask{};
        std::size_t max_payload_bytes{};
    };

    struct AlignedStorageDeleter {
        void operator()(std::byte* pointer) const noexcept;
    };

    using StorageOwner = std::unique_ptr<std::byte, AlignedStorageDeleter>;

    struct ColdState {
        explicit ColdState(ValidatedConfig config);

        ValidatedConfig config_{};
        StorageOwner storage_{};
    };

    [[nodiscard]] static ValidatedConfig validate_config(SpscRingBufferConfig config);

   private:
    struct alignas(kCacheLineSize) WriterState final {
        std::uint64_t current_write_cursor_{};
        std::uint64_t cached_read_cursor_{};
        bool reservation_pending_{};
    };

    struct CursorSet final {
        alignas(kCacheLineSize) std::atomic<std::uint64_t> write_cursor_{0};
        alignas(kCacheLineSize) std::atomic<std::uint64_t> read_cursor_{0};
    };

    struct alignas(kCacheLineSize) ReaderState final {
        std::uint64_t current_read_cursor_{};
        std::uint64_t cached_write_cursor_{};
        std::uint32_t records_since_publish_{};
        std::uint32_t bytes_since_publish_{};
        bool read_pending_{};
    };

    static_assert(alignof(WriterState) == kCacheLineSize);
    static_assert(sizeof(WriterState) == kCacheLineSize);
    static_assert(alignof(CursorSet) == kCacheLineSize);
    static_assert(sizeof(CursorSet) == 2 * kCacheLineSize);
    static_assert(alignof(ReaderState) == kCacheLineSize);
    static_assert(sizeof(ReaderState) == kCacheLineSize);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

    friend struct SpscRingBufferTestAccess;

    ColdState cold_state_;
    WriterState writer_state_;
    CursorSet cursors_;
    ReaderState reader_state_;
};

}  // namespace qlog::detail

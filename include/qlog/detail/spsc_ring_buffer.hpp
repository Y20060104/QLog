#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

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
    ~WriteHandle() noexcept;

    WriteHandle(const WriteHandle&) = delete;
    WriteHandle& operator=(const WriteHandle&) = delete;

    WriteHandle(WriteHandle&& other) noexcept;
    WriteHandle& operator=(WriteHandle&& other) noexcept;

    void commit() noexcept;
    void abort() noexcept;

    [[nodiscard]] explicit operator bool() const noexcept {
        return ring_ != nullptr;
    }

    [[nodiscard]] inline ReserveStatus status() const noexcept {
        return status_;
    }

    [[nodiscard]] inline std::byte* data() noexcept {
        return payload_;
    }

    [[nodiscard]] inline std::uint32_t size() const noexcept {
        return payload_bytes_;
    }

   private:
    friend class SpscRingBuffer;
    WriteHandle() noexcept = default;
    void deactivate() noexcept;

    SpscRingBuffer* ring_{};
    std::byte* payload_{};
    std::uint64_t next_write_cursor_{};
    std::uint32_t payload_bytes_{};
    ReserveStatus status_{ReserveStatus::full};
};

class ReadHandle final {
   public:
    ~ReadHandle() noexcept;

    ReadHandle(const ReadHandle&) = delete;
    ReadHandle& operator=(const ReadHandle&) = delete;

    ReadHandle(ReadHandle&& other) noexcept;
    ReadHandle& operator=(ReadHandle&& other) noexcept;

    void consume() noexcept;
    void abandon() noexcept;

    [[nodiscard]] explicit operator bool() const noexcept {
        return ring_ != nullptr;
    }

    [[nodiscard]] inline ReadStatus status() const noexcept {
        return status_;
    }

    [[nodiscard]] inline const std::byte* data() const noexcept {
        return payload_;
    }

    [[nodiscard]] inline std::uint32_t size() const noexcept {
        return payload_bytes_;
    }

   private:
    friend class SpscRingBuffer;
    ReadHandle() noexcept = default;
    void deactivate() noexcept;

    SpscRingBuffer* ring_{};
    const std::byte* payload_{};
    std::uint64_t next_read_cursor_{};
    std::uint32_t payload_bytes_{};
    ReadStatus status_{ReadStatus::empty};
};

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
    [[nodiscard]] ReadHandle try_peek() noexcept;
    void publish_reclaimed() noexcept;

   private:
    void commit_write(WriteHandle& write_handle) noexcept;
    void abort_write(WriteHandle& write_handle) noexcept;
    void consume_read(ReadHandle& read_handle) noexcept;
    void abandon_read(ReadHandle& read_handle) noexcept;

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

    friend class WriteHandle;
    friend class ReadHandle;
    friend struct SpscRingBufferTestAccess;

    ColdState cold_state_;
    WriterState writer_state_;
    CursorSet cursors_;
    ReaderState reader_state_;
};

}  // namespace qlog::detail

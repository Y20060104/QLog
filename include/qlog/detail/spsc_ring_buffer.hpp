#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>

namespace qlog::detail {
// 测试所需的前置声明
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
    std::size_t capacity_bytes{64 * 1024};
    std::size_t max_payload_bytes{8 * 1024};
};

class SpscRingBuffer;
class WriteReservation;
class ReadView;

class alignas(64) SpscWriteHandle final {
   public:
    SpscWriteHandle(const SpscWriteHandle&) = delete;
    SpscWriteHandle& operator=(const SpscWriteHandle&) = delete;
    SpscWriteHandle(SpscWriteHandle&&) = delete;
    SpscWriteHandle& operator=(SpscWriteHandle&&) = delete;

    [[nodiscard]] WriteReservation try_reserve(std::size_t exact_payload_bytes) noexcept;

   private:
    friend class SpscRingBuffer;
    explicit SpscWriteHandle(SpscRingBuffer& ring) noexcept;

    SpscRingBuffer* ring_{};
    std::uint64_t local_write_cursor_{};
    std::uint64_t read_cursor_cache_{};
    bool reservation_pending_{};
};

class alignas(64) SpscReadHandle final {
   public:
    SpscReadHandle(const SpscReadHandle&) = delete;
    SpscReadHandle& operator=(const SpscReadHandle&) = delete;
    SpscReadHandle(SpscReadHandle&&) = delete;
    SpscReadHandle& operator=(SpscReadHandle&&) = delete;

    [[nodiscard]] ReadView try_peek() noexcept;
    void publish_reclaimed() noexcept;

   private:
    friend class SpscRingBuffer;
    explicit SpscReadHandle(SpscRingBuffer& ring) noexcept;

    SpscRingBuffer* ring_{};
    std::uint64_t local_read_cursor_{};
    std::uint64_t write_cursor_cache_{};
    std::uint32_t records_since_publish_{};
    std::uint32_t bytes_since_publish_{};
    bool read_pending_{};
};

class WriteReservation final {
   public:
    ~WriteReservation() noexcept;

    WriteReservation(const WriteReservation&) = delete;
    WriteReservation& operator=(const WriteReservation&) = delete;

    WriteReservation(WriteReservation&&) noexcept;
    WriteReservation& operator=(WriteReservation&&) noexcept;

   private:
    friend class SpscWriteHandle;
    WriteReservation() noexcept;
};

class ReadView final {
   public:
    ~ReadView() noexcept;

    ReadView(const ReadView&) = delete;
    ReadView& operator=(const ReadView&) = delete;

    ReadView(ReadView&&) noexcept;
    ReadView& operator=(ReadView&&) noexcept;

   private:
    friend class SpscReadHandle;
    ReadView() noexcept;
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

    [[nodiscard]] SpscWriteHandle& write_handle() & noexcept;
    [[nodiscard]] SpscReadHandle& read_handle() & noexcept;

    SpscWriteHandle& write_handle() && = delete;
    SpscReadHandle& read_handle() && = delete;

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
    friend struct SpscRingBufferTestAccess;
    ColdState cold_state_;
};

}  // namespace qlog::detail
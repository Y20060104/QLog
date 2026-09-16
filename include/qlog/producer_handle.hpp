#pragma once

namespace qlog {

class AsyncLogger;

namespace detail {
class Channel;
}

class ProducerHandle final {
   public:
    ProducerHandle() noexcept = default;

    ProducerHandle(const ProducerHandle&) noexcept = default;
    ProducerHandle& operator=(const ProducerHandle&) noexcept = default;

    ~ProducerHandle() = default;

   private:
    friend class AsyncLogger;

    explicit ProducerHandle(detail::Channel* channel) noexcept : channel_(channel) {}

    detail::Channel* channel_{nullptr};
};

}  // namespace qlog
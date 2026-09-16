#pragma once
#include <cstdint>
#include <optional>

namespace qlog::detail {
class ProducerContext;

enum class ContextError : std::uint8_t {
    allocation_failed,
    tls_capacity_exhausted,
    registration_closed,
    producer_token_exhausted,
};

class ContextResult final {
   public:
    [[nodiscard]] static ContextResult success(ProducerContext& context) noexcept {
        return ContextResult(&context, std::nullopt);
    }
    [[nodiscard]] static ContextResult failure(ContextError error) noexcept {
        return ContextResult(nullptr, error);
    }
    [[nodiscard]] ProducerContext* context() const noexcept {
        return context_;
    }
    [[nodiscard]] const ContextError* error() const noexcept {
        return error_ ? &*error_ : nullptr;
    }

   private:
    ContextResult(ProducerContext* context, std::optional<ContextError> error) noexcept
        : context_(context), error_(error) {}
    ProducerContext* context_;
    std::optional<ContextError> error_;
};
}  // namespace qlog::detail

#include <chrono>
#include <qlog/detail/record_measure.hpp>
#include <vector>
struct X {};
int format_as(X) {
    return 1;
}
int main() {
    std::chrono::seconds v{1};
    auto result = qlog::detail::measure_record({nullptr, 0, 0}, 4096U, v);
    (void)result;
}

#include <chrono>
#include <qlog/detail/record_measure.hpp>
#include <vector>
struct X {};
int format_as(X) {
    return 1;
}
int main() {
    char8_t v = u8'x';
    auto result = qlog::detail::measure_record({nullptr, 0, 0}, 4096U, u8"x");
    (void)result;
}

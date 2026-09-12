#include <chrono>
#include <qlog/detail/record_measure.hpp>
#include <vector>
struct X {};
int format_as(X) {
    return 1;
}
int main() {
    X v;
    auto result = qlog::detail::measure_record({nullptr, 0, 0}, 4096U, format_as(v));
    (void)result;
}

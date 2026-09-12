#include <chrono>
#include <qlog/detail/record_measure.hpp>
#include <vector>
struct X {};
int format_as(X) {
    return 1;
}
int main() {
    const char* v = "x";
    auto result = qlog::detail::measure_record({nullptr, 0, 0}, 4096U, qlog::cstr(v));
    (void)result;
}

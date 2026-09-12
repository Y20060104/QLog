#include <chrono>
#include <qlog/detail/record_measure.hpp>
#include <vector>
struct X {};
int format_as(X) {
    return 1;
}
int main() {
    unsigned __int128 v = 1;
    auto result =
        qlog::detail::measure_record({nullptr, 0, 0}, 4096U, static_cast<unsigned long long>(v));
    (void)result;
}

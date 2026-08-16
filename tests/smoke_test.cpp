#include <qlog/version.hpp>

#include <cassert>

int main() {
    assert(qlog::version() == "0.1.0");
}
#include <qlog/version.hpp>

int main() {
    return qlog::version() == "0.1.0" ? 0 : 1;
}
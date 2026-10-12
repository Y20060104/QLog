#include "qlog/platform/file_defs.hpp"

namespace std {

std::size_t hash<qlog::platform::file_node_info>::operator()(
    const qlog::platform::file_node_info& node_info) const {
    return static_cast<std::size_t>(node_info.hash_code());
}
}  // namespace std
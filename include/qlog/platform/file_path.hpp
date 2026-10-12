#pragma once

#include <cstdint>
#include <string>

namespace qlog::platform {

bool is_absolute(const std::string& path);

std::string get_lexically_path(const std::string& original_path);

struct base_dir_initializer {
   private:
    std::string base_dir_0_;
    std::string base_dir_1_;

   public:
    const std::string& get_base_dir_0() const {
        return base_dir_0_;
    }

    const std::string& get_base_dir_1() const {
        return base_dir_1_;
    }

    void set_base_dir_0(const std::string& dir);
    void set_base_dir_1(const std::string& dir);
    // CR:为什么要两个string 分dir0/1？

    base_dir_initializer();
};
std::string get_base_dir(std::int32_t base_dir_type);

}  // namespace qlog::platform
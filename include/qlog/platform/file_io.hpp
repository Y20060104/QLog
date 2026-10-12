#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "qlog/platform/file_defs.hpp"

namespace qlog::platform {
std::int32_t get_file_size(const platform_file_handle& file_handle, std::size_t& size_ref);

std::int32_t read_file(const platform_file_handle& file_handle, void* target_addr,
                       std::size_t read_size, std::size_t& out_real_read_size);

std::int32_t write_file(const platform_file_handle& file_handle, const void* src_addr,
                        std::size_t write_size, std::size_t& out_real_write_size);

std::int32_t seek_file(const platform_file_handle& file_handle, file_seek_option opt,
                       std::int64_t offset);

std::int32_t truncate_file(const platform_file_handle& file_handle, std::size_t offset);

std::int32_t flush_file(const platform_file_handle& file_handle);

bool lock_file(const platform_file_handle& file_handle);

bool unlock_file(const platform_file_handle& file_handle);

std::int32_t get_file_size(const char* file_path, std::size_t& size_ref);

bool is_dir(const char* path);
bool is_regular_file(const char* path);

std::int32_t make_dir(const char* path);
std::int32_t remove_dir_or_file(const char* path);

std::int32_t open_file(const char* path, file_open_mode_enum mode,
                       platform_file_handle& out_file_handle);
std::int32_t close_file(platform_file_handle& in_out_file_handle);

std::uint64_t get_file_last_modified_epoch_ms(const char* path);
std::vector<std::string> get_all_sub_names(const char* path);
}  // namespace qlog::platform
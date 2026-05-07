# M6 `appender_file_raw.h` / `appender_file_raw.cpp`

## `src/qlog/appender/appender_file_raw.h`

```cpp
#pragma once

#include "qlog/appender/appender_file_binary.h"

namespace qlog::appender
{

class appender_file_raw : public appender_file_binary
{
public:
    static constexpr uint32_t format_version = 6;

protected:
    void log_impl(const entry_runtime_view& view) override;
    std::string get_file_ext_name() override;
    uint32_t get_binary_format_version() const override;

    appender_file_binary::appender_format_type get_appender_format() const override
    {
        return appender_file_binary::appender_format_type::raw;
    }
};

} // namespace qlog::appender
```

## `src/qlog/appender/appender_file_raw.cpp`

```cpp
#include "qlog/appender/appender_file_raw.h"

#include <cstring>

namespace qlog::appender
{

void appender_file_raw::log_impl(const entry_runtime_view& view)
{
    appender_file_base::log_impl(view);
    if (!view.entry_data || view.entry_size == 0)
    {
        return;
    }

    const uint32_t item_size = view.entry_size;
    auto wh = alloc_write_cache(sizeof(item_size) + item_size);
    std::memcpy(wh.data(), &item_size, sizeof(item_size));
    std::memcpy(wh.data() + sizeof(item_size), view.entry_data, item_size);
    return_write_cache(wh);
    mark_write_finished();
}

std::string appender_file_raw::get_file_ext_name()
{
    return ".lograw";
}

uint32_t appender_file_raw::get_binary_format_version() const
{
    return format_version;
}

} // namespace qlog::appender
```


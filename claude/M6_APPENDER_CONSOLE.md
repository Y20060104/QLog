# M6 `appender_console.h` / `appender_console.cpp`

## `src/qlog/appender/appender_console.h`

```cpp
#pragma once

#include "qlog/appender/appender_base.h"
#include "qlog/buffer/mpsc_ring_buffer.h"
#include "qlog/primitives/spin_lock.h"
#include "qlog/serialization/entry_format.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace qlog::appender
{

using type_func_ptr_console_callback = void (*
)(
    uint64_t log_id,
    int32_t category_idx,
    serialization::log_level log_level,
    const char* text,
    int32_t length
);

using type_func_ptr_console_buffer_fetch_callback = void (*)(
    void* pass_through_param,
    uint64_t log_id,
    int32_t category_idx,
    serialization::log_level log_level,
    const char* text,
    int32_t length
);

class appender_console : public appender_base
{
private:
    class console_callback_registry
    {
    public:
        void register_callback(type_func_ptr_console_callback callback);
        [[nodiscard]] type_func_ptr_console_callback get() const;

    private:
        mutable spin_lock lock_ {};
        type_func_ptr_console_callback callback_ = nullptr;
    };

    class console_buffer
    {
    public:
        console_buffer();
        ~console_buffer();

        void insert(
            uint64_t epoch_ms,
            uint64_t log_id,
            int32_t category_idx,
            serialization::log_level log_level,
            const char* content,
            int32_t length
        );

        bool fetch_and_remove(
            type_func_ptr_console_buffer_fetch_callback callback,
            const void* pass_through_param
        );

        void set_enable(bool enable);
        [[nodiscard]] bool is_enable() const;

    private:
        mpsc_ring_buffer* get_or_create_buffer();

    private:
        std::atomic<bool> enable_ { false };
        std::atomic<mpsc_ring_buffer*> buffer_ { nullptr };
        spin_lock init_lock_ {};
        std::atomic<uint64_t> fetch_thread_id_ { 0 };
    };

public:
    appender_console() = default;
    ~appender_console() override = default;

    static void register_console_callback(type_func_ptr_console_callback callback);
    static void unregister_console_callback(type_func_ptr_console_callback callback);

    static void set_console_buffer_enable(bool enable);
    static bool fetch_and_remove_from_console_buffer(
        type_func_ptr_console_buffer_fetch_callback callback,
        const void* pass_through_param
    );

protected:
    bool init_impl(const appender_config& config) override;
    bool reset_impl(const appender_config& config) override;
    void log_impl(const entry_runtime_view& view) override;

    void on_log_item_recovery_begin(entry_runtime_view& read_view) override;
    void on_log_item_recovery_end() override;

private:
    static console_callback_registry& callback_registry();
    static console_buffer& buffer_registry();
    static void default_console_output(serialization::log_level level, const char* text, int32_t len);

private:
    std::string log_name_prefix_ {};
    std::string log_entry_cache_ {};
};

} // namespace qlog::appender
```

## `src/qlog/appender/appender_console.cpp`

```cpp
#include "qlog/appender/appender_console.h"

#include "qlog/serialization/serializer.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace qlog::appender
{

namespace
{
constexpr const char* k_recover_start =
    "/************************************* QLOG RECOVER START *************************************/";
constexpr const char* k_recover_end =
    "/************************************* QLOG RECOVER END *************************************/";
} // namespace

void appender_console::console_callback_registry::register_callback(
    type_func_ptr_console_callback callback
)
{
    scoped_spin_lock lock(lock_);
    callback_ = callback;
}

type_func_ptr_console_callback appender_console::console_callback_registry::get() const
{
    scoped_spin_lock lock(lock_);
    return callback_;
}

appender_console::console_buffer::console_buffer() = default;

appender_console::console_buffer::~console_buffer()
{
    auto* ptr = buffer_.exchange(nullptr, std::memory_order_acq_rel);
    delete ptr;
}

mpsc_ring_buffer* appender_console::console_buffer::get_or_create_buffer()
{
    auto* p = buffer_.load(std::memory_order_acquire);
    if (p)
    {
        return p;
    }

    scoped_spin_lock lock(init_lock_);
    p = buffer_.load(std::memory_order_relaxed);
    if (!p)
    {
        p = new mpsc_ring_buffer(8 * 1024 * 1024);
        buffer_.store(p, std::memory_order_release);
    }
    return p;
}

void appender_console::console_buffer::insert(
    uint64_t,
    uint64_t log_id,
    int32_t category_idx,
    serialization::log_level log_level,
    const char* content,
    int32_t length
)
{
    if (!is_enable())
    {
        return;
    }
    if (!content || length < 0)
    {
        return;
    }

    auto* rb = get_or_create_buffer();
    const uint32_t payload_size = static_cast<uint32_t>(
        sizeof(uint64_t) + sizeof(int32_t) + sizeof(uint8_t) + sizeof(int32_t) +
        static_cast<uint32_t>(length) + 1u
    );

    auto wh = rb->alloc_write_chunk(payload_size);
    if (!wh.success || !wh.data)
    {
        return;
    }

    uint8_t* p = wh.data;
    std::memcpy(p, &log_id, sizeof(log_id));
    p += sizeof(log_id);
    std::memcpy(p, &category_idx, sizeof(category_idx));
    p += sizeof(category_idx);
    *p++ = static_cast<uint8_t>(log_level);
    std::memcpy(p, &length, sizeof(length));
    p += sizeof(length);
    std::memcpy(p, content, static_cast<size_t>(length));
    p += static_cast<size_t>(length);
    *p = 0;

    rb->commit_write_chunk(wh);
}

bool appender_console::console_buffer::fetch_and_remove(
    type_func_ptr_console_buffer_fetch_callback callback,
    const void* pass_through_param
)
{
    if (!is_enable() || !callback)
    {
        return false;
    }

    const uint64_t current_tid = serialization::get_current_thread_id();
    uint64_t expected = 0;
    if (!fetch_thread_id_.compare_exchange_strong(expected, current_tid, std::memory_order_acq_rel))
    {
        if (expected != current_tid)
        {
            constexpr const char* err = "Don't fetch console buffer in different threads";
            callback(
                const_cast<void*>(pass_through_param),
                0,
                0,
                serialization::log_level::error,
                err,
                static_cast<int32_t>(std::strlen(err))
            );
            return false;
        }
    }

    auto* rb = buffer_.load(std::memory_order_acquire);
    if (!rb)
    {
        return false;
    }

    auto rh = rb->read_chunk();
    if (!rh.success || !rh.data)
    {
        return false;
    }

    const uint8_t* p = rh.data;
    uint64_t log_id = 0;
    int32_t category_idx = 0;
    uint8_t lv = 0;
    int32_t len = 0;

    std::memcpy(&log_id, p, sizeof(log_id));
    p += sizeof(log_id);
    std::memcpy(&category_idx, p, sizeof(category_idx));
    p += sizeof(category_idx);
    lv = *p++;
    std::memcpy(&len, p, sizeof(len));
    p += sizeof(len);

    callback(
        const_cast<void*>(pass_through_param),
        log_id,
        category_idx,
        static_cast<serialization::log_level>(lv),
        reinterpret_cast<const char*>(p),
        len
    );

    rb->commit_read_chunk(rh);
    return true;
}

void appender_console::console_buffer::set_enable(bool enable)
{
    enable_.store(enable, std::memory_order_release);
}

bool appender_console::console_buffer::is_enable() const
{
    return enable_.load(std::memory_order_acquire);
}

appender_console::console_callback_registry& appender_console::callback_registry()
{
    static console_callback_registry g_registry;
    return g_registry;
}

appender_console::console_buffer& appender_console::buffer_registry()
{
    static console_buffer g_buffer;
    return g_buffer;
}

void appender_console::register_console_callback(type_func_ptr_console_callback callback)
{
    callback_registry().register_callback(callback);
}

void appender_console::unregister_console_callback(type_func_ptr_console_callback callback)
{
    auto cur = callback_registry().get();
    if (cur == callback)
    {
        callback_registry().register_callback(nullptr);
    }
}

void appender_console::set_console_buffer_enable(bool enable)
{
    buffer_registry().set_enable(enable);
}

bool appender_console::fetch_and_remove_from_console_buffer(
    type_func_ptr_console_buffer_fetch_callback callback,
    const void* pass_through_param
)
{
    return buffer_registry().fetch_and_remove(callback, pass_through_param);
}

void appender_console::default_console_output(
    serialization::log_level level,
    const char* text,
    int32_t len
)
{
    if (!text || len <= 0)
    {
        return;
    }
    std::FILE* out = (level >= serialization::log_level::error) ? stderr : stdout;
    std::fwrite(text, 1, static_cast<size_t>(len), out);
    std::fputc('\n', out);
    std::fflush(out);
}

bool appender_console::init_impl(const appender_config&)
{
    log_name_prefix_.clear();
    log_name_prefix_.push_back('[');
    log_name_prefix_.append(get_name());
    log_name_prefix_.append("]\t");
    log_entry_cache_ = log_name_prefix_;
    return true;
}

bool appender_console::reset_impl(const appender_config&)
{
    log_entry_cache_ = log_name_prefix_;
    return true;
}

void appender_console::log_impl(const entry_runtime_view& view)
{
    if (!layout_ptr_)
    {
        return;
    }

    const auto formatted = layout_ptr_->do_layout(
        view.entry_data,
        view.entry_size,
        view.format_string,
        view.categories,
        view.category_count
    );

    log_entry_cache_.assign(log_name_prefix_);
    log_entry_cache_.append(formatted.data(), formatted.size());

    const auto* hdr = view.header();
    if (!hdr)
    {
        return;
    }
    const auto level = static_cast<serialization::log_level>(hdr->level);

    if (buffer_registry().is_enable())
    {
        buffer_registry().insert(
            hdr->timestamp_ms,
            view.log_id,
            static_cast<int32_t>(hdr->category_idx),
            level,
            log_entry_cache_.c_str(),
            static_cast<int32_t>(log_entry_cache_.size())
        );
    }
    else
    {
        default_console_output(level, log_entry_cache_.c_str(), static_cast<int32_t>(log_entry_cache_.size()));
    }

    auto cb = callback_registry().get();
    if (cb)
    {
        cb(
            view.log_id,
            static_cast<int32_t>(hdr->category_idx),
            level,
            log_entry_cache_.c_str(),
            static_cast<int32_t>(log_entry_cache_.size())
        );
    }
}

void appender_console::on_log_item_recovery_begin(entry_runtime_view&)
{
    default_console_output(
        serialization::log_level::info,
        k_recover_start,
        static_cast<int32_t>(std::strlen(k_recover_start))
    );
}

void appender_console::on_log_item_recovery_end()
{
    default_console_output(
        serialization::log_level::info,
        k_recover_end,
        static_cast<int32_t>(std::strlen(k_recover_end))
    );
}

} // namespace qlog::appender
```


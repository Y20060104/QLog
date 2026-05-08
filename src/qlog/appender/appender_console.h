#pragma once

#include "qlog/appender/appender_base.h"
#include "qlog/buffer/mpsc_ring_buffer.h"
#include "qlog/primitives/spin_lock.h"
#include "qlog/serialization/entry_format.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace qlog::appender{
    // 回调函数
    using type_func_ptr_console_callback=void (*)(uint64_t log_id,int32_t category_idx,serialization::log_level log_level,const char* text,int32_t length);
    // 从别的缓冲区获取的日志回调函数
    using type_func_ptr_console_buffer_fetch_callback=void(*)(void* pass_through_param,uint64_t log_id,int32_t category_idx,serialization::log_level log_level,const char* text,int32_t length);

    class appender_console:public appender_base{
        private:
            class console_callback_registry
            {
                public:
                void register_callback(type_func_ptr_console_callback callback);
                [[nodiscard]]   type_func_ptr_console_callback get()const;

                private:
                mutable spin_lock lock_{};
                type_func_ptr_console_callback callback_=nullptr;
            };

            class console_buffer{
                public:
                    console_buffer();
                    ~console_buffer();

                    void insert(uint64_t epoch_ms,uint64_t log_id ,int32_t category_idx,serialization::log_level log_level,const char* content ,int32_t length);

                    bool fetch_and_remove(type_func_ptr_console_buffer_fetch_callback callback,const void* pass_through_param);

                    void set_enable(bool enbale);
                    [[nodiscard]] bool is_enable()const;
                    private:
                    mpsc_ring_buffer* get_or_creat_buffer();

                    private:
                    std::atomic<bool>enable_{false};
                    std::atomic<mpsc_ring_buffer*> buffer_{nullptr};
                    spin_lock init_lock_{};
                    std::atomic<uint64_t> fetch_thread_id_{0};
            };

            public: 
            appender_console()=default;
            ~appender_console()override =default;

            static void register_console_callback(type_func_ptr_console_callback callback);
            static void unregister_console_callback(type_func_ptr_console_callback callback);

            static void set_console_buffer_enable(bool enable);
            static bool fetch_and_remove_from_console_buffer(type_func_ptr_console_buffer_fetch_callback callback,const void* pass_through_param);

            protected:
            bool init_impl(const appender_config& config)override;
            bool reset_impl(const appender_config& config)override;
            void log_impl(const entry_runtime_view& view)override;

            void on_log_item_recovery_begin(entry_runtime_view& read_view)override;
            void on_log_item_recovery_end()override;

            private:
            static console_callback_registry& callback_registry();
            static console_buffer& buffer_registry();
            static void default_console_output(serialization::log_level level,const char* text,int32_t len);

            private:
            std::string log_name_prefix_{};
            std::string log_entry_cache_{};
    };

}// namespace qlog::appender
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "qlog/platform/atomic/atomic.hpp"

namespace qlog::platform {

enum class enum_thread_status : std::uint8_t {
    init,
    running,
    pendding_cancel,
    finished,
    released,
    detached,
    error
};

struct thread_attr {
    std::size_t max_stack_size = 512 * 1024;
};

class thread {
   public:
    typedef std::uint64_t thread_id;

    thread(thread_attr attr = thread_attr());

    thread(const thread& rhs) = delete;
    thread(thread&& rhs) = delete;
    thread& operator=(const thread& rhs) = delete;
    thread& operator=(thread&& rhs) = delete;

    void set_thread_name(const std::string& thread_name);

    void start();
    void join();
    void detach();

    bool cancel();
    bool is_cancelled();

    static void yield();
    static void cpu_relax();
    static void sleep(std::uint64_t millsec);

    static std::string get_current_thread_name();
    static thread_id get_current_thread_id();

    thread_id get_thread_id() const {
        return thread_id_;
    }

    const std::string& get_thread_name() const {
        return thread_name_;
    }

    enum_thread_status get_status() const {
        return status_.load(memory_order::seq_cst);
    }

    virtual ~thread();

   protected:
    virtual void run() = 0;
    virtual void on_finished() {}

   private:
    void internal_run();
    void apply_thread_name();
    void apply_name_raise();

    std::string thread_name_;
    thread_id thread_id_;
    const thread_attr attr_;
    qlog::platform::atomic<enum_thread_status> status_;
    struct thread_platform_def* platform_data_;

    friend struct thread_platform_processor;
};

}  // namespace qlog::platform